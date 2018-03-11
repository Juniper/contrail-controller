#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import os


def get_package_version(pkg):
    #retrieve current installed version of pkg
    cmd = "contrail-version %s | grep %s" % (pkg, pkg)
    version_line = os.popen(cmd).read()
    try:
        version = version_line.split()[1]
    except IndexError:
        return None
    else:
        return version


def is_running_in_docker():
    with open('/proc/1/cgroup', 'rt') as ifh:
        return 'docker' in ifh.read()


def is_running_in_kubepod():
    with open('/proc/1/cgroup', 'rt') as ifh:
        return 'kubepods' in ifh.read()
