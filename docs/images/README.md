# Documentation screenshots

All ten JPEG images were captured on 28 October 2026 from the **normal installed
Windows CILogg**, build **26.04.2.1078**, running from
`C:\Essence_SC\cilogg\cilogg.exe`. They replace the earlier portable-build captures.
The viewer uses the installation's existing dark theme, font, and highlighter sets.

| Image | Workflow |
| --- | --- |
| `workspace.jpg` | Full log and `ERROR\|WARN` search; 30 matching rows with the live COM tabs visible. |
| `regex-lab-showcase.jpg` | Sample evaluation with named `level` and `service` groups; 120 retained rows and the first result selected. |
| `value-counts-showcase.jpg` | Counts of the `level` capture: INFO 70, DEBUG 20, WARN 20, ERROR 10. |
| `actions-showcase.jpg` | Configured device actions, action-tab routing, and transmit sequences. |
| `responses-showcase.jpg` | Configured match rules and enabled automatic responses. |
| `response-editor-showcase.jpg` | The Panel Ready trigger, linked actions, delays, and snapshot option. |
| `plugins-showcase.jpg` | Installed ADB Logcat, Syslog Server, and example plugins. |
| `syslog-settings-showcase.jpg` | Syslog encoding, output format, severity, and sender restrictions. |
| `script-runner-showcase.jpg` | Script Runner's global/per-tab controls before selecting a script. |
| `highlighters-showcase.jpg` | Configured highlighter sets, patterns, and foreground/background colors. |

Log and analysis views use the synthetic
[device-session.txt](../examples/device-session.txt) fixture. Other tool windows
show the normal installation's configuration. No device commands were sent and
no scripts or listeners were started for these captures. Editors were cancelled;
the temporary sample tab was closed and the original COM tab restored afterward.

To reproduce the analysis views, follow
[Your first investigation](../../DOCUMENTATION.md#your-first-investigation).
To browse all ten images with captions, open the [screenshot gallery](../screenshots.md).

Images are native window captures without composited UI. The website uses identical
copies under `website/static/screenshots/`; update both locations together.
Existing older PNG assets remain for historical documentation references.
