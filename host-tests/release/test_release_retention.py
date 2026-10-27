"""Guard against pruning stable firmware, study data or unverified assets."""

from __future__ import annotations

import datetime as dt
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "release_retention", ROOT / "scripts_local" / "release_retention.py")
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)

NOW = dt.datetime(2026, 10, 27, 19, 0, tzinfo=dt.timezone.utc)


def release(index: int, lane: str = "X4 Pro Nightly Study", *, hours_ago: int = 24):
    hash8 = f"{index:08x}"
    tag = f"v1.16.13-rc.20261027.g{hash8}"
    return {
        "id": index,
        "name": f"{lane} {tag}",
        "tag_name": tag,
        "draft": False,
        "prerelease": True,
        "target_commitish": hash8 + "a" * 32,
        "published_at": (NOW - dt.timedelta(hours=hours_ago)).isoformat(),
        "assets": [{"name": "firmware.bin"}, {"name": "firmware.bin.sha256"}],
    }


class ReleaseRetentionTests(unittest.TestCase):
    def test_keeps_three_study_and_two_connect_four(self):
        releases = [release(i, hours_ago=10 - i) for i in range(1, 6)]
        releases += [release(i, "X4 Pro Connect Four Nightly", hours_ago=12 - i)
                     for i in range(6, 9)]
        protected = releases[4]["tag_name"]
        planned = MODULE.cleanup_plan(releases, now=NOW, protect_tag=protected,
                                      keep_study=3, keep_connect_four=2)
        self.assertEqual({p["id"] for p in planned}, {1, 2, 6})
        self.assertNotIn(protected, {p["tag_name"] for p in planned})

    def test_preserves_stable_data_milestones_and_drafts(self):
        study = [release(i, hours_ago=10 - i) for i in range(1, 6)]
        examples = [
            dict(release(90), name="v1.16.12", tag_name="v1.16.12", prerelease=False),
            dict(release(91), name="German Trivia SD Pack",
                 tag_name="trivia-de-pack"),
            dict(release(92), name="CrossPlay Public Study DEMO Packs",
                 tag_name="trivia-study-packs"),
            dict(release(93), name="X4 Pro Nightly Study wrong-title"),
            dict(release(94), draft=True),
            dict(release(95), assets=[{"name": "firmware.bin"}]),
            dict(release(96), target_commitish="main"),
            dict(release(97), name="Inx X4 Pro alpha", tag_name="v1.0.20-x4pro-alpha.8"),
        ]
        plan = MODULE.cleanup_plan(study + examples, now=NOW,
                                   protect_tag=study[-1]["tag_name"])
        self.assertEqual({p["id"] for p in plan}, {1, 2})

    def test_legacy_cleanup_only_explicit_alphas(self):
        def legacy(tag, name, assets, target="main", age=55):
            return {"tag_name": tag, "name": name, "draft": False,
                    "prerelease": True, "target_commitish": target,
                    "published_at": (NOW - dt.timedelta(days=age)).isoformat(),
                    "assets": [{"name": n} for n in assets]}
        old = [legacy(f"v1.0.20-x4pro-alpha.{i}",
                      f"Inx X4 Pro release/v1.0.20-x4pro-alpha.{i} (ALPHA)",
                      ["firmware.bin"]) for i in range(1, 9)]
        v = "v1.15.1-alpha"
        old.append(legacy(v, v, ["firmware.bin", f"crossplay-{v}-x4pro-full.bin"],
                          target="a" * 40))
        v = "v1.15.1-alpha.1"
        old.append(legacy(v, v, ["firmware.bin", f"crossplay-{v}-x4pro-full.bin"],
                          target="b" * 40))
        bogus = [
            legacy("v1.15.0", "v1.15.0", ["firmware.bin"]),
            legacy("v1.0.20-x4pro-alpha.3", "untrusted name", ["firmware.bin"]),
            legacy("v1.0.20-x4pro-alpha.5",
                   "Inx X4 Pro release/v1.0.20-x4pro-alpha.5 (ALPHA)",
                   ["firmware.bin"], age=2),
        ]
        planned = MODULE.legacy_alpha_plan(old + bogus, now=NOW)
        self.assertEqual({x["tag_name"] for x in planned},
                         MODULE.ARCHIVED_ALPHA_TAGS)

    def test_age_guard_and_absent_protected_tag_fail_closed(self):
        study = [release(i, hours_ago=6 - i) for i in range(1, 6)]
        selected = study[-1]["tag_name"]
        plan = MODULE.cleanup_plan(study, now=NOW, protect_tag=selected,
                                   min_age_hours=5)
        self.assertEqual([p["id"] for p in plan], [1])
        with self.assertRaises(ValueError):
            MODULE.cleanup_plan(study, now=NOW,
                                protect_tag="v1.16.13-rc.20261027.gdeadbeef")
        with self.assertRaises(ValueError):
            MODULE.cleanup_plan(study, now=NOW, protect_tag=selected,
                                keep_study=1)
        with self.assertRaises(ValueError):
            MODULE.cleanup_plan(study, now=NOW, protect_tag=selected,
                                min_age_hours=-1)


if __name__ == "__main__":
    unittest.main()
