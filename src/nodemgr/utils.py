import os

class NodeMgrUtils(object):
    @staticmethod
    def get_package_version(pkg):
        #retrieve current installed version of pkg
        cmd = "contrail-version %s | grep %s" % (pkg,pkg)
        version = os.popen(cmd).read()
        return version.split()[1]
