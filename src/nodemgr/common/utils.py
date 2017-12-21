#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import os
import platform
import subprocess


class NodeMgrUtils(object):
    @staticmethod
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


def is_systemd_based():
    (pdist, version, name) = platform.dist()
    if pdist == 'Ubuntu' and version == '16.04':
        return True
    return False


def is_running_in_docker():
    with open('/proc/1/cgroup', 'rt') as ifh:
        return 'docker' in ifh.read()


def is_running_in_kubepod():
    with open('/proc/1/cgroup', 'rt') as ifh:
        return 'kubepods' in ifh.read()


def package_installed(pkg):
    (pdist, _, _) = platform.dist()
    if pdist == 'Ubuntu':
        cmd = "dpkg -l " + pkg
    else:
        cmd = "rpm -q " + pkg
    with open(os.devnull, "w") as fnull:
        return not subprocess.call(cmd.split(), stdout=fnull, stderr=fnull)
