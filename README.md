# QLaue

QLaue is a Qt desktop application for orienting single-crystal samples using the
Laue method. The original project is
[stuwilkins/QLaue](https://github.com/stuwilkins/QLaue).

This source tree includes changes to build the application with Qt 5 on macOS.

## Download for macOS

[Download QLaue for Apple Silicon (ZIP)](https://github.com/hirotsugu-tagami/QLaue/releases/download/macos-arm64-2026-09-24/QLaue-macos-arm64-2026-09-24.zip)
or read the [release notes and checksums](https://github.com/hirotsugu-tagami/QLaue/releases/tag/macos-arm64-2026-09-24).

Extract the ZIP and copy `QLaue.app` to Applications. Qt and the required runtime
libraries are included; no separate Qt, Conda or Python installation is needed.
This build is for Apple Silicon (arm64) and was checked on macOS 26.6.2.

The build is a **prerelease** with outstanding findings listed in
[the audit report](docs/AUDIT-2026-09-24.md). It has an ad-hoc signature and has
not been signed with an Apple Developer ID or notarized. If macOS blocks the
first launch, verify the download source and follow
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
symmetry, triclinic reciprocal geometry and repeated positive/negative rotation
on all three axes in both rotation modes.

See [the September 2026 audit](docs/AUDIT-2026-09-24.md) for finding status,
evidence, reproduction commands and the scope of verification. The follow-up
fixes resolve the matrix memory defects, crystal name/rotation state and atom
editor defects; other persistence, threading and scientific findings remain.

## Repository Contents

The C++ sources, Qt Designer `.ui` files, project configuration and image/icon
resources are kept in Git. Build directories, generated code, `.app` bundles,
new `.dmg` files and macOS metadata are ignored. Previously tracked installers
in `binary/` are retained as part of the upstream history. Current macOS binaries
are published as assets in [GitHub Releases](https://github.com/hirotsugu-tagami/QLaue/releases).

## License

The source files retain the original copyright and GPL version 2 or later
notices. See [LICENSE](LICENSE) for the license text.
