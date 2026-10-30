"""Worker memory is exact, optional, and never silently converted to zero."""

import unittest

from worker_memory import validate_memory


def observation(**fields):
    return dict(
        status="measured",
        backend="windows_peak_working_set",
        unit="bytes",
        scope="largest_single_worker_high_water_through_observation",
        peakRssBytes="9007199254741009",
        measuredObservations="3",
        missingObservations="0",
        lastObservation="response",
        lastNativeError=None,
        **fields,
    )


class WorkerMemoryTests(unittest.TestCase):
    def test_exact_decimal_is_preserved(self):
        memory = observation()
        self.assertEqual(validate_memory(memory, True), 9007199254741009)

    def test_zero_numeric_or_missing_measurement_is_rejected_on_windows(self):
        for value in (None, "0", 42, "-1", "1e3", "01", "1" * 21):
            with self.subTest(value=value), self.assertRaises(ValueError):
                memory = observation()
                memory["peakRssBytes"] = value
                validate_memory(memory, True)
        with self.assertRaises(ValueError):
            validate_memory({}, True)

    def test_unsupported_is_explicit_and_not_measured(self):
        memory = observation()
        memory.update(status="unsupported", backend="none", peakRssBytes=None,
                      measuredObservations="0", missingObservations="3")
        self.assertIsNone(validate_memory(memory, False))
        with self.assertRaises(ValueError):
            validate_memory(memory, True)
        memory["peakRssBytes"] = "0"
        with self.assertRaises(ValueError):
            validate_memory(memory, False)

    def test_missing_samples_and_wrong_units_do_not_pass_gate(self):
        for fields in ({"status": "partial"}, {"missingObservations": "1"},
                       {"measuredObservations": "0"}, {"unit": "KiB"},
                       {"lastNativeError": "5"}, {"lastObservation": "none"}):
            with self.subTest(fields=fields), self.assertRaises(ValueError):
                memory = observation()
                memory.update(fields)
                validate_memory(memory, True)


if __name__ == "__main__":
    unittest.main()
