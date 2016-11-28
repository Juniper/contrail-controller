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

import subprocess
import unittest

from netronome.vrouter.subprocess import *


class TestSubprocess(unittest.TestCase):
    def test_check_stdout_and_stderr_failure(self):
        try:
            args = ['/bin/sh', '-c', '/']

            with self.assertRaises(subprocess.CalledProcessError):
                # This is supposed to be plug-compatible with:
                # subprocess.check_output(args=args)
                check_stdout_and_stderr(args=args)

        except subprocess.CalledProcessError as e:
            self.assertIsNotNone(e.output)
            self.assertNotEqual(e.output, '')

    def test_check_stdout_and_stderr_success(self):
        # I actually had broken this at one point...
        args = ['/bin/true']
        out = check_stdout_and_stderr(args=args)
        self.assertIsNotNone(out)

if __name__ == '__main__':
    unittest.main()
