import json
import gevent
from kube_monitor import KubeMonitor

class NamespaceMonitor(KubeMonitor):

    def __init__(self, args=None, logger=None, q=None):
        super(NamespaceMonitor, self).__init__(args, logger, q)
        self.handle = self.register_monitor('namespaces')

    def _process_namespace_event(self, event):
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
