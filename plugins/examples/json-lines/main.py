import json
import pathlib
from cilogg_plugin import Job, JsonObject, Plugin

plugin = Plugin()


@plugin.action("convert")
def convert(job: Job, arguments: JsonObject) -> JsonObject:
    source = pathlib.Path(arguments["input_path"])
    total = max(source.stat().st_size, 1)
    consumed = 0
    records = 0
    with source.open("r", encoding="utf-8") as incoming, open(arguments["output_path"], "w", encoding="utf-8") as outgoing:
        for line in incoming:
            job.check_cancelled()
            consumed += len(line.encode("utf-8"))
            if not line.strip():
                continue
            value = json.loads(line)
            text = " | ".join(f"{key}={item}" for key, item in value.items()) if isinstance(value, dict) else json.dumps(value, ensure_ascii=False)
            outgoing.write(text + "\n")
            records += 1
            if records % 1000 == 0:
                job.progress(min(consumed / total, 1.0))
    job.progress(1.0)
    return {"records": records, "output_path": arguments["output_path"]}


if __name__ == "__main__":
    plugin.serve()
