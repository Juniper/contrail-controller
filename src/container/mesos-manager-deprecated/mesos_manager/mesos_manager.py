#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
Mesos network manager
"""
# Standard library import
import gevent
import sys
from gevent.queue import Queue

# Application library import
import common.args as mesos_args
import common.logger as logger
from cfgm_common import vnc_cgitb
import vnc.vnc_mesos as vnc_mesos
import mesos_server as mserver


class MesosNetworkManager(object):
    '''Starts all background process'''

    _mesos_network_manager = None

    def __init__(self, args=None, mesos_api_connected=False, queue=None):
        self.args = args
        if queue:
            self.queue = queue
        else:
            self.queue = Queue()
        self.logger = logger.MesosManagerLogger(args)
        self.vnc = vnc_mesos.VncMesos(args=self.args,
                                      logger=self.logger,
                                      queue=self.queue)
        self.mserver = mserver.MesosServer(args=self.args,
                                           logger=self.logger,
                                           queue=self.queue)
    # end __init__

    def start_tasks(self):
        self.logger.info("Starting all tasks.")
        gevent.joinall([
            gevent.spawn(self.vnc.vnc_process),
            gevent.spawn(self.mserver.start_server),
        ])
    # end start_tasks

    def reset(self):
        for cls in DBBaseMM.get_obj_type_map().values():
            cls.reset()

    @classmethod
    def get_instance(cls):
       return MesosNetworkManager._mesos_network_manager

    @classmethod
    def destroy_instance(cls):
       inst = cls.get_instance()
       if inst is None:
           return
       inst.vnc = None
       inst.q = None
       MesosNetworkManager._mesos_network_manager = None
# end class MesosNetworkManager


def main(args_str=None, mesos_api_skip=False, event_queue=None):
    vnc_cgitb.enable(format='text')
    args = mesos_args.parse_args(args_str)
    mesos_nw_mgr = MesosNetworkManager(args,
                                       mesos_api_connected=mesos_api_skip,
                                       queue=event_queue)
    MesosNetworkManager._mesos_network_manager = mesos_nw_mgr
    mesos_nw_mgr.start_tasks()


if __name__ == '__main__':
    sys.exit(main())
