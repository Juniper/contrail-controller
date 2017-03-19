#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import json
import gevent
from kube_monitor import KubeMonitor
from kube_manager.common.kube_config_db import PodKM

class PodMonitor(KubeMonitor):

    def __init__(self, args=None, logger=None, q=None):
        super(PodMonitor, self).__init__(args, logger, q, PodKM,
            resource_name='pods')
        self.init_monitor()
        self.logger.info("PodMonitor init done.");

    def process_event(self, event):
        pod_data = event['object']
        event_type = event['type']
        kind = event['object'].get('kind')

        if event_type != 'DELETED':
            if pod_data['spec'].get('hostNetwork'):
                return
            if not pod_data['spec'].get('nodeName'):
                return

        namespace = pod_data['metadata'].get('namespace')
        pod_name = pod_data['metadata'].get('name')
        if not namespace or not pod_name:
            return

        if self.db:
            pod_uuid = self.db.get_uuid(event['object'])
            if event_type != 'DELETED':
                # Update Pod DB.
                pod = self.db.locate(pod_uuid)
                pod.update(pod_data)
            else:
                # Remove the entry from Pod DB.
                self.db.delete(pod_uuid)
        else:
            pod_uuid = pod_data['metadata'].get('uid')

        print("%s - Got %s %s %s:%s:%s"
              %(self.name, event_type, kind, namespace, pod_name, pod_uuid))
        self.logger.debug("%s - Got %s %s %s:%s:%s"
              %(self.name, event_type, kind, namespace, pod_name, pod_uuid))
        self.q.put(event)

    def event_callback(self):
        while True:
            self.process()
            gevent.sleep(0)
