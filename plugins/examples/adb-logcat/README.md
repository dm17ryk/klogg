# ADB Logcat

**ADB Logcat** is preinstalled and enabled by default. Configure `adb_path`
with the Android SDK platform-tools executable, or leave it empty for discovery
through `ANDROID_HOME`, `ANDROID_SDK_ROOT`, common SDK locations and PATH.
**List ADB Devices** shows serial numbers, connection state and device details.
Authorize USB debugging on the device before capturing.

The device panel has a capture button for each ready device and a refresh button.
Start several devices to keep independent captures in separate tabs and jobs.

**Start ADB Logcat** opens a live capture tab. Select a serial when several devices
are connected, choose a buffer and format, and optionally enter space-separated
filters such as `MyTag:D *:S`. An empty serial uses the configured default or ADB's
device selection. Existing device logs are included; this plugin never clears them.
Cancel the job or close its capture tab to terminate that job's logcat process.
Disconnects and command failures appear in the plugin job details; start a new job
after reconnecting. The ADB server is left running.

CLI/MCP use plugin ID `org.cilogg.adb-logcat` with actions `devices` and `start`.
For example, invoke `start` with `{"serial":"emulator-5554","buffer":"all",
"format":"threadtime","filters":"*:I"}`. Requires Python 3.9+ and platform-tools.

ADB command reference: https://developer.android.com/tools/adb
Logcat reference: https://developer.android.com/tools/logcat
Behavior reference: https://github.com/64x-lunicorn/LogSquirl-Logcat
