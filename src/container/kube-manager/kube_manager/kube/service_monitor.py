#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import json
import gevent
from kube_monitor import KubeMonitor
from kube_manager.common.kube_config_db import ServiceKM

class ServiceMonitor(KubeMonitor):

    def __init__(self, args=None, logger=None, q=None):
        super(ServiceMonitor, self).__init__(args, logger, q, ServiceKM,
            resource_name='services')
        self.init_monitor()
        self.logger.info("ServiceMonitor init done.");

    def process_event(self, event):
        service_data = event['object']
        event_type = event['type']
        kind = event['object'].get('kind')

        namespace = service_data['metadata'].get('namespace')
        service_name = service_data['metadata'].get('name')
        if not namespace or not service_name:
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
        else:
            service_uuid = service_data['metadata'].get('uid')

        print("%s - Got %s %s %s:%s:%s"
              %(self.name, event_type, kind, namespace, service_name, service_uuid))
        self.logger.debug("%s - Got %s %s %s:%s:%s"
              %(self.name, event_type, kind, namespace, service_name, service_uuid))
        self.q.put(event)

    def event_callback(self):
        while True:
            self.process()
            gevent.sleep(0)
