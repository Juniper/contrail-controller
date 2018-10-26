#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
VNC management for Mesos
"""

import gevent
from gevent.queue import Empty
import requests

class VncMesos(object):
    "Class to handle vnc operations"
    def __init__(self, args=None, logger=None, queue=None):
        self.args = args
        self.logger = logger
        self.queue = queue

    def process_q_event(self, event):
        """Process ADD/DEL event"""
        if obj_labels.operation == 'ADD':
            self.logger.info('Add request.')
        elif obj_labels.operation == 'DEL':
            self.logger.info('Delete request')
        else:
            self.logger.error('Invalid operation')

    def vnc_process(self):
        """Process event from the work queue"""
        while True:
            try:
                event = self.queue.get()
                self.logger.info("VNC: Handle CNI Data for ContainerId: {}."
                                 .format(event['cid']))
                self.process_q_event(event)
            except Empty:
                gevent.sleep(0)
