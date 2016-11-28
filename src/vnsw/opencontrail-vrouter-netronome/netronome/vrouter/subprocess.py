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

from __future__ import absolute_import

import subprocess


def check_stdout_and_stderr(args):
    """Like subprocess.check_output() but also returns stderr to the caller."""

    # Why the built-in subprocess module doesn't have something like this is a
    # mystery to me.
    p = subprocess.Popen(
        args, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    out, err = p.communicate()
    if p.returncode != 0:
        raise subprocess.CalledProcessError(
            cmd=args, returncode=p.returncode, output=out + err
        )

    # NOTE: discarding err
    return out
