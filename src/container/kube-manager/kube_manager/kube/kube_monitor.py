import eventlet
import json
import sys
import requests
import os
import json
from urllib3 import (request,PoolManager)

class KubeMonitor(object):

    # Common handle to a generic http connection to api server.
    kube_api_conn_handle = None

    def __init__(self, args=None, logger=None, q=None, db=None,
                 resource_name='KubeMonitor', beta=False):
        self.args = args
        self.logger = logger
        self.q = q
        self.cloud_orchestrator = self.args.orchestrator
        self.token = self.args.token # valid only for OpenShift
        self.headers = {}

        if not self.kube_api_conn_handle:
            self.kube_api_conn_handle = PoolManager()

        # Per-monitor stream handle to api server.
        self.kube_api_stream_handle = None

        # Resource name corresponding to this monitor.
        self.resource_name = resource_name
        self.resource_beta = beta

        # Use Kube DB if kube object caching is enabled in config.
        if args.kube_object_cache == 'True':
            self.db = db
        else:
            self.db = None

        if self.cloud_orchestrator == "openshift":
            protocol = "https"
            self.headers = {'Authorization': "Bearer " + self.token}
        else: # kubernetes
            protocol = "http"

        # URL to the api server.
        self.url = "%s://%s:%s" % (protocol,
                                   self.args.kubernetes_api_server,
                                   self.args.kubernetes_api_port)
        # URL to the v1-components in api server.
        self.v1_url = "%s/api/v1" % (self.url)
        # URL to v1-beta1 components to api server.
        self.beta_url = "%s/apis/extensions/v1beta1" % (self.url)

        self.logger.info("KubeMonitor init done.");

    def _get_component_url(self):
        """URL to a component.
        This method return the URL for the component represented by this
        monitor instance.
        """
        if self.resource_beta == False:
            base_url = self.v1_url
        else:
            base_url = self.beta_url
        url = "%s/%s" % (base_url, self.resource_name)
        return url

    def get_entry_url(self, base_url, entry):
        """URL to an entry of this component.
        This method returns a URL to a specific entry of this component.
        """
        return base_url + entry['metadata']['selfLink']

    def init_monitor(self):
        """Initialize/sync a monitor component.
        This method will initialize a monitor component.
        As a part of this init, this method will read existing entries in api
        server and populate the local db.
        """
        # Get the URL to this component.
        url = self._get_component_url()

        # Read existing entries for this component, from api server.
        resp = self.kube_api_conn_handle.request('GET', url)
        if resp.status != 200:
            return

        initial_entries = json.loads(resp.data)['items']
        if initial_entries:
            for entry in initial_entries:
                entry_url = self.get_entry_url(self.url, entry)
                resp = self.kube_api_conn_handle.request('GET', entry_url)
                if resp.status != 200:
                    continue
                try:
                    # Construct the event and initiate processing.
                    event = {'object':json.loads(resp.data), 'type':'ADDED'}
                    self.process_event(event)
                except ValueError:
                    self.logger.error("Invalid data read from kube api server :"
                                      " %s" % (entry))

    def register_monitor(self):
        """Register this component for notifications from api server.
        """
        url = self._get_component_url()

        if self.cloud_orchestrator == "openshift":
            resp = requests.get(url, params={'watch': 'true'}, stream=True,
                                headers=self.headers, verify=False)
        else: # kubernetes
            resp = requests.get(url, params={'watch': 'true'}, stream=True)

        if resp.status_code != 200:
            return

        # Get handle to events for this monitor.
        self.kube_api_stream_handle = resp.iter_lines(chunk_size=10,
                                                    delimiter='\n')

    def get_resource(self, resource_type, resource_name, namespace=None, beta=False):
        if beta == False:
            base_url = self.url
        else:
            base_url = self.beta_url

        if resource_type == "namespaces":
            url = "%s/%s" % (base_url, resource_type)
        else:
            url = "%s/namespaces/%s/%s/%s" % (base_url, namespace,
                                              resource_type, resource_name)

        if self.cloud_orchestrator == "openshift":
            resp = requests.get(url, stream=True,
                                headers=self.headers, verify=False)
        else: # kubernetes
            resp = requests.get(url, stream=True)

        if resp.status_code != 200:
            return
        return json.loads(resp.raw.read())

    def patch_resource(self, resource_type, resource_name, merge_patch, namespace=None, beta=False):
        if beta == False:
            base_url = self.url
        else:
            base_url = self.beta_url

        if resource_type == "namespaces":
            url = "%s/%s" % (base_url, resource_type)
        else:
            url = "%s/namespaces/%s/%s/%s" % (base_url, namespace,
                                              resource_type, resource_name)

        self.headers.update({'Accept': 'application/json', 'Content-Type': 'application/strategic-merge-patch+json'})
        if self.cloud_orchestrator == "openshift":
            resp = requests.patch(url, headers=self.headers, data=json.dumps(merge_patch), verify=False)
        else: # kubernetes
            resp = requests.patch(url, headers=self.headers, data=json.dumps(merge_patch))

        if resp.status_code != 200:
            return
        return resp.iter_lines(chunk_size=10, delimiter='\n')

    def process(self):
        """Process available events."""
        if not self.kube_api_stream_handle:
            self.logger.error("Event handler not found for %s. "
                            "Cannot process its events." % (self.resource_name))
            return

        try:
            line = next(self.kube_api_stream_handle)
            if not line:
                return
        except StopIteration:
            return

        try:
            self.process_event(json.loads(line))
        except ValueError:
            print("Invalid JSON data from response stream:%s" % line)

    def process_event(self, event):
        """Process an event."""
        pass
