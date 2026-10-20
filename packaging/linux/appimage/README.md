# Linux AppImages

CI creates both x86_64 and AArch64 AppImages. The ARM64 job runs on a native
`ubuntu-24.04-arm` runner: type-2 AppImage magic bytes do not match QEMU's
binfmt executable rules, even when the ELF machine is correct. Do not substitute
an x86_64 deployment tool inside an ARM64 container.
See the [upstream AppImage/QEMU issue](https://github.com/AppImage/AppImageKit/issues/828).

Both jobs run linuxdeployqt and its nested appimagetool with
`APPIMAGE_EXTRACT_AND_RUN=1`, so packaging does not require FUSE or a privileged
container. Tool download, dependency deployment, image creation, or validation
failure fails the job. A skip text file is not a release package.

The ARM64 container uses Qt 6.10.1 on Ubuntu 24.04, so its runtime baseline is
**Ubuntu 24.04 / glibc 2.39 or compatible newer systems**. Its deployment explicitly
allows that baseline through linuxdeployqt's older-system guard and preserves
the libc copyright file required by that mode. It does not bundle glibc or claim
compatibility with older distributions. The x86_64 baseline is unchanged.
This matches [Qt 6.10's official ARM binary baseline](https://doc.qt.io/qt-6.10/linux.html).

Before upload, CI checks the image and executable architecture, extracts the
image, verifies Qt SQL/SQLite and offscreen plugins and preinstalled live-source
plugins, and runs the packaged application version and native MCP help.

Run orchestration regressions without Qt or downloads:

```sh
bash tests/packaging/appimage_test.sh
```

Validate a real image inside its target-architecture build container:

```sh
bash packaging/linux/appimage/validate_appimage.sh build_root/packages/cilogg-VERSION-aarch64.AppImage
```
