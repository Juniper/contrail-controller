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
        namespce_data = event['object']
        event_type = event['type']

        if self.db:
            namespace_uuid = self.db.get_uuid(event['object'])
            if event_type != 'DELETED':
                # Update Namespace DB.
                namespace = self.db.locate(namespace_uuid)
                namespace.update(namespce_data)
            else:
                # Remove the entry from Namespace DB.
                self.db.delete(namespace_uuid)

        print("Put %s %s %s" % (event['type'],
            event['object'].get('kind'),
            event['object']['metadata'].get('name')))
        self.q.put(event)

    def event_callback(self):
        while True:
            self.process()
            gevent.sleep(0)
