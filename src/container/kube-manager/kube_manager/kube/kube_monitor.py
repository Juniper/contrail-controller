#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import sys
import os
import time
import socket
import select
import eventlet
import json
import requests

from cStringIO import StringIO
from cfgm_common.utils import cgitb_hook

class KubeMonitor(object):

    def __init__(self, args=None, logger=None, q=None, db=None,
                 resource_name='KubeMonitor', beta=False):
        self.name = type(self).__name__
        self.args = args
        self.logger = logger
        self.q = q
        self.cloud_orchestrator = self.args.orchestrator
        self.token = self.args.token # valid only for OpenShift
        self.headers = {'Connection': 'Keep-Alive'}
        self.verify = False
        self.timeout = 60

        # Per-monitor stream handle to api server.
        self.kube_api_resp = None
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
            header = {'Authorization': "Bearer " + self.token}
            self.headers.update(header)
            self.verify = False
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

        if not self._is_kube_api_server_alive():
            msg = "kube_api_service is not available"
            self.logger.error("%s - %s" %(self.name, msg))
            raise Exception(msg)

        self.logger.info("%s - KubeMonitor init done." %self.name)

    def _is_kube_api_server_alive(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        result = sock.connect_ex((self.args.kubernetes_api_server, \
                                  self.args.kubernetes_api_port))
        if result == 0:
            return True
        else:
            return False

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

        try:
            resp = requests.get(url, headers=self.headers, verify=self.verify)
            if resp.status_code != 200:
                resp.close()
                return
        except requests.exceptions.RequestException as e:
            self.logger.error("%s - %s" % (self.name, e))
            return

        initial_entries = resp.json()['items']
        resp.close()
        if initial_entries:
            for entry in initial_entries:
                entry_url = self.get_entry_url(self.url, entry)
                try:
                    resp = requests.get(entry_url, headers=self.headers, \
                                        verify=self.verify)
                    if resp.status_code != 200:
                        resp.close()
                        continue
                except requests.exceptions.RequestException as e:
                    self.logger.error("%s - %s" % (self.name, e))
                    continue
                try:
                    # Construct the event and initiate processing.
                    event = {'object':resp.json(), 'type':'ADDED'}
                    self.process_event(event)
                except ValueError:
                    self.logger.error("Invalid data read from kube api server:"
                                      " %s" % (entry))
                except Exception as e:
                    string_buf = StringIO()
                    cgitb_hook(file=string_buf, format="text")
                    err_msg = string_buf.getvalue()
                    self.logger.error("%s - %s" %(self.name, err_msg))

                resp.close()

    def register_monitor(self):
        """Register this component for notifications from api server.
        """
        if self.kube_api_resp:
            self.kube_api_resp.close()
        if not self._is_kube_api_server_alive():
            msg = "kube_api_service is not available"
            self.logger.error("%s - %s" %(self.name, msg))
            time.sleep(self.timeout)
            return

        url = self._get_component_url()
        try:
            resp = requests.get(url, params={'watch': 'true'}, \
                                stream=True, headers=self.headers, \
                                verify=self.verify)
            if resp.status_code != 200:
                resp.close()
                return
            # Get handle to events for this monitor.
            self.kube_api_resp = resp
            self.kube_api_stream_handle = resp.iter_lines(chunk_size=256,
                                                          delimiter='\n')
            self.logger.info("%s - Watches %s" %(self.name, url))
        except requests.exceptions.RequestException as e:
            self.logger.error("%s - %s" % (self.name, e))

    def get_resource(self, resource_type, resource_name, \
                     namespace=None, beta=False):
        json_data = {}
        if beta == False:
            base_url = self.v1_url
        else:
            base_url = self.beta_url

        if resource_type == "namespaces":
            url = "%s/%s" % (base_url, resource_type)
        else:
            url = "%s/namespaces/%s/%s/%s" % (base_url, namespace,
                                              resource_type, resource_name)
        try:
            resp = requests.get(url, stream=True, \
                                headers=self.headers, verify=self.verify)
            if resp.status_code == 200:
                json_data = json.loads(resp.raw.read())
            resp.close()
        except requests.exceptions.RequestException as e:
            self.logger.error("%s - %s" % (self.name, e))

        return json_data

    def patch_resource(self, resource_type, resource_name, \
                       merge_patch, namespace=None, beta=False):
        if beta == False:
            base_url = self.v1_url
        else:
            base_url = self.beta_url

        if resource_type == "namespaces":
            url = "%s/%s" % (base_url, resource_type)
        else:
            url = "%s/namespaces/%s/%s/%s" % (base_url, namespace,
                                              resource_type, resource_name)

        headers = {'Accept': 'application/json', \
                   'Content-Type': 'application/strategic-merge-patch+json'}
        headers.update(self.headers)

        try:
            resp = requests.patch(url, headers=headers, \
                                  data=json.dumps(merge_patch), \
                                  verify=self.verify)
            if resp.status_code != 200:
                resp.close()
                return
        except requests.exceptions.RequestException as e:
            self.logger.error("%s - %s" % (self.name, e))

        return resp.iter_lines(chunk_size=10, delimiter='\n')

    def process(self):
        """Process available events."""
        if not self.kube_api_stream_handle:
            self.logger.error("%s - Event handler not found. "
                              "Cannot process its events." % (self.name))
            return

        resp = self.kube_api_resp
        fp = resp.raw._fp.fp
        if fp is None:
            self.register_monitor()
            return

        try:
            line = next(self.kube_api_stream_handle)
            if not line:
                return
        except StopIteration:
            return
        except requests.exceptions.ChunkedEncodingError as e:
            self.logger.error("%s - %s" %(self.name, e))
            return

        try:
            self.process_event(json.loads(line))
        except ValueError:
            self.logger.error("Invalid JSON data from response stream:%s" % line)
        except Exception as e:
            string_buf = StringIO()
            cgitb_hook(file=string_buf, format="text")
            err_msg = string_buf.getvalue()
            self.logger.error("%s - %s" %(self.name, err_msg))

    def process_event(self, event):
        """Process an event."""
        pass
