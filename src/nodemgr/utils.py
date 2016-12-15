#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import os

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
