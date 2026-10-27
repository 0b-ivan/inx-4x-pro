#!/usr/bin/env python3
"""Conservative, auditable retention for *feature* X4 Pro nightly releases.

The default is a dry run. Only correctly named, verified feature prereleases
are candidates. Stable SemVer, GitHub-hosted study packs, German translation
data, and legacy test milestones are never eligible. The --protect-tag guard
prevents deleting anything if the newly tested Nightly is not yet published.

Requires GitHub CLI and GH_TOKEN for live GitHub operation. No third-party deps.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys
from collections.abc import Sequence
from typing import Any

UTC = dt.timezone.utc
NIGHTLY_TAG = re.compile(r"^v[0-9]+\.[0-9]+\.[0-9]+-rc\.[0-9]{8}\.g([0-9a-f]{8})$")
LANES = (
    ("X4 Pro Nightly Study", "study"),
    ("X4 Pro Connect Four Nightly", "connect-four"),
)
REQUIRED_ASSETS = frozenset({"firmware.bin", "firmware.bin.sha256"})


def _published(release: dict[str, Any]) -> dt.datetime | None:
    raw = release.get("published_at")
    if not isinstance(raw, str):
        return None
    try:
        parsed = dt.datetime.fromisoformat(raw.replace("Z", "+00:00"))
        return parsed.astimezone(UTC) if parsed.tzinfo is not None else None
    except ValueError:
        return None


def lane_for(release: dict[str, Any]) -> str | None:
    """Accept only precisely shaped, published, identifiable Nightlies."""
    if release.get("draft") is not False or release.get("prerelease") is not True:
        return None
    tag = release.get("tag_name")
    if not isinstance(tag, str):
        return None
    match = NIGHTLY_TAG.fullmatch(tag)
    if match is None or _published(release) is None:
        return None
    sha = release.get("target_commitish")
    if not isinstance(sha, str) or not re.fullmatch(r"[0-9a-f]{40}", sha):
        return None
    if sha[:8] != match.group(1):
        return None
    assets = release.get("assets")
    if not isinstance(assets, list):
        return None
    asset_names = {x.get("name") for x in assets if isinstance(x, dict)}
    if not REQUIRED_ASSETS.issubset(asset_names):
        return None
    for title, key in LANES:
        if release.get("name") == title + " " + tag:
            return key
    return None


def cleanup_plan(
    releases: Sequence[dict[str, Any]],
    *,
    now: dt.datetime,
    protect_tag: str,
    keep_study: int = 3,
    keep_connect_four: int = 2,
    min_age_hours: float = 4.0,
) -> list[dict[str, Any]]:
    """Plan only old, unprotected feature-nightly releases."""
    if now.tzinfo is None:
        raise ValueError("now must be timezone-aware")
    if not NIGHTLY_TAG.fullmatch(protect_tag):
        raise ValueError("protect-tag must be a versioned immutable Nightly")
    protected = [r for r in releases if r.get("tag_name") == protect_tag]
    if len(protected) != 1 or lane_for(protected[0]) != "study":
        raise ValueError("expected published, verified study Nightly is absent")
    if keep_study < 2 or keep_connect_four < 2:
        raise ValueError("must retain at least two builds per feature lane")
    if min_age_hours < 0:
        raise ValueError("min-age-hours may not be negative")

    keep = {"study": keep_study, "connect-four": keep_connect_four}
    planned: list[dict[str, Any]] = []
    for lane in keep:
        found = [r for r in releases if lane_for(r) == lane]
        found.sort(key=lambda r: (_published(r), r["tag_name"]), reverse=True)
        # A currently published release or pinned current tag never gets pruned.
        for r in found[keep[lane]:]:
            age = (now.astimezone(UTC) - _published(r)).total_seconds() / 3600
            if r["tag_name"] == protect_tag or age < min_age_hours:
                continue
            planned.append(r)
    return planned


def github_releases(repo: str) -> list[dict[str, Any]]:
    all_releases: list[dict[str, Any]] = []
    page = 1
    while True:
        result = subprocess.run(
            ["gh", "api", f"repos/{repo}/releases?per_page=100&page={page}"],
            check=True,
            capture_output=True,
            text=True,
        )
        found = json.loads(result.stdout)
        if not isinstance(found, list):
            raise ValueError("GitHub API did not return a release list")
        all_releases.extend(found)
        if len(found) < 100:
            return all_releases
        page += 1
        if page > 20:
            raise ValueError("too many release pages; refusing cleanup")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", required=True, help="owner/repo")
    parser.add_argument("--protect-tag", required=True, help="just-published feature Nightly tag")
    parser.add_argument("--keep-study", type=int, default=3)
    parser.add_argument("--keep-connect-four", type=int, default=2)
    parser.add_argument("--min-age-hours", type=float, default=4.0)
    parser.add_argument("--apply", action="store_true", help="actually delete planned releases and tags")
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repository):
        parser.error("invalid owner/repo")
    # Cannot use arbitrary --repository in Actions to clean someone else's repo.
    connected = os.environ.get("GITHUB_REPOSITORY")
    if args.apply and (not connected or connected != args.repository):
        parser.error("--apply requires matching GITHUB_REPOSITORY")
    try:
        releases = github_releases(args.repository)
        planned = cleanup_plan(
            releases, now=dt.datetime.now(UTC), protect_tag=args.protect_tag,
            keep_study=args.keep_study, keep_connect_four=args.keep_connect_four,
            min_age_hours=args.min_age_hours,
        )
        print(f"Inspected {len(releases)} releases; obsolete safe candidates: {len(planned)}")
        for release in planned:
            print("PRUNE" if args.apply else "DRY RUN", release["tag_name"], release["name"])
        if not args.apply:
            print("No releases or tags were modified (default dry run).")
        for release in planned if args.apply else ():
            tag = release["tag_name"]
            # Use GitHub's release operation (not a git force-push), cleaning
            # the now-obsolete lightweight/annotated tag as well.
            subprocess.run(
                ["gh", "release", "delete", tag, "--repo", args.repository,
                 "--yes", "--cleanup-tag"],
                check=True,
            )
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"Release retention failed closed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
