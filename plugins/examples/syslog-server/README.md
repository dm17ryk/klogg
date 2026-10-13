# Syslog Server

This preinstalled plugin provides the UDP syslog logging feature of Tftpd64.
It is enabled by default; select **Plugins → Start Syslog Server** to begin capture.
Defaults are `127.0.0.1:514` (local traffic only). To receive remote device logs,
explicitly select a LAN interface address, `0.0.0.0` for all IPv4 interfaces, or
`::` for IPv6, and configure **Allowed sender IPs/CIDRs** before starting.
UDP syslog has no sender authentication; an empty allowlist accepts every reachable
sender. Use another UDP port such as `5514` when needed.
Point devices at this computer's address and the selected syslog port; allow that
UDP port through the firewall. Port 514 can require privileges on Unix systems.

Logs open in a live CILogg tab. Each datagram is one record. Annotated output adds
the UTC reception time, sender IP/port, facility, severity and detected format,
followed by the complete received message including structured data. RFC 3164
legacy and RFC 5424 headers are recognized; plain/malformed messages are retained
with unknown metadata. Raw output omits annotations. Embedded control characters
are rendered as `\xNN` so multiline datagrams remain a single capture record;
trailing CR/LF/NUL framing is removed.

Configuration selects UTF-8, Latin-1 or Windows-1252, maximum severity (0 emergency
through 7 debug), and optional sender IP/CIDR restrictions separated by commas or
whitespace. Plain messages remain accepted when severity filtering is enabled.
Capture limits apply through the host's normal plugin stream. Batches are bounded;
UDP can drop messages under sustained overload because it has no acknowledgements.
Cancel the job, close its capture tab, or disable the plugin to release the port.
Job cancellation finishes the already-received batch while its capture remains open;
credit/acknowledgement waits during cancellation are limited to one second.

CLI/MCP plugin ID: `org.cilogg.syslog-server`, action: `start`.
Local arguments example: `{"bind":"127.0.0.1","port":5514,"count":0}`.
Use `cancel_plugin_job` to stop the server. Requires Python 3.9+, no extra packages.

Reference: https://github.com/PJO2/tftpd64
Protocols: https://www.rfc-editor.org/rfc/rfc3164 and
https://www.rfc-editor.org/rfc/rfc5424.
