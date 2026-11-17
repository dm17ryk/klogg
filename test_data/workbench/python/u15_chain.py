"""U15 parity driver: runs several fixtures in one global script.

A host keeps one global script binding once it has run (run_global_script then refuses another
script), so the U15 cases run their Python fixtures through this one script. The arguments file
lists the steps: {"steps": [{"fixture": <path>, "args": {...}}, ...]}; each fixture sees its own
arguments through app.script_args() and writes its own output file.
"""

import runpy

from klogg import app

steps = app.script_args()["steps"]
try:
    for step in steps:
        # An instance attribute shadows Application.script_args for this step.
        app.script_args = lambda step=step: step["args"]
        runpy.run_path(step["fixture"], run_name="__main__")
finally:
    vars(app).pop("script_args", None)
