#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from __future__ import print_function

import gevent
from kube_manager.common.kube_config_db import NetworkPolicyKM
from kube_manager.kube.kube_monitor import KubeMonitor


class NetworkPolicyMonitor(KubeMonitor):

    def __init__(self, args=None, logger=None, q=None, network_policy_db=None):
        super(NetworkPolicyMonitor, self).__init__(
            args, logger, q,
            NetworkPolicyKM, resource_type='networkpolicy')
        self.init_monitor()
        self.logger.info("NetworkPolicyMonitor init done.")

    def process_event(self, event):
        np_data = event['object']
        event_type = event['type']
        kind = event['object'].get('kind')
        namespace = event['object']['metadata'].get('namespace')
        name = event['object']['metadata'].get('name')

        if self.db:
            np_uuid = self.db.get_uuid(np_data)
            np = self.db.locate(np_uuid)
            if event_type != 'DELETED':
                # Update Network Policy DB.
                np.update(np_data)
            else:
                # Invoke pre-delete processing for network policy delete.
                np.remove_entry()

                # Remove the entry from Network Policy DB.
                self.db.delete(np_uuid)
        else:
            np_uuid = event['object']['metadata'].get('uid')

        print(
            "%s - Got %s %s %s:%s:%s"
            % (self.name, event_type, kind, namespace, name, np_uuid))
        self.logger.debug(
            "%s - Got %s %s %s:%s:%s"
            % (self.name, event_type, kind, namespace, name, np_uuid))
        self.q.put(event)

    def event_callback(self):
        while True:
            self.process()
            gevent.sleep(0)
