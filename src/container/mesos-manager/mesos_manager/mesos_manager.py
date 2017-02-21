#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
Mesos network manager
"""

import gevent
from gevent.queue import Queue

import common.args as mesos_args
import common.logger
from cfgm_common import vnc_cgitb
import vnc.vnc_mesos as vnc_mesos
import mesos_server as mserver


class MesosNetworkManager(object):
    def __init__(self, args=None):
        self.args = args
        self.q = Queue()

        self.logger = common.logger.MesosManagerLogger(args)

        self.vnc = vnc_mesos.VncMesos(args=self.args,
            logger=self.logger, q=self.q)

        self.mserver = mserver.MesosServer(args=self.args,
            logger=self.logger, q=self.q)
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
    main()
