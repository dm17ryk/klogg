"""Reserve controller ports with the same Windows ownership rules as Qt."""

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import runtime_checks


class ControllerPortsTests(unittest.TestCase):
    def test_windows_probe_rejects_shared_port_ownership(self):
        sockets = []

        class Listener:
            def __init__(self):
                self.exclusive = False
                sockets.append(self)

            def __enter__(self):
                return self

            def __exit__(self, *arguments):
                pass

            def setsockopt(self, level, option, value):
                self.exclusive = (level, option, value) == (
                    runtime_checks.socket.SOL_SOCKET, 12345, 1
                )

            def bind(self, address):
                if not self.exclusive:
                    raise OSError("Windows port requires exclusive ownership")

            def getsockname(self):
                return "127.0.0.1", 50000

        with tempfile.TemporaryDirectory() as directory, patch(
            "runtime_checks.sys.platform", "win32"
        ), patch("runtime_checks.socket.socket", Listener), patch(
            "runtime_checks.socket.SO_EXCLUSIVEADDRUSE", 12345, create=True
        ):
            arguments = runtime_checks.controller_arguments(Path(directory))
        self.assertEqual(arguments[arguments.index("--listen") + 1], "127.0.0.1:50000")
        self.assertEqual(len(sockets), 2)
        self.assertTrue(all(listener.exclusive for listener in sockets))


if __name__ == "__main__":
    unittest.main()
