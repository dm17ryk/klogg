"""Tk owns the main thread; the protocol stays responsive on its reader thread."""
import queue
import threading
import tkinter as tk
from cilogg_plugin import Job, JsonObject, Plugin

plugin = Plugin()
commands = queue.Queue(maxsize=128)
root = tk.Tk()
root.title("CILogg Plugin Window")
root.withdraw()
tk.Label(root, text="This window belongs to a separate plugin process.").pack(padx=30, pady=30)


@plugin.action("show")
def show(job: Job, _arguments: JsonObject) -> JsonObject:
    job.check_cancelled()
    commands.put_nowait("show")
    return {"shown": True}


def pump() -> None:
    if plugin.stopping.is_set():
        root.destroy()
        return
    while not commands.empty():
        commands.get_nowait()
        root.deiconify()
        root.lift()
    root.after(100, pump)


root.protocol("WM_DELETE_WINDOW", root.withdraw)
reader = threading.Thread(target=plugin.serve, name="plugin-protocol")
reader.start()
root.after(100, pump)
root.mainloop()
reader.join(timeout=3)
