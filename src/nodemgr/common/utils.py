#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import os
import platform


def get_package_version(pkg):

    # retrieve current installed version of pkg
    try:
        cmd = 'rpm -q --qf "%%{VERSION}-%%{RELEASE}" %s' % pkg
        return os.popen(cmd).read()
    except Exception:
        return None


def is_running_in_docker():
    pid = os.getpid()
    with open('/proc/{}/cgroup'.format(pid), 'rt') as ifh:
        return 'docker' in ifh.read()

def is_running_in_podman():
    pid = os.getpid()
    with open('/proc/{}/cgroup'.format(pid), 'rt') as ifh:
        return 'libpod' in ifh.read()

def is_running_in_kubepod():
    pid = os.getpid()
    with open('/proc/{}/cgroup'.format(pid), 'rt') as ifh:
        return 'kubepods' in ifh.read()
