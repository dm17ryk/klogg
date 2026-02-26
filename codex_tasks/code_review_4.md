Please address the comments from this code review:

## Overall Comments
- The logic for ensuring/creating the capture file now exists both in `MainWindow::startComCaptureSession` and `ensureCaptureFileExists`; consider centralizing this into a single helper to avoid duplication and keep behavior consistent between interactive and restore paths.
- The `startComCaptureSession` signature with two `bool` flags (`allowActionsPrompt`, `showErrors`) makes call sites harder to read; using a small options struct or enum flags would improve readability and reduce the chance of inverted arguments.

## Individual Comments

### Comment 1
<location path="src/ui/src/serialcaptureworker.cpp" line_range="71-80" />
<code_context>
+    settings.baudRate = object.value( QStringLiteral( "baudRate" ) ).toInt( settings.baudRate );
</code_context>
<issue_to_address>
**issue (bug_risk):** Avoid using uninitialized SerialCaptureSettings fields as defaults when deserializing.

In `deserializeSerialCaptureSettings`, `settings` is default-constructed and its members (e.g. `baudRate`, `dataBits`, `parity`) are then used as default values in `toInt(...)`. Because `SerialCaptureSettings` does not give these members explicit default initializers, their values are indeterminate here, which is undefined behavior. Either give all numeric/enum members explicit in-class defaults in `SerialCaptureSettings`, or pass concrete defaults directly to `toInt` (e.g. `toInt(115200)` or `toInt(static_cast<int>(QSerialPort::Data8))`) instead of reading from `settings`.
</issue_to_address>