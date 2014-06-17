# vim: tabstop=4 shiftwidth=4 softtabstop=4

# Copyright 2012 Locaweb.
# All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.
#
# @author: Juliano Martinez, Locaweb.

import fcntl
import os
import shlex
import signal
import socket
import struct
import tempfile

from eventlet.green import subprocess
from eventlet import greenthread


def _subprocess_setup():
    # Python installs a SIGPIPE handler by default. This is usually not what
    # non-Python subprocesses expect.
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)


def subprocess_popen(args, stdin=None, stdout=None, stderr=None, shell=False,
                     env=None):
    return subprocess.Popen(args, shell=shell, stdin=stdin, stdout=stdout,
                            stderr=stderr, preexec_fn=_subprocess_setup,
                            close_fds=True, env=env)


def create_process(cmd, root_helper=None, addl_env=None):
    """Create a process object for the given command.

    The return value will be a tuple of the process object and the
    list of command arguments used to create it.
    """
    if root_helper:
        cmd = shlex.split(root_helper) + cmd
    cmd = map(str, cmd)

    env = os.environ.copy()
    if addl_env:
        env.update(addl_env)

    obj = subprocess_popen(cmd, shell=False,
                           stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE,
                           env=env)

    return obj, cmd


def execute(cmd, root_helper=None, process_input=None, addl_env=None,
            check_exit_code=True, return_stderr=False):
    try:
        obj, cmd = create_process(cmd, root_helper=root_helper,
                                  addl_env=addl_env)
        _stdout, _stderr = (process_input and
                            obj.communicate(process_input) or
                            obj.communicate())
        obj.stdin.close()
        m = ("\nCommand: %(cmd)s\nExit code: %(code)s\nStdout: %(stdout)r\n"
             "Stderr: %(stderr)r") % {'cmd': cmd, 'code': obj.returncode,
                                      'stdout': _stdout, 'stderr': _stderr}
        if obj.returncode:
            if check_exit_code:
                raise RuntimeError(m)
    finally:
        # NOTE(termie): this appears to be necessary to let the subprocess
        #               call clean something up in between calls, without
        #               it two execute calls in a row hangs the second one
        greenthread.sleep(0)

    return return_stderr and (_stdout, _stderr) or _stdout


def get_interface_mac(interface):
    DEVICE_NAME_LEN = 15
    MAC_START = 18
    MAC_END = 24
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    info = fcntl.ioctl(s.fileno(), 0x8927,
                       struct.pack('256s', interface[:DEVICE_NAME_LEN]))
    return ''.join(['%02x:' % ord(char)
                    for char in info[MAC_START:MAC_END]])[:-1]


def replace_file(file_name, data):
    """Replaces the contents of file_name with data in a safe manner.

    First write to a temp file and then rename. Since POSIX renames are
    atomic, the file is unlikely to be corrupted by competing writes.

    We create the tempfile on the same device to ensure that it can be renamed.
    """

    base_dir = os.path.dirname(os.path.abspath(file_name))
    tmp_file = tempfile.NamedTemporaryFile('w+', dir=base_dir, delete=False)
    tmp_file.write(data)
    tmp_file.close()
    os.chmod(tmp_file.name, 0o644)
    os.rename(tmp_file.name, file_name)


def find_child_pids(pid):
    """Retrieve a list of the pids of child processes of the given pid."""

    try:
        raw_pids = execute(['ps', '--ppid', pid, '-o', 'pid='])
    except RuntimeError as e:
        no_children_found = 'Exit code: 1' in str(e)
        if no_children_found:
            ctxt.reraise = False
            return []
        raise
    return [x.strip() for x in raw_pids.split('\n') if x.strip()]
