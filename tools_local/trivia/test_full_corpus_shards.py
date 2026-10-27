import unittest
import json
import tempfile
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import full_corpus_shards as shards
from full_corpus_shards import SHARDS
class ShardingTests(unittest.TestCase):
    def test_every_question_assigned_to_one_shard(self):
        combined=[i for s in range(SHARDS) for i in range(s,49958,SHARDS)]
        self.assertEqual(len(combined),49958)
        self.assertEqual(len(set(combined)),49958)
        self.assertEqual(set(combined),set(range(49958)))
    def test_each_job_bounded(self):
        self.assertLessEqual(max(len(range(s,49958,SHARDS)) for s in range(SHARDS)),1000)
    def test_exactly_fifty(self):
        self.assertEqual(SHARDS,50)

class ResumeTests(unittest.TestCase):
    def test_complete_checkpoint_skips_paid_calls_without_api_key(self):
        source=[{"id":str(i)} for i in range(49958)]
        rows=[{"id":str(i),"source_hash":"pinned","reviewed":False}
              for i in range(0,49958,SHARDS)]
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory)
            out=base/"questions.jsonl"
            out.write_text("".join(json.dumps(row)+"\\n" for row in rows),encoding="utf-8")
            args=SimpleNamespace(source="unused",seed="unused",shard=0,
                out=str(out),quarantine=str(base/"quarantine.jsonl"),
                ledger=str(base/"usage.jsonl"),summary=str(base/"summary.json"))
            with patch.object(shards,"originals",return_value=source), \\
                 patch.object(shards,"verify_seed",return_value={}), \\
                 patch.object(shards,"validate_record"), \\
                 patch.object(shards,"translate_one") as paid, \\
                 patch.dict("os.environ",{},clear=True):
                shards.translate(args)
                paid.assert_not_called()
            result=json.loads(Path(args.summary).read_text())
            self.assertEqual(result["translated"],len(rows))
            self.assertEqual(result["calls"],0)

if __name__=="__main__":
    unittest.main()
