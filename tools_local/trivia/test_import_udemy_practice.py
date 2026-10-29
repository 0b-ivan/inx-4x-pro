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


    def test_har_extracts_only_quiz_assessments_and_deduplicates(self):
        from json import dumps
        def entry(url, payload, status=200):
            return {"request": {"method": "GET", "url": url,
                                "headers": [{"name": "Cookie", "value": "PRIVATE"}]},
                    "response": {"status": status,
                                 "content": {"text": dumps(payload)}}}
        first = item()
        first["section"] = "Domain 1: SDLC Automation"
        first["assessment_type"] = "multiple-choice"
        first["correct_response"] = ["a"]
        first["prompt"]["feedbacks"] = ["", "", "", ""]
        first["prompt"]["explanation"] = "<p>Explanation</p><img src='https://example.invalid/chart.png'>"
        second = item(question="Select two database services",
                      answers=["Amazon RDS", "Amazon EBS", "Amazon DynamoDB", "AWS Lambda"],
                      correct=["a", "c"])
        second["assessment_type"] = "multi-select"
        second["section"] = "Domain 2: Configuration Management and IaC"
        second["prompt"]["feedbacks"] = ["", "", "", ""]
        response1 = {"results": [first], "next": None, "count": 1}
        response2 = {"results": [second], "next": None, "count": 1}
        url1 = "https://www.udemy.com/api-2.0/quizzes/1/assessments/"
        url2 = "https://www.udemy.com/api-2.0/quizzes/2/assessments/?page=1"
        har = {"log": {"entries": [
            entry(url1, response1), entry(url1, response1),
            entry(url2, response2),
            entry("https://www.udemy.com/api-2.0/users/me", {"email": "PRIVATE"}),
        ]}}
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "export.har"
            src.write_text(dumps(har), encoding="utf-8")
            rows = importer.import_files([src])
            self.assertEqual(len(rows), 2)
            self.assertEqual({row["source_quiz"] for row in rows}, {"1", "2"})
            self.assertEqual({row["source_question_number"] for row in rows}, {1})
            self.assertEqual({row["type"] for row in rows}, {"single", "multi"})
            self.assertEqual(rows[0]["correct"], [0])
            self.assertEqual(rows[1]["correct"], [0, 2])
            self.assertEqual(rows[0]["category"], "Domain 1: SDLC Automation")
            self.assertEqual(rows[0]["explanation_images"], 1)
            self.assertIn("[IMAGE]", rows[0]["explanation"])
            self.assertIn("<img", rows[0]["explanation_html"])
            self.assertNotIn("PRIVATE", str(rows))

    def test_har_rejects_missing_and_conflicting_responses(self):
        from json import dumps
        endpoint = "https://www.udemy.com/api-2.0/quizzes/1/assessments/"
        def request(payload):
            return {"request": {"method": "GET", "url": endpoint},
                    "response": {"status": 200, "content": {"text": dumps(payload)}}}
        good = {"count": 1, "next": None, "results": [item()]}
        bad = {"count": 1, "next": None,
               "results": [item(question="Other question")]}
        self.assertEqual(len(importer._har_rows({"log": {"entries": [request(good)]}})), 1)
        with self.assertRaisesRegex(importer.ImportProblem, "different copies"):
            importer._har_rows({"log": {"entries": [request(good), request(bad)]}})
        with self.assertRaisesRegex(importer.ImportProblem, "count"):
            importer._har_rows({"log": {"entries": [request({**good, "count": 75})]}})
        with self.assertRaisesRegex(importer.ImportProblem, "paginated"):
            importer._har_rows({"log": {"entries": [request({**good, "next": "/next"})]}})
        with self.assertRaisesRegex(importer.ImportProblem, "no quiz assessment"):
            importer._har_rows({"log": {"entries": []}})

    def test_assessment_type_must_match_answer_count(self):
        row = item()
        row["assessment_type"] = "multi-select"
        with self.assertRaisesRegex(importer.ImportProblem, "disagrees"):
            importer.convert(row)

    def test_cli_rejects_questions_the_pack_builder_cannot_file(self):
        good = item("Which service runs containers?")
        good["section"] = "Domain 3: Resilient Cloud Solutions"
        data = {"count": 2, "next": None, "results": [item(), good]}
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "assessments.json"
            src.write_text(json.dumps(data), encoding="utf-8")
            out = Path(tmp) / "private.jsonl"
            with self.assertRaises(SystemExit) as failed:
                importer.main(["--src", str(src), "--out", str(out)])
            self.assertEqual(failed.exception.code, 1)
            self.assertFalse(out.exists())
            importer.main(["--src", str(src), "--out", str(out), "--allow-uncategorized"])
            rows = [json.loads(line) for line in out.read_text(encoding="utf-8").splitlines()]
            self.assertEqual([r["category"] for r in rows],
                             ["Uncategorized", "Domain 3: Resilient Cloud Solutions"])
        with self.assertRaisesRegex(importer.ImportProblem, "'Sample Section'"):
            importer.require_domains([importer.convert(item())])
        importer.require_domains([importer.convert(good)])

    def test_output_into_public_repo_is_blocked(self):
        repo_root = Path(importer.__file__).resolve().parents[2]
        with self.assertRaisesRegex(importer.ImportProblem, "outside"):
            importer.write_private([importer.convert(item())],
                                   repo_root / "do-not-create-udemy-content.jsonl")


if __name__ == "__main__":
    unittest.main()
