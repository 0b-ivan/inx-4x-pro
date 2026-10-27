#!/usr/bin/env python3
"""Create original, public-domain-style DEMO training data for X4 Pro releases.

No Udemy/HAR or other privately licensed questions are read or published.
This dataset is for exercising the device downloader, not an exam simulation.
"""
import argparse
import json
import tempfile
from pathlib import Path

from build_aws_study import build

CERTS = ("DOP-C02-DEMO", "SAA-C03-DEMO")
TOPICS = (
    "Automation demo",
    "Infrastructure demo",
    "Reliability demo",
    "Observability demo",
    "Incident response demo",
    "Security demo",
)

def make_pack(cert: str, out: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="x4pro-public-") as temp:
        source = Path(temp) / "original-synthetic-questions.jsonl"
        with source.open("w", encoding="utf-8") as stream:
            for i in range(24):
                topic = i % 6
                multiple = i % 5 == 0
                # Each question is written specifically for demonstrating
                # navigation and choice logic; it is not a paid exam question.
                row = {
                    "q": (f"DEMO {cert} question {i+1}: A sample team needs to "
                          f"practice {TOPICS[topic].lower()}. "
                          "Which of these labels are marked as correct by this "
                          "synthetic device-test answer key? "
                          "This is demonstration content, not certification preparation."),
                    "options": [
                        "Demo choice Alpha",
                        "Demo choice Beta",
                        "Demo choice Gamma",
                        "Demo choice Delta",
                    ],
                    "correct": [0, 2] if multiple else [1],
                    "type": "multi" if multiple else "single",
                    "explanation": (
                        "Demonstration explanation only. The answer key is "
                        "synthetic; it does not represent an AWS certification fact."),
                    "category": f"Topic {topic + 1}: {TOPICS[topic]}",
                }
                stream.write(json.dumps(row) + "\n")
        n, size, _ = build(source, out / f"{cert}.dat")
        assert n == 24 and size > 100

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    out = args.out.expanduser().resolve()
    out.mkdir(parents=True, exist_ok=True)
    for cert in CERTS:
        make_pack(cert, out)
    (out / "catalog.txt").write_text(
        "".join(f"AWS|{cert}\n" for cert in CERTS), encoding="ascii")
    (out / "README.txt").write_text(
        "X4 Pro public study-pack DEMOS. All content is original synthetic test data.\n"
        "These are NOT real AWS exam questions. Personal paid question packs remain private.\n",
        encoding="utf-8")

if __name__ == "__main__":
    main()
