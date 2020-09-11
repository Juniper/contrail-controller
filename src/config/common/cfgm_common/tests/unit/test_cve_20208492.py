# -*- coding: utf-8 -*-

#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

import unittest

from six.moves.urllib.request import AbstractBasicAuthHandler


class TestCVE(unittest.TestCase):

    def test_test(self):
        self.assertEqual('(?:^|,)'   # start of the string or ','
                         '[ \t]*'    # optional whitespaces
                         '([^ \t]+)' # scheme like "Basic"
                         '[ \t]+'    # mandatory whitespaces
                         # realm=xxx
                         # realm='xxx'
                         # realm="xxx"
                         'realm=(["\']?)([^"\']*)\\2', AbstractBasicAuthHandler.rx.pattern)
