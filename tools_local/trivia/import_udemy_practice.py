#!/usr/bin/env python3
"""Import a locally saved Udemy practice-test JSON response into private study JSONL.

No network access, browser automation, cookies or credentials. This is an
intermediate format, NOT a CrossPlay XTRIVIA v1 pack: v1 cannot store long
scenario questions, explanations, or multiple correct choices.

Known historical Udemy quiz shape (not guaranteed by Udemy):
  {"results": [{"prompt": {"question": "...", "answers": ["...", "..."],
                            "explanation": "..."},
                "correct_response": ["exact answer text"], "section": "..."}]}

The tool deliberately rejects unknown answer keys rather than guessing.
Do not commit purchased questions or raw browser responses to this repository.
"""

import argparse
import hashlib
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import re
import sys
import tempfile


class ImportProblem(ValueError):
    """Input was incomplete or could not be mapped without ambiguity."""


class _PlainText(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts = []

    def handle_starttag(self, tag, attrs):
        if tag.lower() in ("p", "div", "li", "br", "h1", "h2", "h3"):
            self.parts.append("\n")

    def handle_endtag(self, tag):
        if tag.lower() in ("p", "div", "li", "h1", "h2", "h3"):
            self.parts.append("\n")

    def handle_data(self, data):
        self.parts.append(data)


def _plain(value):
    if not isinstance(value, str):
        raise ImportProblem("expected a text field")
    parser = _PlainText()
    parser.feed(value)
    text = "".join(parser.parts).replace("\xa0", " ")
    lines = [re.sub(r"[ \t\r\f\v]+", " ", line).strip()
             for line in text.split("\n")]
    return "\n".join(line for line in lines if line)


def _answer_text(value):
    if isinstance(value, str):
        return _plain(value)
    if isinstance(value, dict):
        for key in ("text", "answer", "value", "body"):
            if key in value:
                return _answer_text(value[key])
    raise ImportProblem("unsupported answer option shape")


def _question_rows(data):
    if isinstance(data, list):
        rows = []
        for item in data:
            rows.extend(_question_rows(item))
        return rows
    if isinstance(data, dict):
        prompt = data.get("prompt")
        if isinstance(prompt, dict) and "question" in prompt and "answers" in prompt:
            return [data]
        # Do not recursively visit arbitrary fields: browser responses can
        # contain private account/session metadata unrelated to the quiz.
        for key in ("results", "assessments", "questions", "items", "data"):
            if key in data and isinstance(data[key], (list, dict)):
                return _question_rows(data[key])
    raise ImportProblem("no supported question records; expected results[].prompt")


def _correct_positions(response, options):
    if isinstance(response, str):
        response = [response]
    if not isinstance(response, list) or not response:
        raise ImportProblem("missing or unsupported correct_response")
    selected = set()
    for entry in response:
        if not isinstance(entry, str):
            raise ImportProblem("correct_response entries must be option text or letters")
        value = _plain(entry)
        matches = [i for i, option in enumerate(options)
                   if option.casefold() == value.casefold()]
        if len(matches) == 1:
            index = matches[0]
        elif len(matches) == 0 and len(value) == 1 and "A" <= value.upper() <= "F":
            index = ord(value.upper()) - ord("A")
            if index >= len(options):
                raise ImportProblem("answer letter outside available options")
        else:
            raise ImportProblem("correct answer does not uniquely match an option")
        if index in selected:
            raise ImportProblem("duplicate correct answer")
        selected.add(index)
    if len(selected) == len(options):
        raise ImportProblem("every option marked correct; no distractor")
    return sorted(selected)


def convert(raw):
    """Map only question-specific fields; never copy account/session data."""
    prompt = raw.get("prompt")
    if not isinstance(prompt, dict):
        raise ImportProblem("missing prompt object")
    question = _plain(prompt.get("question"))
    choices = prompt.get("answers")
    if not isinstance(choices, list) or not 2 <= len(choices) <= 6:
        raise ImportProblem("expected two to six answer options")
    options = [_answer_text(item) for item in choices]
    if not question or not all(options):
        raise ImportProblem("blank question or answer")
    if len({option.casefold() for option in options}) != len(options):
        raise ImportProblem("duplicate answer options")
    correct = _correct_positions(raw.get("correct_response"), options)
    explanation = prompt.get("explanation", "")
    section = raw.get("section", "")
    if explanation is None:
        explanation = ""
    if section is None:
        section = ""
    if not isinstance(explanation, str) or not isinstance(section, str):
        raise ImportProblem("unsupported explanation/section shape")
    fingerprint = hashlib.sha256(
        (question + "\0" + "\0".join(options)).encode("utf-8")
    ).hexdigest()[:16]
    return {
        "id": fingerprint,
        "type": "multi" if len(correct) > 1 else "single",
        "q": question,
        "options": options,
        "correct": correct,  # zero-based positions in options; NOT choice letters
        "explanation": _plain(explanation),
        "source_section": _plain(section),
        "category": "Uncategorized",  # never infer an AWS exam domain from section
    }


def read_input(path, allow_incomplete=False):
    """Accept one local JSON response, or JSONL of responses/questions."""
    path = Path(path)
    with path.open("r", encoding="utf-8-sig") as f:
        if path.suffix.lower() == ".jsonl":
            data = [json.loads(line) for line in f if line.strip()]
        else:
            data = json.load(f)
    if isinstance(data, dict) and data.get("next") and not allow_incomplete:
        raise ImportProblem(
            "paginated response has a next page; save every page and rerun "
            "with --allow-incomplete only when providing all pages"
        )
    return _question_rows(data)


def import_files(paths, allow_incomplete=False):
    records = []
    seen = set()
    for path in paths:
        rows = read_input(path, allow_incomplete)
        for position, raw in enumerate(rows, 1):
            try:
                record = convert(raw)
            except (ImportProblem, TypeError) as exc:
                raise ImportProblem(f"{Path(path).name}: record {position}: {exc}") from exc
            if record["id"] in seen:
                raise ImportProblem(
                    f"{Path(path).name}: record {position}: duplicate question across inputs"
                )
            seen.add(record["id"])
            records.append(record)
    if not records:
        raise ImportProblem("no questions found")
    return records


def write_private(records, destination, overwrite=False):
    """Atomic, owner-only JSONL output outside the public git worktree."""
    target = Path(destination).expanduser().resolve()
    repo_root = Path(__file__).resolve().parents[2]
    if target == repo_root or repo_root in target.parents:
        raise ImportProblem("output must be outside the public repository")
    if target.exists() and not overwrite:
        raise ImportProblem("output exists; use --overwrite to replace it")
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=".udemy-import-", dir=str(target.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as out:
            for record in records:
                out.write(json.dumps(record, ensure_ascii=False) + "\n")
            out.flush()
            os.fsync(out.fileno())
        os.replace(tmp, target)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--src", nargs="+", required=True,
                        help="local Udemy assessments JSON/JSONL file(s)")
    parser.add_argument("--out", help="private output JSONL outside repository")
    parser.add_argument("--overwrite", action="store_true")
    parser.add_argument("--allow-incomplete", action="store_true",
                        help="allow individual paginated input files (provide all pages)")
    parser.add_argument("--dry-run", action="store_true",
                        help="validate and count, do not write questions")
    args = parser.parse_args(argv)
    if not args.dry_run and not args.out:
        parser.error("--out is required unless --dry-run")
    try:
        records = import_files(args.src, args.allow_incomplete)
        if not args.dry_run:
            write_private(records, args.out, args.overwrite)
    except (ImportProblem, OSError, ValueError) as exc:
        parser.exit(1, f"Import failed: {exc}\n")
    single = sum(row["type"] == "single" for row in records)
    print(f"Validated {len(records)} English questions: {single} single-answer, "
          f"{len(records) - single} multiple-answer."
          + (" No file written." if args.dry_run else " Private JSONL written."))


if __name__ == "__main__":
    main()
