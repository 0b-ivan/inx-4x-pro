#!/usr/bin/env python3
"""Offline unit tests for the private Udemy JSON import boundary."""

import json
import os
from pathlib import Path
import tempfile
import unittest

import import_udemy_practice as importer


def item(question="Which service stores objects?", answers=None, correct=None):
    answers = answers or [
        "Amazon S3", "Amazon EBS", "Amazon EFS", "Amazon FSx"
    ]
    return {
        "id": 12345,  # intentionally NOT copied to output
        "prompt": {
            "question": f"<p>{question}</p>",
            "answers": answers,
            "explanation": "<p>Check the service's storage model.</p>",
        },
        "correct_response": correct or [answers[0]],
        "section": "Sample Section",
        "session_token": "DO_NOT_COPY",
        "user_email": "DO_NOT_COPY",
    }


class TestUdemyImporter(unittest.TestCase):
    def test_historical_udemy_results_shape(self):
        data = {"count": 1, "results": [item()], "next": None}
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "assessments.json"
            src.write_text(json.dumps(data), encoding="utf-8")
            rows = importer.import_files([src])
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["q"], "Which service stores objects?")
        self.assertEqual(rows[0]["correct"], [0])
        self.assertEqual(rows[0]["type"], "single")
        self.assertEqual(rows[0]["explanation"], "Check the service's storage model.")
        self.assertEqual(rows[0]["category"], "Uncategorized")
        self.assertNotIn("user_email", rows[0])
        self.assertNotIn("session_token", str(rows))

    def test_multi_response_and_markup(self):
        src = item(
            "Select two managed database services:",
            ["<b>Amazon RDS</b>", "AWS Lambda", "Amazon DynamoDB", "Amazon EC2"],
            ["Amazon DynamoDB", "Amazon RDS"],
        )
        row = importer.convert(src)
        self.assertEqual(row["options"][0], "Amazon RDS")
        self.assertEqual(row["type"], "multi")
        self.assertEqual(row["correct"], [0, 2])
        self.assertEqual(row["source_section"], "Sample Section")

    def test_letter_key_only_when_text_is_not_matched(self):
        row = importer.convert(item(correct=["D"]))
        self.assertEqual(row["correct"], [3])

    def test_literal_option_value_takes_precedence_over_letter(self):
        row = importer.convert(item(
            "Select the literal option", ["A", "B", "C", "D"], ["B"]
        ))
        self.assertEqual(row["correct"], [1])

    def test_id_does_not_depend_on_udemy_account_or_question_id(self):
        a = item()
        b = item()
        a["id"], b["id"] = 1, 9999
        b["user_email"] = "changed@example.test"
        self.assertEqual(importer.convert(a)["id"], importer.convert(b)["id"])

    def test_unknown_answer_rejected_not_guessed(self):
        with self.assertRaisesRegex(importer.ImportProblem, "match"):
            importer.convert(item(correct=["Not one of the options"]))
        with self.assertRaisesRegex(importer.ImportProblem, "must be"):
            importer.convert(item(correct=[1]))

    def test_duplicate_choices_rejected(self):
        with self.assertRaisesRegex(importer.ImportProblem, "duplicate answer"):
            importer.convert(item(answers=["Same", "same", "Other"], correct=["Other"]))

    def test_all_answers_correct_rejected(self):
        with self.assertRaisesRegex(importer.ImportProblem, "distractor"):
            importer.convert(item(answers=["First", "Second"], correct=["First", "Second"]))

    def test_missing_question_or_incorrect_json_rejected(self):
        with self.assertRaises(importer.ImportProblem):
            importer._question_rows({"profile": {"name": "nobody"}})
        with self.assertRaises(importer.ImportProblem):
            importer.convert({"prompt": {"question": "", "answers": ["x", "y"]},
                              "correct_response": ["x"]})

    def test_incomplete_pagination_requires_explicit_opt_in(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "page-1.json"
            src.write_text(json.dumps({"next": "page-2", "results": [item()]}),
                           encoding="utf-8")
            with self.assertRaisesRegex(importer.ImportProblem, "next page"):
                importer.import_files([src])
            self.assertEqual(len(importer.import_files([src], allow_incomplete=True)), 1)

    def test_duplicates_across_input_files_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            src1, src2 = Path(tmp) / "page1.json", Path(tmp) / "page2.json"
            src1.write_text(json.dumps({"results": [item()]}), encoding="utf-8")
            src2.write_text(json.dumps({"results": [item()]}), encoding="utf-8")
            with self.assertRaisesRegex(importer.ImportProblem, "duplicate question"):
                importer.import_files([src1, src2])

    def test_jsonl_input(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "input.jsonl"
            first = item()
            second = item(question="What is an availability zone?")
            src.write_text(json.dumps(first) + "\n" + json.dumps(second) + "\n",
                           encoding="utf-8")
            self.assertEqual(len(importer.import_files([src])), 2)

    def test_atomic_private_write_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "questions.jsonl"
            rows = [importer.convert(item())]
            importer.write_private(rows, target)
            self.assertEqual(json.loads(target.read_text(encoding="utf-8").strip()), rows[0])
            if os.name == "posix":
                self.assertEqual(target.stat().st_mode & 0o777, 0o600)
            with self.assertRaisesRegex(importer.ImportProblem, "output exists"):
                importer.write_private(rows, target)
            importer.write_private(rows, target, overwrite=True)
            self.assertEqual(len(list(Path(tmp).glob(".udemy-import-*"))), 0)

    def test_output_into_public_repo_is_blocked(self):
        repo_root = Path(importer.__file__).resolve().parents[2]
        with self.assertRaisesRegex(importer.ImportProblem, "outside"):
            importer.write_private([importer.convert(item())],
                                   repo_root / "do-not-create-udemy-content.jsonl")


if __name__ == "__main__":
    unittest.main()
