import json
import gevent
from kube_monitor import KubeMonitor
from kube_manager.common.kube_config_db import NetworkPolicyKM

class NetworkPolicyMonitor(KubeMonitor):

    def __init__(self, args=None, logger=None, q=None, network_policy_db=None):
        super(NetworkPolicyMonitor, self).__init__(args, logger, q,
            NetworkPolicyKM)
        self.handle = self.register_monitor('networkpolicies', beta=True)
        self.logger.info("NetworkPolicyMonitor init done.");

    def _process_network_policy_event(self, event):
        print("Put %s %s %s:%s" % (event['type'],
            event['object'].get('kind'),
            event['object']['metadata'].get('namespace'),
            event['object']['metadata'].get('name')))

        np_data = event['object']
        event_type = event['type']

        if self.db:
            np_uuid = self.db.get_uuid(np_data)
            if event_type != 'DELETED':
                # Update Network Policy DB.
                np = self.db.locate(np_uuid)
                np.update(np_data)
            else:
                # Remove the entry from Network Policy DB.
                self.db.delete(np_uuid)

        self.q.put(event)

    def process(self):
        try:
            line = next(self.handle)
            if not line:
                return
        except StopIteration:
            return

        try:
            self._process_network_policy_event(json.loads(line))
        except ValueError:
            print("Invalid JSON data from response stream:%s" % line)

    def network_policy_callback(self):
        while True:
            self.process()
            gevent.sleep(0)
