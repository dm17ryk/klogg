"""Reserve controller ports with the same Windows ownership rules as Qt."""

import tempfile
import json
import types
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


def reply(payload, error=False):
    return types.SimpleNamespace(isError=error, content=[types.SimpleNamespace(text=json.dumps(payload))])


class ControllerRetryTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.selected = []

        def arguments(directory):
            args = ["serve", "--listen", f"127.0.0.1:{50000 + len(self.selected) * 2}"]
            self.selected.append(args)
            return args

        self.patch = patch("runtime_checks.controller_arguments", side_effect=arguments)
        self.patch.start()
        self.addCleanup(self.patch.stop)

    def session(self, outcomes):
        class Session:
            count = 0

            async def call_tool(self, name, arguments):
                if name == "cilogg_jobs":
                    return reply(outcomes[self.count - 1])
                self.count += 1
                if arguments.get("background"):
                    return reply({"job_id": str(self.count), "pid": 123})
                return reply(outcomes[self.count - 1], error=True)

        return Session()

    def failed(self, message="The bound address is already in use"):
        return {"state": "failed", "stderr": message, "stdout": ""}

    async def test_background_bind_collision_selects_a_new_pair(self):
        session = self.session([self.failed(), {"state": "running", "stdout": "Listening", "stderr": ""}])
        job, args = await runtime_checks.controller_job(session, Path("private"))
        self.assertEqual(job["job_id"], "2")
        self.assertEqual(args, self.selected[1])
        self.assertNotEqual(self.selected[0], self.selected[1])

    async def test_background_crash_is_not_retried(self):
        session = self.session([self.failed("worker crashed")])
        with self.assertRaisesRegex(AssertionError, "controller_startup_failed") as failure:
            await runtime_checks.controller_job(session, Path("private"))
        self.assertEqual(session.count, 1)
        self.assertIn('"state": "failed"', str(failure.exception))
        self.assertIn("worker crashed", str(failure.exception))

    async def test_noisy_stdout_cannot_hide_startup_error_or_break_json(self):
        state = self.failed("precise startup cause")
        state["stdout"] = "\U0001f600" * 20000
        session = self.session([state])
        with self.assertRaisesRegex(AssertionError, "controller_startup_failed") as failure:
            await runtime_checks.controller_job(session, Path("private"))
        diagnostic = str(failure.exception).split(": ", 1)[1]
        details = json.loads(diagnostic)
        self.assertEqual(details["stderr"], "precise startup cause")
        self.assertTrue(details["startup_output_truncated"])
        self.assertLessEqual(len(diagnostic.encode("ascii")), 16384)
        self.assertEqual(session.count, 1)

    async def test_background_collision_retries_are_bounded(self):
        session = self.session([self.failed()] * 3)
        with self.assertRaisesRegex(AssertionError, "controller_bind_retries_exhausted"):
            await runtime_checks.controller_job(session, Path("private"))
        self.assertEqual(session.count, 3)

    async def test_foreground_timeout_retries_bind_collision(self):
        session = self.session(["CLI exit 1: The address is protected", "CLI timed out"])
        await runtime_checks.controller_timeout(session, Path("private"))
        self.assertEqual(session.count, 2)
        self.assertNotEqual(*self.selected)

    async def test_foreground_crash_is_not_a_timeout(self):
        session = self.session(["CLI exit 1: worker crashed"])
        with self.assertRaisesRegex(AssertionError, "controller_timeout_failed"):
            await runtime_checks.controller_timeout(session, Path("private"))
        self.assertEqual(session.count, 1)

    def cancel_attempts(self, first_failure, repeat=False):
        replies = []
        calls = []

        def send(message):
            if message["method"] != "tools/call":
                return
            calls.append(message)
            ident = message["id"]
            failed = len(calls) == 1 or repeat
            outcome = {"id": ident, "result": {"isError": True, "content": [
                {"text": first_failure if failed else "CLI cancelled"}
            ]}}
            # Exercise both exit-before-ping and exit-after-ping orderings.
            replies.extend([outcome, {"id": ident + 1}] if failed else [{"id": ident + 1}, outcome])

        return send, lambda: replies.pop(0), calls

    def test_cancellation_retries_early_bind_failure(self):
        send, receive, calls = self.cancel_attempts("CLI exit 1: Address already in use")
        runtime_checks.cancel_controller(send, receive, Path("private"))
        self.assertEqual(len(calls), 2)
        self.assertNotEqual(self.selected[0], self.selected[1])

    def test_cancellation_does_not_hide_a_crash(self):
        send, receive, calls = self.cancel_attempts("worker crashed")
        with self.assertRaisesRegex(AssertionError, "controller_cancellation_failed"):
            runtime_checks.cancel_controller(send, receive, Path("private"))
        self.assertEqual(len(calls), 1)

    def test_cancellation_collision_retries_are_bounded(self):
        send, receive, calls = self.cancel_attempts("Address already in use", repeat=True)
        with self.assertRaisesRegex(AssertionError, "controller_bind_retries_exhausted"):
            runtime_checks.cancel_controller(send, receive, Path("private"))
        self.assertEqual(len(calls), 3)


if __name__ == "__main__":
    unittest.main()
