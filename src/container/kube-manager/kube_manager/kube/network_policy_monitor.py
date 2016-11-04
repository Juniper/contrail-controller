import json
import gevent
from kube_monitor import KubeMonitor

class NetworkPolicyMonitor(KubeMonitor):

    def __init__(self, args=None, logger=None, q=None):
        super(NetworkPolicyMonitor, self).__init__(args, logger, q)
        self.handle = self.register_monitor('networkpolicies', beta=True)

    def _process_network_policy_event(self, event):
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
            self._process_network_policy_event(json.loads(line))
        except ValueError:
            print("Invalid JSON data from response stream:%s" % line)

    def network_policy_callback(self):
        while True:
            self.process()
            gevent.sleep(0)
