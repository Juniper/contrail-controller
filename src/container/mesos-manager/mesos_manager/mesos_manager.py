#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import sys

"""
Mesos network manager
"""

class MesosNetworkManager(object):
    '''Starts all background process'''


    def __init__(self):
        _mesos_network_manager = None
    # end __init__

    def start_tasks(self):
        print ("Starting mesos manager.")
    # end start_tasks
# end class MesosNetworkManager


def main():
    mesos_nw_mgr = MesosNetworkManager()
    MesosNetworkManager._mesos_network_manager = mesos_nw_mgr
    mesos_nw_mgr.start_tasks()

if __name__ == '__main__':
    sys.exit(main())
