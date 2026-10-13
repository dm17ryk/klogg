"""UDP syslog capture (Tftpd64-style), with legacy and RFC 5424 metadata."""
from __future__ import annotations

import ipaddress
import re
import select
import socket
from datetime import datetime, timezone

from cilogg_plugin import Cancelled, Job, JsonObject, Plugin

plugin = Plugin()
SEVERITIES = ("EMERG", "ALERT", "CRIT", "ERROR", "WARN", "NOTICE", "INFO", "DEBUG")
FACILITIES = ("kern", "user", "mail", "daemon", "auth", "syslog", "lpr", "news",
              "uucp", "cron", "authpriv", "ftp", "ntp", "audit", "alert", "clock",
              "local0", "local1", "local2", "local3", "local4", "local5", "local6", "local7")


def debug(message: str) -> None:
    plugin.notify("log", {"message": "Syslog: " + message})


def metadata(text: str) -> JsonObject:
    result: JsonObject = {"format": "plain", "facility": "unknown", "severity": "unknown"}
    priority = re.match(r"^<([0-9]{1,3})>", text)
    if priority is None or int(priority[1]) > 191:
        return result
    value = int(priority[1])
    result.update(priority=value, facility=FACILITIES[value // 8],
                  severity=SEVERITIES[value % 8], severity_number=value % 8)
    body = text[priority.end():]
    modern = re.match(r"^([1-9][0-9]{0,2}) (\S+) (\S+) (\S+) (\S+) (\S+) (.*)$", body,
                      flags=re.DOTALL)
    if modern:
        result.update(format="rfc5424", version=modern[1], timestamp=modern[2],
                      hostname=modern[3], app=modern[4], procid=modern[5], msgid=modern[6])
    else:
        legacy = re.match(r"^([A-Z][a-z]{2} [ 0-9][0-9] [0-9]{2}:[0-9]{2}:[0-9]{2}) (\S+) ", body)
        if legacy:
            result.update(format="rfc3164", timestamp=legacy[1], hostname=legacy[2])
    return result


def printable(text: str) -> str:
    # One datagram stays one capture record even when it contains embedded newlines.
    return "".join(f"\\x{ord(char):02x}" if ord(char) < 32 or ord(char) == 127
                   else char for char in text)


@plugin.action("start")
def start(job: Job, arguments: JsonObject) -> JsonObject:
    bind = str(arguments.get("bind", "0.0.0.0")).strip()
    try:
        address = ipaddress.ip_address(bind)
    except ValueError as error:
        raise ValueError("Bind address must be a numeric IPv4 or IPv6 address") from error
    port = int(arguments.get("port", 514))
    count = int(arguments.get("count", 0))
    if not 1 <= port <= 65535 or not 0 <= count <= 1000000:
        raise ValueError("Invalid syslog port or message count")
    config = dict(plugin.configuration)
    encoding = str(config.get("encoding", "utf-8"))
    output_format = config.get("output", "annotated")
    max_severity = int(config.get("max_severity", 7))
    if encoding not in {"utf-8", "latin-1", "cp1252"} or output_format not in {"annotated", "raw"}:
        raise ValueError("Unsupported encoding or output format")
    if not 0 <= max_severity <= 7:
        raise ValueError("Maximum severity must be between 0 and 7")
    allowed = [ipaddress.ip_network(item, strict=False) for item in
               re.split(r"[,\s]+", str(config.get("allowed_senders", "")).strip()) if item]
    debug(f"Configuration bind={address} port={port} count={count} encoding={encoding} "
          f"output={output_format} max_severity={max_severity} sender_networks={len(allowed)}")
    family = socket.AF_INET6 if address.version == 6 else socket.AF_INET
    accepted = 0
    filtered = 0
    total_bytes = 0
    with socket.socket(family, socket.SOCK_DGRAM) as sock:
        if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        if family == socket.AF_INET6:
            sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024 * 1024)
        try:
            sock.bind((bind, port))
        except OSError as error:
            debug(f"Bind failed error={error}")
            raise RuntimeError(f"Cannot listen on {bind}:{port}: {error}") from error
        sock.setblocking(False)
        with job.stream() as stream:
            job.panel("syslog-status", "Syslog Server", [{"type": "text",
                      "text": f"Listening for UDP syslog on {bind}:{port}"}])
            debug("Socket and capture ready")
            try:
                while count == 0 or accepted < count:
                    job.check_cancelled()
                    if stream.closed.is_set():
                        raise Cancelled("Syslog capture tab closed")
                    ready, _, _ = select.select([sock], [], [], 0.1)
                    if not ready:
                        continue
                    batch = []
                    batch_bytes = 0
                    # Drain bursts into bounded batches; no unbounded producer queue.
                    for _ in range(32):
                        job.check_cancelled()
                        try:
                            payload, peer = sock.recvfrom(65535)
                        except BlockingIOError:
                            break
                        sender = ipaddress.ip_address(peer[0])
                        if allowed and not any(sender in network for network in allowed):
                            filtered += 1
                            if filtered == 1 or filtered % 100 == 0:
                                debug(f"Sender rejected count={filtered} address={sender}")
                            continue
                        text = payload.decode(encoding, errors="replace").rstrip("\r\n\0")
                        details = metadata(text)
                        if details.get("severity_number", 0) > max_severity:
                            filtered += 1
                            if filtered == 1 or filtered % 100 == 0:
                                debug(f"Severity rejected count={filtered} level={details['severity']}")
                            continue
                        raw = printable(text)
                        if output_format == "annotated":
                            received = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
                            endpoint = f"[{sender}]:{peer[1]}" if sender.version == 6 else f"{sender}:{peer[1]}"
                            raw = (f"{received} sender={endpoint} facility={details['facility']} "
                                   f"severity={details['severity']} format={details['format']} {raw}")
                        batch.append(raw)
                        accepted += 1
                        total_bytes += len(payload)
                        batch_bytes += len(payload)
                        if (count and accepted >= count) or batch_bytes >= 16384:
                            break
                    if batch:
                        stream.write(batch)
                        if accepted == len(batch) or accepted // 100 != (accepted - len(batch)) // 100:
                            debug(f"Capture checkpoint messages={accepted} bytes={total_bytes} filtered={filtered}")
                            job.panel("syslog-status", "Syslog Server", [{"type": "text",
                                      "text": f"Listening on {bind}:{port}. Messages: {accepted}; filtered: {filtered}."}])
            finally:
                debug(f"Closing UDP listener messages={accepted} bytes={total_bytes} filtered={filtered}")
    return {"messages": accepted, "bytes": total_bytes, "filtered": filtered}


if __name__ == "__main__":
    plugin.serve()
