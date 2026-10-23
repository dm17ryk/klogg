# macOS DMG packaging

CI deploys the application bundle and signs it when signing is configured, then
runs the packaging script from `build_root`:

```sh
python3 ../scripts/package_macos_dmg.py --version "$CILOGG_VERSION" --headless
```

Headless mode builds a compressed HFS+ image with native `hdiutil`, containing
`cilogg.app` and the `/Applications` link. It verifies the image before publishing
it under the existing package filename. The existing CI layout check then mounts
it read-only and checks its executable and link. Signing, notarization, update ZIP
creation and platform compatibility settings follow the existing workflow.

This mode avoids CPack's Finder/AppleScript window customization. On hosted Intel
macOS, Finder failed while enumerating the image, and the backing image stayed
busy even after repeated forced detach attempts. Starting with a native image
avoids creating that Finder-held mount. CI images have the simple drag-to-Applications
layout rather than the Finder-customized background and icon positions.

Without `--headless`, interactive packaging retains the CPack route and its native
recovery path. Both modes retain bounded native creation retries, fresh backing
files per attempt, verification before publication and cleanup restricted to images
backed by their build/recovery directories. A busy owned mount remains an error;
the script does not force-clean unrelated images or silently publish a bad image.

Run the focused tests with:

```sh
python3 -m unittest discover -s tests/packaging -p test_macos_dmg.py -v
```

The dedicated DMG workflow runs the real image tests on Intel and ARM macOS without
waiting for a Qt/C++ build. Windows/Linux runs cover mocked orchestration and skip
the two native disk-image tests explicitly. A native test is not proof that the
full deployed application bundle has passed packaging, signing or notarization.

Rollback removes `--headless` from the packaging action to restore the interactive
CPack strategy; existing output names and verification/signing steps remain valid.
