from datetime import datetime, timezone
from cilogg_plugin import Job, JsonObject, Plugin

plugin = Plugin()


@plugin.action("start")
def start(job: Job, arguments: JsonObject) -> JsonObject:
    count = int(arguments.get("count", 100))
    interval = float(arguments.get("interval", 0.1))
    index = 0
    with job.stream() as stream:
        while count == 0 or index < count:
            job.check_cancelled()
            prefix = plugin.configuration.get("prefix", "DEMO")
            stream.write([f"{datetime.now(timezone.utc).isoformat()} {prefix} record={index}"])
            index += 1
            if count:
                job.progress(index / count)
            if job.cancelled.wait(interval):
                job.check_cancelled()
    return {"records": index}


if __name__ == "__main__":
    plugin.serve()
