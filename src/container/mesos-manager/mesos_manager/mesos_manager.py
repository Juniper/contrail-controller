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
import common.logger
from cfgm_common import vnc_cgitb
import vnc.vnc_mesos as vnc_mesos
import mesos_server as mserver


class MesosNetworkManager(object):
    '''Starts all background process'''
    def __init__(self, args=None):
        self.args = args
        self.queue = Queue()
        self.logger = common.logger.MesosManagerLogger(args)
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
# end class MesosNetworkManager


def main():
    vnc_cgitb.enable(format='text')
    args = mesos_args.parse_args()
    mesos_nw_mgr = MesosNetworkManager(args)
    mesos_nw_mgr.start_tasks()


if __name__ == '__main__':
    sys.exit(main())
