import unittest
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
if __name__=="__main__":
    unittest.main()
