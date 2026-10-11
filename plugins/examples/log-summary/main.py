from cilogg_plugin import Job, JsonObject, Plugin

plugin = Plugin()


@plugin.action("summarize")
def summarize(job: Job, arguments: JsonObject) -> JsonObject:
    path = arguments.get("input_path", job.context.get("filePath"))
    if not path:
        raise ValueError("Open a log file first")
    lines = 0
    counts = {"ERROR": 0, "WARN": 0, "INFO": 0}
    with open(path, "r", encoding="utf-8", errors="replace") as log:
        for line in log:
            job.check_cancelled()
            lines += 1
            for term in counts:
                counts[term] += term in line.upper()
    job.panel("summary", "Log Summary", [
        {"type": "text", "text": f"{path}\n{lines} records"},
        {"type": "table", "columns": ["Term", "Records"], "rows": list(map(list, counts.items()))},
        {"type": "button", "label": "Refresh", "action": "summarize"},
    ])
    return {"records": lines, "counts": counts}


if __name__ == "__main__":
    plugin.serve()
