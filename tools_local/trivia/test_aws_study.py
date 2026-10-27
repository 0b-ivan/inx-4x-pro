#!/usr/bin/env python3
"""Synthetic-only tests: private AWS question text never enters GitHub."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

script = Path(__file__).with_name("build_aws_study.py")
spec = importlib.util.spec_from_file_location("aws_study_builder", script)
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def sample(q="Which AWS service provides object storage?", correct=None, category=1):
    return {
        "type": "single" if correct is None else "multi",
        "q": q,
        "options": ["Amazon S3", "Amazon EBS", "Amazon EFS", "Amazon FSx"],
        "correct": [0] if correct is None else correct,
        "explanation": "S3 is object storage; image [IMAGE] referenced.",
        "category": f"Domain {category}: Example test domain",
        "explanation_html": "<script>never put this in binary</script>",
        "user_email": "should never leave the input",
    }


class TestStudyBuilder(unittest.TestCase):
    def test_roundtrip_and_private_no_html(self):
        with tempfile.TemporaryDirectory() as directory:
            src = Path(directory) / "study.jsonl"
            dst = Path(directory) / "DOP-C02.dat"
            rows = [sample(), sample("Which two qualify?", [0, 2], 6)]
            src.write_text("".join(json.dumps(r) + "\n" for r in rows), encoding="utf-8")
            n, size, largest = builder.build(src, dst)
            data = dst.read_bytes()
            topics = dst.with_suffix(".topics").read_text(encoding="ascii").splitlines()
            self.assertEqual(len(topics), 6)
            self.assertEqual(topics[0], "1 Example test domain")
            self.assertEqual(topics[5], "6 Example test domain")
            self.assertEqual((n, size), (2, len(data)))
            self.assertLessEqual(largest, builder.MAX_RECORD)
            self.assertEqual(struct.unpack("<8sHBBI", data[:16]),
                             (builder.MAGIC, 1, 0, 0, 2))
            self.assertEqual(len(data[16:20]), 4)  # content ID
            offsets = struct.unpack("<3I", data[20:32])
            self.assertEqual(offsets[0], 0)
            self.assertLess(offsets[1], offsets[2])
            self.assertEqual(len(data), 32 + offsets[-1])
            first = data[32:32+offsets[1]]
            second = data[32+offsets[1]:]
            self.assertEqual(first[:4], bytes([1, 4, 1, 0]))
            self.assertEqual(second[:4], bytes([6, 4, 5, 0]))
            self.assertIn(b"Which AWS service", first)
            self.assertNotIn(b"never put this", data)
            self.assertNotIn(b"should never", data)
            self.assertIn(b"[IMAGE]", data)

    def test_generic_topic_titles_are_accepted(self):
        row = sample()
        row["category"] = "Topic 1: Linux services"
        encoded = builder.encode(row, 1)
        self.assertEqual(encoded[0], 1)
        row["category"] = "Category 2: Network protocols"
        self.assertEqual(builder.encode(row, 1)[0], 2)

    def test_conflicting_topics_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            src = Path(directory) / "study.jsonl"
            rows = [sample(), sample("Second synthetic question")]
            rows[1]["category"] = "Domain 1: Different label"
            src.write_text("".join(json.dumps(row) + "\n" for row in rows),
                           encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "conflicting"):
                builder.build(src)

    def test_bad_answer_and_type_rejected(self):
        for row in (
            {**sample(), "correct": [0, 0]},
            {**sample(), "correct": [4]},
            {**sample(), "correct": [0, 1]},
            {**sample(), "category": "None"},
            {**sample(), "options": ["Same", "same"]},
            {**sample(), "q": ""},
            {**sample(), "q": "embedded\0zero"},
        ):
            with self.subTest(row=row):
                with self.assertRaises(ValueError):
                    builder.encode(row, 1)

    def test_large_record_rejected(self):
        with self.assertRaisesRegex(ValueError, "record is"):
            builder.encode({**sample(), "explanation": "X" * builder.MAX_RECORD}, 1)

    def test_duplicates_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            src = Path(directory) / "study.jsonl"
            src.write_text((json.dumps(sample()) + "\n") * 2)
            with self.assertRaisesRegex(ValueError, "duplicate"):
                builder.build(src)

    def test_nonprivate_output_rejected(self):
        root = script.resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            src = Path(directory) / "study.jsonl"
            src.write_text(json.dumps(sample()) + "\n")
            with self.assertRaisesRegex(ValueError, "outside"):
                builder.build(src, root / "DOP-C02.dat")


if __name__ == "__main__":
    unittest.main()
