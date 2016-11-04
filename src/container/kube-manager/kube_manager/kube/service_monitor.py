import json
import gevent
from kube_monitor import KubeMonitor

class ServiceMonitor(KubeMonitor):

    def __init__(self, args=None, logger=None, q=None):
        super(ServiceMonitor, self).__init__(args, logger, q)
        self.handle = self.register_monitor('services')

    def _process_service_event(self, event):
        service_data = event['object']
        event_type = event['type']

        service_name = service_data['metadata'].get('name')
        namespace = service_data['metadata'].get('namespace')
        if not service_name or not namespace:
            return
        print("Put %s %s %s:%s" % (event['type'],
            event['object'].get('kind'),
            event['object']['metadata'].get('namespace'),
            event['object']['metadata'].get('name')))
        self.q.put(event)

    def process(self):
        line = next(self.handle)
        if not line:
            return

        try:
            self._process_service_event(json.loads(line))
        except ValueError:
            print("Invalid JSON data from response stream:%s" % line)

    def service_callback(self):
        while True:
            self.process()
            gevent.sleep(0)
