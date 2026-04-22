import os
import runpy
import sys
import traceback


def main() -> int:
    script_file = os.environ.get("CILOGG_SCRIPT_FILE", "")
    if not script_file:
        sys.stderr.write("Missing CILOGG_SCRIPT_FILE\n")
        return 2

    try:
        runpy.run_path(script_file, run_name="__main__")
        return 0
    except SystemExit as exc:
        code = exc.code if isinstance(exc.code, int) else 0
        return code
    except BaseException:
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
