import json
import gevent
from kube_monitor import KubeMonitor
from kube_manager.common.kube_config_db import ServiceKM

class ServiceMonitor(KubeMonitor):

    def __init__(self, args=None, logger=None, q=None):
        super(ServiceMonitor, self).__init__(args, logger, q, ServiceKM)
        self.handle = self.register_monitor('services')
        self.logger.info("ServiceMonitor init done.");

    def _process_service_event(self, event):
        service_data = event['object']
        event_type = event['type']

        service_name = service_data['metadata'].get('name')
        namespace = service_data['metadata'].get('namespace')
        if not service_name or not namespace:
            return

        if self.db:
            service_uuid = self.db.get_uuid(event['object'])
            if event_type != 'DELETED':
                # Update Service DB.
                service = self.db.locate(service_uuid)
                service.update(service_data)
            else:
                # Remove the entry from Service DB.
                self.db.delete(service_uuid)

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
