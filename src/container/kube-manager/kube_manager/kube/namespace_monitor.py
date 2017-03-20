#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import json
import gevent
from kube_monitor import KubeMonitor
from kube_manager.common.kube_config_db import NamespaceKM

class NamespaceMonitor(KubeMonitor):

    def __init__(self, args=None, logger=None, q=None):
        super(NamespaceMonitor, self).__init__(args, logger, q, NamespaceKM,
            resource_name='namespaces')
        self.init_monitor()
        self.logger.info("NamespaceMonitor init done.");

    def get_entry_url(self, base_url, entry):
        """Get URL to an entry.
        NOTE: This method overrides a generic implementation provided by
        KubeMonitor base class. This is to workaround a bug in Kuberneters
        config stored in its api server where the 'selfLink' is not correctly
        populated for namespace entries. Once that bug is fixed, this method
        should be removed.
        """
        return self.v1_url + "/namespaces/" +  entry['metadata']['name']

    def process_event(self, event):
        namespace_data = event['object']
        event_type = event['type']
        kind = event['object'].get('kind')
        name = event['object']['metadata'].get('name')

        if self.db:
            namespace_uuid = self.db.get_uuid(event['object'])
            if event_type != 'DELETED':
                # Update Namespace DB.
                namespace = self.db.locate(namespace_uuid)
                namespace.update(namespace_data)
            else:
                # Remove the entry from Namespace DB.
                self.db.delete(namespace_uuid)
        else:
            namespace_uuid = event['object']['metadata'].get('uid')

        print("%s - Got %s %s %s:%s"
              %(self.name, event_type, kind, name, namespace_uuid))
        self.logger.debug("%s - Got %s %s %s:%s"
              %(self.name, event_type, kind, name, namespace_uuid))
        self.q.put(event)

    def event_callback(self):
        while True:
            self.process()
            gevent.sleep(0)
