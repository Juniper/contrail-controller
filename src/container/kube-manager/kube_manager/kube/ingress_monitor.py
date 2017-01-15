import json
import gevent
from kube_monitor import KubeMonitor
from kube_manager.common.kube_config_db import IngressKM

class IngressMonitor(KubeMonitor):

    def __init__(self, args=None, logger=None, q=None):
        super(IngressMonitor, self).__init__(args,
              logger, q, IngressKM)
        self.handle = self.register_monitor('ingresses', beta=True)
        self.logger.info("IngressMonitor init done.");

    def _process_ingress_event(self, event):
        event_type = event['type']
        kind = event['object'].get('kind')

        namespace = event['object']['metadata'].get('namespace')
        name = event['object']['metadata'].get('name')
        if not namespace or not name:
            return

        event_obj = event['object']
        if self.db:
            uuid = self.db.get_uuid(event_obj)
            if event_type != 'DELETED':
                # Update Ingress DB.
                ingress_obj = self.db.locate(uuid)
                ingress_obj.update(event_obj)
            else:
                # Remove the entry from Ingress DB.
                self.db.delete(uuid)

        print("Put %s %s %s:%s" %(event_type, kind, namespace, name))
        self.q.put(event)

    def process(self):
        try:
            line = next(self.handle)
            if not line:
                return
        except StopIteration:
            return

        try:
            self._process_ingress_event(json.loads(line))
        except ValueError:
            print("Invalid JSON data from response stream:%s" % line)

    def ingress_callback(self):
        while True:
            self.process()
            gevent.sleep(0)
