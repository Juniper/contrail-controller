#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Mesos network manager
"""

import socket
import gevent
from gevent.queue import Queue

import common.args as mesos_args
import common.logger as logger
from vnc_api.vnc_api import *
import vnc.vnc_mesos as vnc_mesos

class MesosNetworkManager(object):
    def __init__(self, args=None):
        self.args = args
        self.logger = logger.MesosManagerLogger(args)
        self.q = Queue()
        self.vnc = vnc_mesos.VncMesos(args=self.args,
            logger=self.logger, q=self.q)

    def start_tasks(self):
        self.logger.info("Starting all tasks.")

        gevent.joinall([
            gevent.spawn(self.vnc.vnc_process),
        ])


def main():
    args = mesos_args.parse_args()
    mesos_nw_mgr = MesosNetworkManager(args)
    mesos_nw_mgr.start_tasks()


if __name__ == '__main__':
    main()
