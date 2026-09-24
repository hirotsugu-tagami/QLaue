#ifndef PayloadDir
  #define PayloadDir "..\dist\windows"
#endif

[Setup]
AppId={{31B1E247-6909-4D04-9AF2-5B9C50AE33ED}
AppName=QLaue
AppVersion=0.2
AppVerName=QLaue 0.2 preview
AppPublisher=QLaue contributors
AppPublisherURL=https://github.com/hirotsugu-tagami/QLaue
DefaultDirName={localappdata}\Programs\QLaue
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
DisableProgramGroupPage=yes
OutputDir=..\dist\installers
OutputBaseFilename=QLaue-windows-x64-setup
SetupIconFile=..\images\crystal.ico
UninstallDisplayIcon={app}\QLaue.exe
LicenseFile=..\LICENSE
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\QLaue"; Filename: "{app}\QLaue.exe"

[Run]
Filename: "{app}\QLaue.exe"; Description: "Launch QLaue"; Flags: nowait postinstall skipifsilent
