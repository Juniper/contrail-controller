import json
import gevent
from kube_monitor import KubeMonitor

class EndPointMonitor(KubeMonitor):

    def __init__(self, args=None, logger=None, q=None):
        super(EndPointMonitor, self).__init__(args, logger, q)
        self.handle = self.register_monitor('endpoints', beta=False)
        self.logger.info("EndPointyMonitor init done.");

    def _process_endpoint_event(self, event):
        endpoint_data = event['object']
        event_type = event['type']

        endpoint_name = endpoint_data['metadata'].get('name')
        namespace = endpoint_data['metadata'].get('namespace')

        if not endpoint_name or not namespace:
            return

        if endpoint_name == "kube-controller-manager" or endpoint_name == "kube-scheduler":
            return

        print("Put %s %s %s:%s" % (event['type'],
            event['object'].get('kind'),
            event['object']['metadata'].get('namespace'),
            event['object']['metadata'].get('name')))
        self.q.put(event)

    def process(self):
        try:
            line = next(self.handle)
            if not line:
                return
        except StopIteration:
            return

        try:
            self._process_endpoint_event(json.loads(line))
        except ValueError:
            print("Invalid JSON data from response stream:%s" % line)

    def endpoint_callback(self):
        while True:
            self.process()
            gevent.sleep(0)
