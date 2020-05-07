# -*- coding: utf-8 -*-
from __future__ import unicode_literals

import unittest

from cfgm_common.utils import CacheContainer
from cfgm_common.utils import decode_string
from cfgm_common.utils import encode_string


class TestCacheContainer(unittest.TestCase):
    def test_cache_container_trimming(self):
        c = CacheContainer(5)
        lst = ['a', 'b', 'c', 'd', 'e', 'f', 'h', 'i', 'j', 'k', 'm']

        for index, value in enumerate(lst):
            c[value] = index + 1

        self.assertEqual(len(list(c.dictionary.keys())), 5)
        self.assertEqual(set(lst[-5:]), set(c.dictionary.keys()))

    def test_cache_container_fetch(self):
        c = CacheContainer(5)
        lst = ['a', 'b', 'c', 'd', 'e']

        for index, value in enumerate(lst):
            c[value] = index + 1

        self.assertEqual(set(lst), set(c.dictionary.keys()))

        # get the oldest value and check on the next set its not lost
        c['a']
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


class TestFqNameEncode(unittest.TestCase):
    def test_fq_name_encoding(self):
        test_suite = [
            ('only-ascii', 'only-ascii'),
            ('only ascii with space', 'only+ascii+with+space'),
            ('non-ascii-é', 'non-ascii-%C3%A9'),
            ('non ascii with space é', 'non+ascii+with+space+%C3%A9'),
            ('non-ascii-encoded-\xe9', 'non-ascii-encoded-%C3%A9'),
            (b'binary', TypeError),
            ('foo=bar', 'foo=bar'),
            # (, ),
        ]
        for string, expected_result in test_suite:
            if (isinstance(expected_result, type) and
                    issubclass(expected_result, Exception)):
                self.assertRaises(expected_result, encode_string, string)
            else:
                self.assertEqual(expected_result, encode_string(string))
                self.assertEqual(decode_string(expected_result), string)
