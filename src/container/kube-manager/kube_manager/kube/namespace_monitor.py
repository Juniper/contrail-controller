import json
import gevent
from kube_monitor import KubeMonitor
from kube_manager.common.kube_config_db import NamespaceKM

class NamespaceMonitor(KubeMonitor):

    def __init__(self, args=None, logger=None, q=None):
        super(NamespaceMonitor, self).__init__(args, logger, q, NamespaceKM)
        self.handle = self.register_monitor('namespaces')
        self.logger.info("NamespaceMonitor init done.");

    def _process_namespace_event(self, event):
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

    def process(self):
        line = next(self.handle)
        if not line:
            return

        try:
            self._process_namespace_event(json.loads(line))
        except ValueError:
            print("Invalid JSON data from response stream:%s" % line)

    def namespace_callback(self):
        while True:
            self.process()
            gevent.sleep(0)
