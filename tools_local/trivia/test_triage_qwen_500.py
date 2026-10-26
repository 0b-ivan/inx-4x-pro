#!/usr/bin/env python3
import unittest
from triage_qwen_500 import classify,compile_queue,PRIORITIES

class HumanReviewQueueTests(unittest.TestCase):
    def fixture(self,id,numeric=None,wordplay=False,verdict=None):
        return {"id":id,"english":{"q":"What happened in 1999?","a":"Example","alt":[],"w":[]},
                "german":{"q":"Was geschah 1999?","a":"Beispiel","alt":[],"w":[]},
                "needs_human_review":True,
                "new_numeric_warnings":numeric or [],
                "risk_flags":["potential_wordplay"] if wordplay else [],
                "prior_ai_verdict":verdict,"prior_ai_reason":None}
    def test_numeric_is_hard_blocker(self):
        r=self.fixture("a",[{"kind":"unit_mismatch"}],verdict="reject")
        self.assertEqual(classify(r)[0],PRIORITIES[0])
    def test_rejected_not_auto_cleared_after_parser_fix(self):
        self.assertEqual(classify(self.fixture("b",verdict="reject"))[0],PRIORITIES[1])
    def test_wordplay(self):
        self.assertEqual(classify(self.fixture("c",wordplay=True))[0],PRIORITIES[2])
    def test_rewrite(self):
        self.assertEqual(classify(self.fixture("d",verdict="rewrite"))[0],PRIORITIES[3])
    def test_clean_draft_still_human_review(self):
        self.assertEqual(classify(self.fixture("e"))[0],PRIORITIES[4])
    def test_unapproved_flag_cannot_be_false(self):
        r=self.fixture("f");r["needs_human_review"]=False
        with self.assertRaises(ValueError): classify(r)
    def test_needs_exact_500(self):
        with self.assertRaises(ValueError): compile_queue([self.fixture("a")]*500)
    def test_500_unique_sorted(self):
        samples=[self.fixture(str(i)) for i in range(500)]
        samples[40]["prior_ai_verdict"]="reject"
        samples[41]["new_numeric_warnings"]=[{"kind":"numeric_mismatch"}]
        out=compile_queue(samples)
        self.assertEqual(len(out),500)
        self.assertEqual(out[0]["id"],"41")
        self.assertEqual(out[1]["id"],"40")
        self.assertTrue(all(x["needs_human_review"] for x in out))
if __name__=="__main__":
    unittest.main()
