import unittest
from cfgm_common.utils import CacheContainer

class TestCachContainer(unittest.TestCase):
    def test_cache_container_trimming(self):
        c = CacheContainer(5)
        l = ['a','b','c','d','e','f','h','i','j','k','m']

        for index, value in enumerate(l):
            c[value] = index+1

        self.assertEqual(len(c.dictionary.keys()), 5)
        self.assertEqual(set(l[-5:]), set(c.dictionary.keys()))

    def test_cache_container_fetch(self):
        c = CacheContainer(5)
        l = ['a','b','c','d','e']

        for index, value in enumerate(l):
            c[value] = index+1

        self.assertEqual(set(l), set(c.dictionary.keys()))

        # get the oldest value and check on the next set its not lost
        _ = c['a']
        c['f'] = 6
        self.assertEqual(set(['c', 'd', 'e', 'f', 'a']),
                         set(c.dictionary.keys()))

        # put a value for the oldest key and check its not lost
        c['c'] = 'x'
        self.assertEqual(c['c'], 'x')
        self.assertEqual(set(['d', 'e', 'f', 'a', 'c']),
                         set(c.dictionary.keys()))

        c['g'] = 7
        self.assertEqual(set(['e', 'f', 'a', 'c', 'g']),
                         set(c.dictionary.keys()))
