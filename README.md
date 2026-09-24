# QLaue

QLaue is a Qt desktop application for orienting single-crystal samples using the
Laue method. The original project is
[stuwilkins/QLaue](https://github.com/stuwilkins/QLaue).

This source tree includes changes to build the application with Qt 5 on macOS.

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
```

Deployment, code signing and runtime testing are separate steps. This repository
does not contain a notarized release or claim that the generated app is ready
for public distribution.

## Repository Contents

The C++ sources, Qt Designer `.ui` files, project configuration and image/icon
resources are kept in Git. Build directories, generated code, `.app` bundles,
new `.dmg` files and macOS metadata are ignored. Previously tracked installers
in `binary/` are retained as part of the upstream history. New release binaries
can be attached to GitHub Releases separately from source commits.

## License

The source files retain the original copyright and GPL version 2 or later
notices. See [LICENSE](LICENSE) for the license text.
