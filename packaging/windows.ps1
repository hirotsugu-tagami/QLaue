# Run with PowerShell 7, Qt 5 on PATH, Visual Studio 2022 and Inno Setup 6.
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
Set-Location (Split-Path $PSScriptRoot -Parent)
$repo = (Get-Location).Path
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual C++ build tools were not found.' }
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}
$qt = (& qmake -query QT_INSTALL_PREFIX).Trim()
$commit = (& git rev-parse HEAD).Trim()

foreach ($project in @('QLaue', 'image_import', 'crystal_features', 'print_alignment')) {
    $build = New-Item -ItemType Directory -Force "build-$project"
    Push-Location $build
    try {
        $pro = if ($project -eq 'QLaue') { "$repo/QLaue.pro" } else { "$repo/tests/$project.pro" }
        & qmake $pro -spec win32-msvc -config release 'QMAKE_CXXFLAGS+=/MP'
        & nmake /NOLOGO release
    } finally { Pop-Location }
}
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_LOGGING_TO_CONSOLE = '1'
foreach ($program in @(
    './build-image_import/release/image-import-check.exe',
    './build-crystal_features/release/crystal-features-check.exe',
    './build-print_alignment/release/print-alignment-check.exe'
)) {
    $arguments = @()
    if ($program -like '*print-alignment-check.exe') { $arguments = @('./build-print_alignment/output') }
    try { & $program @arguments } catch {
        $debugger = "${env:ProgramFiles(x86)}/Windows Kits/10/Debuggers/x64/cdb.exe"
        if (Test-Path $debugger) {
            & $debugger -c 'sxe -c ".ecxr; k; q" av; g' $program @arguments
        }
        throw
    }
}
Remove-Item env:QT_QPA_PLATFORM

$payload = (New-Item -ItemType Directory -Force dist/windows).FullName
Copy-Item build-QLaue/release/QLaue.exe $payload
& windeployqt --release --no-compiler-runtime --no-translations --no-opengl-sw --no-angle --no-system-d3d-compiler "$payload/QLaue.exe"
# App-local CRT keeps installation per-user and usable without a separate download.
Copy-Item "$env:VCToolsRedistDir/x64/Microsoft.VC143.CRT/*.dll" $payload
Copy-Item LICENSE "$payload/LICENSE-QLaue.txt"
Copy-Item docs/AUDIT-2026-09-24.md $payload
@"
QLaue 0.2 preview for Windows 10/11 x64
Qt and the Visual C++ runtime are included. No Qt or Python installation is needed.
Save your analysis and close QLaue before upgrading. Start QLaue from the Start menu.
Uninstall using Windows Settings > Apps. Your saved analysis files are retained.

QLaue source (GPL-2.0-or-later): https://github.com/hirotsugu-tagami/QLaue/tree/$commit
Qt 5.15.2 source: https://download.qt.io/archive/qt/5.15/5.15.2/single/qt-everywhere-src-5.15.2.tar.xz
Qt is dynamically linked. Its licenses and third-party notices are in licenses/.
Visual C++ runtime: https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files
This preview has outstanding findings documented in AUDIT-2026-09-24.md.
The installer is not Authenticode-signed.
"@ | Set-Content "$payload/README.txt" -Encoding utf8

$qtSource = Join-Path (Split-Path $qt -Parent) Src
foreach ($module in @('qtbase', 'qtimageformats', 'qtsvg')) {
    $source = Join-Path $qtSource $module
    if (-not (Test-Path "$source/LICENSE.LGPL3")) { throw "Missing Qt source licenses: $source" }
    # Preserve license/attribution paths, including the bundled third-party components.
    Get-ChildItem $source -Recurse -File | Where-Object {
        $_.Name -match '(?i)^(license|copying|copyright|notice)|^qt_attribution\.json$'
    } | ForEach-Object {
        $relative = [IO.Path]::GetRelativePath($source, $_.FullName)
        $destination = Join-Path "$payload/licenses/$module" $relative
        New-Item -ItemType Directory -Force (Split-Path $destination -Parent) | Out-Null
        Copy-Item $_.FullName $destination
    }
}
@{
    source_commit = $commit
    architecture = 'x64'
    qt_version = (& qmake -query QT_VERSION).Trim()
    compiler = 'MSVC 2022'
    runtime_version = (Get-Item "$payload/vcruntime140.dll").VersionInfo.FileVersion
    tested_os = [Environment]::OSVersion.VersionString
    workflow_run = $env:GITHUB_RUN_ID
    signing = 'unsigned'
} | ConvertTo-Json | Set-Content "$payload/build-manifest.json" -Encoding utf8

& "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe" "/DPayloadDir=$payload" packaging/windows.iss
$installer = (Resolve-Path dist/installers/QLaue-windows-x64-setup.exe).Path
$check = (New-Item -ItemType Directory -Force build-installer-check).FullName
$installed = Join-Path $check installed
# Install twice to exercise the upgrade path, then launch without Qt on PATH.
foreach ($attempt in 1..2) {
    $setup = Start-Process $installer -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/DIR=`"$installed`"", "/LOG=`"$check/install-$attempt.log`"") -PassThru -Wait
    if ($setup.ExitCode -ne 0) { throw "Installer failed: $($setup.ExitCode)" }
}
foreach ($required in @('QLaue.exe', 'Qt5Core.dll', 'Qt5Widgets.dll', 'platforms/qwindows.dll', 'imageformats/qtiff.dll', 'vcruntime140.dll', 'README.txt', 'build-manifest.json')) {
    if (-not (Test-Path "$installed/$required")) { throw "Installer omitted $required" }
}
$savedPath = $env:PATH
try {
    $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
    Remove-Item env:QT_PLUGIN_PATH, env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
    $app = Start-Process "$installed/QLaue.exe" -WorkingDirectory $check -PassThru
    Start-Sleep -Seconds 5
    if ($app.HasExited) { throw "Installed QLaue exited during startup: $($app.ExitCode)" }
    $app.Refresh()
    if ($app.MainWindowHandle -eq 0) { throw 'Installed QLaue did not open a window.' }
    $modules = @($app.Modules | Where-Object { $_.ModuleName -match '^Qt5|^qwindows\.dll$|^(msvcp140|vcruntime140)' })
    if ($modules.Count -lt 5) { throw 'Could not verify the deployed runtime libraries.' }
    foreach ($module in $modules) {
        if (-not $module.FileName.StartsWith($installed, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Runtime loaded outside installation: $($module.FileName)"
        }
    }
    $modules | Select-Object ModuleName, FileName | Out-File "$check/runtime.log"
} finally {
    if ($app -and -not $app.HasExited) { Stop-Process -Id $app.Id }
    $env:PATH = $savedPath
}
$uninstall = Start-Process "$installed/unins000.exe" -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$check/uninstall.log`"") -PassThru -Wait
if ($uninstall.ExitCode -ne 0 -or (Test-Path "$installed/QLaue.exe")) { throw 'Uninstall failed.' }
$hash = (Get-FileHash $installer -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  QLaue-windows-x64-setup.exe" | Set-Content dist/installers/SHA256SUMS-windows.txt -Encoding ascii
Copy-Item "$payload/build-manifest.json" dist/installers/build-manifest-windows.json
Write-Output 'PASS Windows regression checks, install, upgrade, isolated runtime startup and uninstall.'
