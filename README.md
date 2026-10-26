# QLaue

QLaue is a Qt desktop application for orienting single-crystal samples using the
Laue method. The original project is
[stuwilkins/QLaue](https://github.com/stuwilkins/QLaue).

This source tree includes changes to build the application with Qt 5 on macOS
and Windows.

## Download

| Platform | Installer | Installation |
| --- | --- | --- |
| Windows 10/11, x64 | [QLaue-windows-x64-setup.exe](https://github.com/hirotsugu-tagami/QLaue/releases/download/preview-2026-10-26-r2/QLaue-windows-x64-setup.exe) | Run the setup wizard, then open QLaue from the Start menu. |
| macOS, Apple Silicon | [QLaue-macos-arm64.dmg](https://github.com/hirotsugu-tagami/QLaue/releases/download/preview-2026-10-26-r2/QLaue-macos-arm64.dmg) | Open the disk image and drag QLaue.app to Applications. |

Save your analysis and close the old application before upgrading. Qt and the
required runtime libraries are included; no separate Qt, Conda or Python
installation is needed. The Windows installer installs for the current user
without administrator privileges and provides an uninstaller. The macOS build
is for Apple Silicon (arm64), with a macOS 11.0 deployment target, checked on
macOS 26.7.1. An Intel Mac build is not included.

See the [release notes and checksums](https://github.com/hirotsugu-tagami/QLaue/releases/tag/preview-2026-10-26-r2)
for the source revision and validation details.

These are **prereleases**. The [current fix record](docs/FIXES-2026-10-26-APP-FUNCTIONS.md)
describes the resolved findings and validation limits; earlier audit reports retain
their historical findings. The Windows installer is not
Authenticode-signed. The macOS app has an ad-hoc signature and has not been
signed with an Apple Developer ID or notarized. If macOS blocks the first launch,
verify the download source and follow
[Apple's instructions for opening the app](https://support.apple.com/102445).

## Build on macOS

Requirements:

- Xcode or the Xcode Command Line Tools, with a working C++ compiler and `make`.
- A Qt 5 development installation with Core, Gui, Widgets, Xml, Network and
  PrintSupport, including `qmake`, `moc`, `uic` and `rcc`.

The application has been built with Qt 5.15.15 on Apple Silicon. Qt 6 is not
supported by this source tree.

From the repository root, set `QT_BIN` to the `bin` directory of your Qt 5
installation, then build in a separate directory:

```sh
export QT_BIN="/path/to/qt5/bin"
"$QT_BIN/qmake" -v
mkdir -p build-qt5
cd build-qt5
"$QT_BIN/qmake" ../QLaue.pro
make -j4
open QLaue.app
```

Check that `qmake -v` reports Qt 5. If you use a Conda environment containing Qt 5,
activate that environment and use `export QT_BIN="$CONDA_PREFIX/bin"` instead.
Qt 5 may warn when building against a newer macOS SDK; a successful build does
not establish compatibility with every macOS version.

## Bundle Qt Libraries

The initial build uses libraries from the Qt installation. To copy the required
libraries and plugins into the application, run the matching Qt 5 deployment
tool from `build-qt5`:

```sh
"$QT_BIN/macdeployqt" QLaue.app
codesign --force --deep --sign - QLaue.app
codesign --verify --deep --strict QLaue.app
```

Deployment, code signing and runtime testing are separate steps. The commands
above apply a local ad-hoc signature. Apple Developer ID signing and notarization
have not been performed for the published preview; see its release notes for
the tested environment and known limitations.

## Build Installers

The [Windows installer workflow](.github/workflows/windows-installer.yml) can be
run manually in GitHub Actions, or by pushing a `preview-*` tag. It builds with
Qt 5.15.2/MSVC on Windows Server 2022, runs the image/CIF/rotation/print checks,
and uses Qt's `windeployqt` and Inno Setup to create a single setup executable.
It tests installation, upgrade, launch with the bundled runtime, and uninstall.
Download `QLaue-windows-x64` from the successful run's artifacts; its contents
include the `.exe`, SHA-256 checksum and build manifest. Publication to Releases
is a separate step. For a local build with the same tools, run
`pwsh -File packaging/windows.ps1` with Qt 5 on `PATH` and its matching source
modules in the adjacent `Src` directory (for license notices).

On macOS, prepare a directory containing the deployed and signed `QLaue.app`,
`README.txt`, `LICENSE-QLaue.txt`, `build-manifest.json`, and `licenses/` with the
runtime notices. Include dependency build recipes and patches when applicable.
Then run:

```sh
bash packaging/macos-dmg.sh dist/macos-payload dist/installers/QLaue-macos-arm64.dmg
```

The script creates and verifies a compressed disk image with an Applications
shortcut. Verify a mounted copy and its startup before publishing the `.dmg`
and `.exe` together with their checksums as GitHub Release assets.

## Image Import and Checks

Use **Laue > Import Image** to open a bitmap. The image picker lists formats
supported by the running Qt installation, including BMP, and provides **All
Files** for images with unusual or missing extensions. Optional formats such as
TIFF depend on the installed image plugins. A failed import reports the decoder
error and keeps the existing image.

To run the image import regression check with Qt 5, from the repository root:

```sh
mkdir -p build-image-check
cd build-image-check
"$QT_BIN/qmake" ../tests/image_import.pro
make -j4
QT_QPA_PLATFORM=offscreen ./image-import-check
```

The check exercises the application's import dialog and image-loading slot with
BMP (including indexed and monochrome images), PNG, JPEG, TIFF when available,
Japanese file names, cancellation and invalid input. It uses Qt's non-native
dialog for unattended execution; macOS's native file picker needs a separate
interactive check.

## Import a Crystal from CIF

Open **Set Lattice**, click **Import CIF...**, and select the CIF file. The dialog
fills the six cell parameters, space group, description and fractional atom
coordinates. Review the values and click **OK** to apply them. **Cancel** leaves
the current crystal unchanged; a failed import also preserves the editor contents.
The existing crystal orientation is retained when the lattice is applied.
Source atom coordinates retain their signs and integer cell offsets on import
and after **OK**; for example, `-0.3333` stays `-0.3333`. Generated symmetry
equivalents are reduced to the unit cell, and periodic copies are counted once.
Uncertainties in parentheses are read as uncertainties, so `-0.0247(11)` is
displayed as its central value, `-0.0247`.

The importer supports conventional small-molecule/inorganic CIF 1.1 structure
files: quoted and multiline values, comments, reordered atom-loop columns,
uncertainties such as `5.431(2)`, scientific notation and H through Cf. It uses
Hall symbols, Hermann–Mauguin symbols or IT numbers with the application's space
group table. Supplied symmetry operations are checked against the selected
setting; cell metrics distinguish hexagonal/rhombohedral axes. If operations or
a Hall symbol are absent and multiple settings remain, the first matching
database setting is selected, so check the setting in the dialog.

One structure per file is supported (unrelated metadata blocks are allowed).
For multiple structures, export a single data block first. CIF 2.0, partial
occupancies, unknown elements/coordinates and settings absent from the internal
table produce an explanatory error. Zero-occupancy sites are skipped. Atom
displacement parameters are not imported because QLaue has no corresponding
model. Atom counts are conservatively limited so their symmetry equivalents
fit the existing 2048-atom storage. Files are limited to 16 MiB.

The parser follows the [IUCr CIF 1.1 syntax](https://www.iucr.org/resources/cif/spec/version1.1/cifsyntax).
No additional runtime library or Python installation is needed.

The **Crystal Rotations** X/Y/Z arrows apply the chosen step on every click,
including in goniometer mode. Reorientation commands that set an absolute angle
keep their existing behavior.

To run the CIF and repeated-rotation regression checks:

```sh
mkdir -p build-crystal-check
cd build-crystal-check
"$QT_BIN/qmake" ../tests/crystal_features.pro
make -j4
QT_QPA_PLATFORM=offscreen ./crystal-features-check
```

These checks exercise real Qt controls with the non-native file dialog. They
cover CIF validation, failed/cancelled imports, 51 atom sites, hydrogen, trigonal
symmetry, signed source coordinates through import/display/apply, periodic atom
equivalence, triclinic reciprocal geometry and repeated positive/negative
rotation on all three axes in both rotation modes.

See [the September 2026 audit](docs/AUDIT-2026-09-24.md) for finding status,
evidence, reproduction commands and the scope of verification. The follow-up
fixes resolve the matrix memory defects, crystal name/rotation state and atom
editor defects. The [September 27 follow-up](docs/AUDIT-2026-09-27-FIXES.md)
records the subsequent persistence, worker, numerical and image-state fixes.
The [September 28 follow-up](docs/AUDIT-2026-09-28-FIXES.md) records fixes for
all ten findings from the subsequent audit, including calibration cancellation,
fixed beam position during scale changes, hidden-image printing and settings round trips.
Those changes are included in the October 26 installers linked above.
The [October 26 fix report](docs/FIXES-2026-10-26-APP-FUNCTIONS.md) records the
subsequent seven app-function fixes: analysis invalidation, camera controls,
orientation reset, dock visibility and print settings/output.

## Solve the Crystal Orientation

The October 26 installers include the indexing changes described in this section.

**Laue > Solve Orientation** indexes manually selected reflections using the
known lattice and detector geometry. Load the lattice (for example using
**Set Lattice > Import CIF**) and image, then set the beam center, image scale,
sample-to-detector distance and wavelength range before starting.

1. Click at least two distinct, separated reflections. Additional spots help
   distinguish ambiguous candidates. Up to 100 spots can be selected.
2. Press **Enter** to search; **Backspace/Delete** removes the last selection.
   **Esc** also finishes selection and opens the search parameters, as before.
3. Set maximum absolute **H, K, L** (0–20 per axis), angular tolerance in degrees
   and spot tolerance in millimeters. The index limits apply to **all matched
   spots**, including those used to check each candidate. Larger ranges take
   longer. The search does not depend on the display's Accuracy setting.
4. Review the **Indexing Display**. Candidates are ranked by the number of
   matched observations (**Hits**), then the root-mean-square position error
   of those matches (**RMS (mm)**). Select a row to apply its orientation.
   A completed search does not automatically change the crystal orientation.
   **Resolve Orientation** searches again using the existing selections.

The first two observations define each candidate; the remaining observations
check it. Distances use a circular tolerance, and nearest unused predictions
are assigned to observations so that one calculated spot cannot produce multiple
hits. If there are more than 1000 candidates, only the best 1000 are retained
and the film reports this limit. Cancel stops the worker and retains the
previous results and crystal orientation.

Replacing or transposing the image, changing its beam center or scale, or using
Reset clears the selected reflections and orientation candidates. Select the
reflections again with **Solve Orientation**. Changing the detector distance or
wavelength range clears the candidate scores while keeping the selected points;
use **Resolve Orientation** to search with the new settings. Image brightness,
contrast, inversion and visibility changes keep the selections.

This is a position-based, semi-automatic search. It checks whether a fundamental
reflection or one of its harmonics lies in the wavelength range. It does not
filter candidates using the legacy intensity model; systematic absences and
experimental intensities still need review. Symmetry-equivalent orientations
can appear separately. Automatic spot detection, least-squares refinement of
orientation/geometry and refinement of atomic coordinates are not included.

Run the indexing regression check with Qt 5:

```sh
mkdir -p build-indexing-check
cd build-indexing-check
"$QT_BIN/qmake" ../tests/indexing.pro
make -j4
QT_QPA_PLATFORM=offscreen ./indexing-check
```

It covers known cubic, hexagonal and triclinic orientations, noisy positions,
an unmatched observation, unique matches, wavelength harmonics, invalid inputs,
bounded candidate storage, cancellation/restart and the Qt selection/result flow.
The Windows installer recipe runs this check with the native Windows platform.

## Print or Save the Analysis as PDF

With **Laue > Show Image** enabled, use **File > Print** to print the imported
image and calculated spots together with crystal and orientation parameters.
On macOS, choose **PDF > Save as PDF** in the print dialog. **Show Labels**
controls the hkl labels. The r3 build fixes the image/spot displacement caused
by using the screen's origin when drawing on a different-sized printed page.
Printing now uses the page's plot area and restores the screen's image geometry
afterwards.

To check image/spot alignment in portrait and landscape at 72 and 300 dpi:

```sh
mkdir -p build-print-check
cd build-print-check
"$QT_BIN/qmake" ../tests/print_alignment.pro
make -j4
QT_QPA_PLATFORM=offscreen ./print-alignment-check ./output
```

The check creates synthetic marker images, screen references, print rasters and
PDFs. It verifies that the calculated spot overlaps the imported marker and
that printing preserves the screen's image scale. Generated PDFs should also
be rendered and visually checked, for example with Poppler's `pdftoppm`.

## Save and Reopen an Analysis

**File > Save / Save As** writes an XTL analysis containing the crystal, source
atom coordinates, orientation, detector/wavelength settings, calculation accuracy
and intensity corrections, and the imported image with its scale, origin and
image adjustments. Brightness, contrast, minimum/maximum, inversion and image
rotation are restored on reopening. New optional attributes preserve compatibility
with older XTL files; missing settings use defined defaults.

New, Open, recent files, CIF/CEL structure imports and closing the window offer
**Save / Discard / Cancel** when the analysis has changed. Failed saves or imports
leave the current analysis in place. Saving uses an atomic file replacement,
and an invalid analysis cannot overwrite a valid file. New clears the previous
save destination.

The [September 27 fix report](docs/AUDIT-2026-09-27-FIXES.md) maps the audited
findings to changes, tests and remaining validation limits. Additional Qt 5 checks
are `tests/numerics.pro`, `tests/persistence.pro`, `tests/audit_fixes.pro`,
`tests/image_state.pro`, `tests/settings_regressions.pro`,
`tests/lattice_constraints.pro`, `tests/camera.pro` and `tests/app_controls.pro`;
build them like the checks above.
Their executable names are `numerics-check`, `persistence-check`,
`audit-fixes-check`, `image-state-check`, `settings-regressions-check`,
`lattice-constraints-check`, `camera-check` and `app-controls-check`. The camera check uses
a local test server, not a physical detector. The Windows packaging recipe runs
these checks before producing an installer.

**Laue > Reset Orientation** restores the current lattice's initial alignment:
100 along the beam axis and 001 in the camera-side plane. It clears goniometer
angles and indexing selections while retaining the crystal, image and calibration.

The **Camera Controls** dock and **Laue > Camera Setup** share the binning and
exposure settings. **Snap!** and **Laue > Aquire Image** use the same acquisition
path; the dock shows status and prevents a second request while an acquisition
is running. Exposure time is entered in seconds.

## Repository Contents

The C++ sources, Qt Designer `.ui` files, project configuration and image/icon
resources are kept in Git. Build directories, generated code, `.app` bundles,
new installers and macOS metadata are ignored. Previously tracked installers
in `binary/` are retained as part of the upstream history. Current Windows and macOS binaries
are published as assets in [GitHub Releases](https://github.com/hirotsugu-tagami/QLaue/releases).

## License

The source files retain the original copyright and GPL version 2 or later
notices. See [LICENSE](LICENSE) for the license text.
