import json
import gevent
from kube_monitor import KubeMonitor

class EndPointMonitor(KubeMonitor):

    def __init__(self, args=None, logger=None, q=None):
        super(EndPointMonitor, self).__init__(args, logger, q,
            resource_name='endpoints')
        self.init_monitor()
        self.logger.info("EndPointyMonitor init done.");

    def process_event(self, event):
        endpoint_data = event['object']
        event_type = event['type']
        kind = event['object'].get('kind')

        endpoint_name = endpoint_data['metadata'].get('name')
        namespace = endpoint_data['metadata'].get('namespace')
        if not endpoint_name or not namespace:
            return

        if endpoint_name == "kube-controller-manager" or endpoint_name == "kube-scheduler":
            return

        print("%s - Got %s %s %s:%s"
              %(self.name, event_type, kind, namespace, endpoint_name))
        self.logger.info("%s - Got %s %s %s:%s"
              %(self.name, event_type, kind, namespace, endpoint_name))
        self.q.put(event)

    def event_callback(self):
        while True:
            self.process()
            gevent.sleep(0)
