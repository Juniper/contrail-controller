#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import os


def get_package_version(pkg):

    # retrieve current installed version of pkg
    try:
        cmd = 'rpm -q --qf "%%{VERSION}-%%{RELEASE}" %s' % pkg
        return os.popen(cmd).read()
    except Exception:
        return None


def is_running_in_docker():
    return os.path.exists('/.dockerenv')


def is_running_in_podman():
    pid = os.getpid()
    with open('/proc/{}/cgroup'.format(pid), 'rt') as ifh:
        return 'libpod' in ifh.read()


def is_running_in_kubepod():
    pid = os.getpid()
    with open('/proc/{}/cgroup'.format(pid), 'rt') as ifh:
        return 'kubepods' in ifh.read()


def is_running_in_containerd():
    pid = os.getpid()
    with open('/proc/{}/cgroup'.format(pid), 'rt') as ifh:
        return 'containerd' in ifh.read()


def get_memory_cgroup(pid_):
    with open('/proc/{}/cgroup'.format(pid_), 'r') as f:
        while True:
            line = f.readline()
            if not line:
                break
            value = line.partition(':memory:')[2]
            if value:
                return value.strip()

    return None
