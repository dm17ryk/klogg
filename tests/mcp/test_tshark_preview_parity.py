"""Portable checks of the real-engine parity comparison boundary."""

import unittest

from tshark_preview_parity import comparable
from workbench_contracts import ParityError, compare_json


class ComparisonTests(unittest.TestCase):
    def test_only_top_level_volatile_metadata_is_excluded(self):
        sample = {"id": "one", "requestId": "request", "elapsedMs": 2,
                  "enginePid": 123, "backend": "tshark_lua", "fields": [
                      {"id": "field", "attributes": {"enginePid": "semantic"}}]}
        self.assertEqual(comparable(sample), {"backend": "tshark_lua", "fields": [
            {"id": "field", "attributes": {"enginePid": "semantic"}}]})

    def test_backend_fallback_and_field_differences_fail(self):
        reference = {"backend": "tshark_lua", "fallbackReason": "", "fields": [
            {"columns": ["name", "", "", "", ""], "id": "field"}]}
        for altered in (
            dict(reference, backend="json_preview"),
            dict(reference, fallbackReason="dissector_not_dispatched"),
            dict(reference, fields=[{"columns": ["name", "lost", "", "", ""], "id": "field"}]),
        ):
            with self.subTest(altered=altered), self.assertRaises(ParityError):
                compare_json(comparable(reference), comparable(altered))


if __name__ == "__main__":
    unittest.main()
