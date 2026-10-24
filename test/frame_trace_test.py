import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    "frame_trace", Path(__file__).resolve().parents[1] / "tools/read_frame_trace.py")
trace = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(trace)


class TraceTest(unittest.TestCase):
    def fixture(self, statuses=(0, -2005532132, 0), final_copy=0):
        header = struct.pack("<6IQ", 0x52544D44, 2, 184, len(statuses), 123, 1, 1000000)
        rows = []
        for index, hr in enumerate(statuses, 1):
            entry = 1000 + (index - 1) * 15625
            ticks = [entry + n * 10 for n in range(14)]
            values = [index, 9 + index, 1, 0, 1, index, index if hr == 0 and final_copy == 0 else 0, 1, 10, 20]
            rows.append(struct.pack("<10I7i4x14q", *values, 0, 0, 0, 0, 0, hr, final_copy, *ticks))
        return header + b"".join(rows)

    def read(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.bin"
            path.write_bytes(data)
            return trace.read_trace(path)

    def test_failed_present_is_not_a_submission_or_display(self):
        header, rows = self.read(self.fixture())
        stats = trace.summarize(header, rows)
        self.assertEqual(stats["present_attempts"], 3)
        self.assertEqual(stats["successful_submissions"], 2)
        self.assertEqual(stats["same_iteration_submissions"], 2)
        self.assertEqual(stats["native_completed_updates"], 2)
        self.assertEqual(stats["native_updates_per_second"], 64)
        self.assertIsNone(stats["displayed_images"])
        self.assertIsNone(stats["driver_queue_age_ms"])
        self.assertEqual(stats["timings_ms"]["upload_lock"]["p99"], .01)

    def test_positive_status_is_not_an_image_submission(self):
        header, rows = self.read(self.fixture((0x0876086C,)))
        self.assertEqual(trace.summarize(header, rows)["successful_submissions"], 0)

    def test_failed_final_copy_does_not_identify_submitted_image(self):
        header, rows = self.read(self.fixture((0,), final_copy=-1))
        stats = trace.summarize(header, rows)
        self.assertEqual(stats["successful_submissions"], 1)
        self.assertEqual(stats["same_iteration_submissions"], 0)

    def test_old_trace_cannot_establish_final_image_identity(self):
        data = self.fixture((0,))
        values = struct.unpack_from("<10I7i4x14q", data, 32)
        old = struct.pack("<6IQ", 0x52544D44, 1, 176, 1, 123, 1, 1000000)
        old += struct.pack("<10I6i14q", *values[:16], *values[17:])
        header, rows = self.read(old)
        stats = trace.summarize(header, rows)
        self.assertEqual(stats["successful_submissions"], 1)
        self.assertEqual(stats["unidentified_submissions"], 1)

    def test_incomplete_or_wrong_schema_is_rejected(self):
        data = self.fixture()
        for bad in (data[:-1], b"", b"XXXX" + data[4:], data[:8] + struct.pack("<I", 175) + data[12:]):
            with self.subTest(size=len(bad)):
                with self.assertRaises(ValueError):
                    self.read(bad)

    def test_missing_native_witness_stays_unknown(self):
        data = bytearray(self.fixture())
        struct.pack_into("<I", data, 20, 0)
        header, rows = self.read(data)
        self.assertIsNone(trace.summarize(header, rows)["native_updates_per_second"])


if __name__ == "__main__":
    unittest.main()
