# vim: set expandtab shiftwidth=4 fileencoding=UTF-8:

# Copyright 2016 Netronome Systems, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import logging
import unittest

# import test
from netronome import subcmd
from netronome.vrouter.tests.unit import *


def _ROOT_LMC(cls=LogMessageCounter):
    return attachLogHandler(logging.root, cls())


def _subcmd():
    return subcmd.Subcmd(
        name='name', description='description', logger=subcmd._LOGGER
    )


class TestSubcmd(unittest.TestCase):
    def test_Subcmd_ctor(self):
        _subcmd()

    def test_SubcmdApp_ctor(self):
        s = subcmd.SubcmdApp()
        self.assertIsNotNone(s.cmds)
        self.assertIsNotNone(s.cmd_map)

        s = subcmd.SubcmdApp(cmds=(_subcmd(),))
        self.assertIsNotNone(s.cmds)
        self.assertIsNotNone(s.cmd_map)

        with self.assertRaises(ValueError):
            s.cmds = None
        with self.assertRaises(ValueError):
            s.cmds = 1
        with self.assertRaises(ValueError):
            s.cmds = 'a string'
        with self.assertRaises(ValueError):
            s.cmds = type(self)

    def test_subcmd_SystemExit_ec_None(self):
        test_data = (
            # Normal exit code pairs.
            (0, 0),
            (1, 1),
            (2, 2),
            (3, 3),

            # Emulate oslo_config.cfg with an invalid command-line argument
            # value. oslo_config raises SystemExit without any arguments in
            # this case, which means "success." We need to make sure that we
            # are working around this bug properly.
            (None, 2),
        )

        for td in test_data:
            c = _subcmd()

            def parse_args(*args, **kwds):
                if td[0] is None:
                    raise SystemExit
                else:
                    raise SystemExit(td[0])

            c.parse_args = parse_args

            s = subcmd.SubcmdApp(cmds=(c,))
            with _ROOT_LMC():
                ec = s.run(argv=['test_subcmd_SystemExit_ec_None', 'name'])
                self.assertEqual(ec, td[1])

if __name__ == '__main__':
    unittest.main()
