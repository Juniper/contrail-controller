import json
import gevent
from kube_monitor import KubeMonitor

class PodMonitor(KubeMonitor):

    def __init__(self, args=None, logger=None, q=None,
                 pod_db=None):
        super(PodMonitor, self).__init__(args, logger, q)
        self.handle = self.register_monitor('pods')
        self.logger.info("PodMonitor init done.");
        self._pod_db = pod_db

    def _process_pod_event(self, event):
        pod_data = event['object']
        event_type = event['type']

        if event_type != 'DELETED':
            if pod_data['spec'].get('hostNetwork'):
                return
            if not pod_data['spec'].get('nodeName'):
                return

        if self._pod_db:
            pod_uuid = self._pod_db.get_uuid(event['object'])
            if event_type != 'DELETED':
                # Update Pod DB.
                pod = self._pod_db.locate(pod_uuid)
                pod.update(pod_data)
            else:
                # Remove the entry from Pod DB.
                self._pod_db.delete(pod_uuid)

        pod_name = pod_data['metadata'].get('name')
        namespace = pod_data['metadata'].get('namespace')
        if not pod_name or not namespace:
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
            self._process_pod_event(json.loads(line))
        except ValueError:
            print("Invalid JSON data from response stream:%s" % line)


    def pod_callback(self):
        while True:
            self.process()
            gevent.sleep(0)
