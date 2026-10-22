# Qt versions and desktop compatibility

The CI matrix targets configurations listed in Qt 6.12's supported-platform
table. RHEL-compatible packages use AlmaLinux for public CI. Ubuntu 26.04 remains
deferred.

| Target | Qt | Minimum / compatibility choice |
| --- | --- | --- |
| Windows x64 and ARM64 | 6.12.0 | Existing MSVC 2022 packages |
| Ubuntu 22.04 x64 DEB | 6.12.0 | Jammy, GCC 11 |
| Ubuntu 24.04 x64 and ARM64 DEB | 6.12.0 | Noble, GCC 13 |
| Debian 12 x64 DEB | 6.12.0 | Bookworm, GCC 12 |
| RHEL-compatible 9.6 / 10.0 x64 RPM | 6.12.0 | AlmaLinux 9.6 / 10.0, GCC 11 / 14, Qt-only search backend |
| openSUSE Leap 16.0 x64 RPM | 6.12.0 | GCC 15; manual package installation |
| AppImage x64 / ARM64 | 6.12.0 | Existing Jammy / Noble build baselines |
| Modern macOS Intel and Apple Silicon | 6.12.0 | macOS 14.4+, built on macOS 26 with Xcode 16+ |
| Compatibility macOS Intel / Apple Silicon | 6.10.1 | macOS 13 / 14.0 respectively |
| Older-named Ubuntu packages and Fedora targets | Existing versions | Compatibility builds; no claim of Qt 6.12 vendor support |

Qt's prebuilt ARM64 SDK requires glibc 2.39, so ARM64 AppImages retain their
Ubuntu 24.04 minimum. ARM desktop support is qualified against Qt's Raspberry
Pi 5 reference hardware. Adding other supported Linux distributions is separate
work; platform support alone does not guarantee matching distribution packages.
Debian ARM64 is not added: the vendor SDK's glibc 2.39 requirement exceeds
Debian 12's glibc 2.36. Supporting it requires a Qt source build. RHEL-compatible
packages use AlmaLinux's public BaseOS, AppStream and CRB repositories without
subscription credentials. They are built and clean-install tested on AlmaLinux,
not native RHEL, and do not imply Qt vendor certification for AlmaLinux or Rocky
Linux. RPM filenames end in `-rhel9-compatible.rpm` or `-rhel10-compatible.rpm`;
the updater maps them to the exact RHEL 9.6 / 10.0 versions. Installation on
AlmaLinux or Rocky Linux is manual because updater distro aliases are not added.
openSUSE packages
require manual installation because the current updater implements `dnf`, not
`zypper`; its unsupported-distribution guard remains in effect.

## Native Linux deployment

Supported native DEBs and the new RPMs enable `KLOGG_BUNDLE_QT_RUNTIME`. Qt's deployment API
installs the vendor runtime and selected plugins under a private CILogg library
directory. Executables, updater/search helpers, preinstalled plugins and plugin
SDK files move together. Public `cilogg` and `cilogg_grep` launchers retain
their normal names and execute the canonical private binary path. This matters
for Qt 6.12, which preserves the original execution path rather than resolving
the public symlink when locating `qt.conf`. The private configuration and relative RPATH avoid changing
other applications' Qt discovery or depending on a build-machine `/opt/Qt` path.

Each upgraded DEB job installs the actual package and its system dependencies in
a clean matching distribution container and runs `cilogg --help` offscreen. New
RHEL-compatible RPMs receive the same check in clean AlmaLinux containers. Release
validation accepts the older layout, command symlinks and launchers, and rejects a
resolved executable outside the extracted package. CTest and AppImage runtime
checks remain part of their respective build jobs. Qt version, compiler, CMake,
selected SDK architecture and deployment decisions appear in CI logs.

Qt versions are selected per matrix row and passed as a Docker build argument.
Shared Ubuntu images default to 6.10.1 so older compatibility targets do not
change implicitly. The installer is pinned to aqtinstall 3.3.0.

## Modern macOS packages

The additional DMGs end in `-mac-x64-qt612.dmg` or `-mac-arm64-qt612.dmg`.
They require macOS 14.4+. The existing DMGs and update ZIPs retain their names
and earlier deployment targets.

Modern Qt 6.12 builds are **manual-install packages**. They do not produce an
automatic-update ZIP, since older installed updater clients do not understand
a package's minimum macOS version. Automatic updates, including updates from a
manually installed modern build, use the compatible Qt 6.10.1 bundle. Install a
subsequent `-qt612.dmg` manually to retain Qt 6.12. A modern updater channel needs
a separately deployed compatibility-aware updater contract.

References: [Qt 6.12 supported platforms](https://doc.qt.io/qt-6.12/supported-platforms.html),
[Qt Linux deployment](https://doc.qt.io/qt-6.12/linux-deployment.html),
[AlmaLinux repositories](https://wiki.almalinux.org/repos/AlmaLinux.html),
[GitHub runner images](https://github.com/actions/runner-images).
