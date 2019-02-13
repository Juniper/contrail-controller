#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
from cStringIO import StringIO
import json
import socket
import time

import requests

from cfgm_common.utils import cgitb_hook


class KubeMonitor(object):

    def __init__(self, args=None, logger=None, q=None, db=None,
                 resource_name='KubeMonitor', beta=False, api_group=None,
                 api_version=None):
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

        self.kubernetes_api_server = self.args.kubernetes_api_server
        if self.token:
            protocol = "https"
            header = {'Authorization': "Bearer " + self.token}
            self.headers.update(header)
            self.verify = False
            self.kubernetes_api_server_port = \
                self.args.kubernetes_api_secure_port
        else:  # kubernetes
            protocol = "http"
            self.kubernetes_api_server_port = self.args.kubernetes_api_port

        # URL to the api server.
        self.url = "%s://%s:%s" % (protocol,
                                   self.kubernetes_api_server,
                                   self.kubernetes_api_server_port)

        # Get the base kubernetes url to use for this resource.
        # Each resouce can be independently configured to use difference
        # versions or api groups. So we let the resource class specify what
        # version and api group it is interested in. The base_url is constructed
        # with the input from the derived class and does not change for the
        # course of the process.
        self.base_url = self._get_base_url(self.url, beta, api_group,
                                           api_version)

        if not self._is_kube_api_server_alive():
            msg = "kube_api_service is not available"
            self.logger.error("%s - %s" % (self.name, msg))
            raise Exception(msg)

        self.logger.info("%s - KubeMonitor init done." % self.name)

    def _is_kube_api_server_alive(self, wait=False):

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        while True:
            result = sock.connect_ex((self.kubernetes_api_server,
                                      self.kubernetes_api_server_port))

            if wait == True and result != 0:
                # Connect to Kubernetes API server was not successful.
                # If requested, wait indefinitely till connection is up.

                msg = "kube_api_service is not reachable. Retry in %s secs." %\
                          (self.timeout)
                self.logger.error("%s - %s" %(self.name, msg))
                time.sleep(self.timeout)
                continue

            # Return result of connection attempt to kubernetes api server.
            return result == 0

    @classmethod
    def _get_base_url(cls, url, beta, api_group, api_version):
        ''' Construct a base url. '''
        if beta:
            # URL to v1-beta1 components to api server.
            version = api_version if api_version else "v1beta1"
            url = "/".join([url, "apis/extensions", version])
        else:
            """ Get the base URL for the resource. """
            version = api_version if api_version else "v1"
            group = api_group if api_group else "api"

            # URL to the v1-components in api server.
            url = "/".join([url, group, version])

        return url

    def get_component_url(self):
        """URL to a component.
        This method return the URL for the component represented by this
        monitor instance.
        """
        return "%s/%s" % (self.base_url, self.resource_name)

    @staticmethod
    def get_entry_url(base_url, entry):
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
        url = self.get_component_url()

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
                    resp = requests.get(entry_url, headers=self.headers,
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

        # Check if kubernetes api service is up. If not, wait till its up.
        self._is_kube_api_server_alive(wait=True)

        url = self.get_component_url()
        try:
            resp = requests.get(url, params={'watch': 'true'},
                                stream=True, headers=self.headers,
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

    def get_resource(self, resource_type, resource_name,
                     namespace=None, beta=False, api_group=None,
                     api_version=None):
        json_data = {}
        base_url = self._get_base_url(self.url, beta, api_group, api_version)

        if resource_type in ("namespaces", "customresourcedefinitions"):
            url = "%s/%s" % (base_url, resource_type)
        else:
            url = "%s/namespaces/%s/%s/%s" % (base_url, namespace,
                                              resource_type, resource_name)

        try:
            resp = requests.get(url, stream=True,
                                headers=self.headers, verify=self.verify)
            if resp.status_code == 200:
                json_data = json.loads(resp.raw.read())
            resp.close()
        except requests.exceptions.RequestException as e:
            self.logger.error("%s - %s" % (self.name, e))

        return json_data

    def patch_resource(
            self, resource_type, resource_name,
            merge_patch, namespace=None, beta=False, sub_resource_name=None,
            api_group=None, api_version=None):
        base_url = self._get_base_url(self.url, beta, api_group, api_version)

        if resource_type == "namespaces":
            url = "%s/%s" % (base_url, resource_type)
        else:
            url = "%s/namespaces/%s/%s/%s" % (base_url, namespace,
                                              resource_type, resource_name)
            if sub_resource_name:
                url = "%s/%s" %(url, sub_resource_name)

        headers = {'Accept': 'application/json',
                   'Content-Type': 'application/strategic-merge-patch+json'}
        headers.update(self.headers)

        try:
            resp = requests.patch(url, headers=headers,
                                  data=json.dumps(merge_patch),
                                  verify=self.verify)
            if resp.status_code != 200:
                resp.close()
                return
        except requests.exceptions.RequestException as e:
            self.logger.error("%s - %s" % (self.name, e))
            return

        return resp.iter_lines(chunk_size=10, delimiter='\n')

    def post_resource(
            self, resource_type, resource_name,
            body_params, namespace=None, beta=False, sub_resource_name=None,
            api_group=None, api_version=None):
        base_url = self._get_base_url(self.url, beta, api_group, api_version)

        if resource_type in ("namespaces", "customresourcedefinitions"):
            url = "%s/%s" % (base_url, resource_type)
        else:
            url = "%s/namespaces/%s/%s/%s" % (base_url, namespace,
                                              resource_type, resource_name)
            if sub_resource_name:
                url = "%s/%s" %(url, sub_resource_name)

        headers = {'Accept': 'application/json',
                   'Content-Type': 'application/json',
                   'Authorization': "Bearer " + self.token}
        headers.update(self.headers)

        try:
            resp = requests.post(url, headers=headers,
                                data=json.dumps(body_params),
                                verify=self.verify)
            if resp.status_code not in [200, 201]:
                resp.close()
                return
        except requests.exceptions.RequestException as e:
            self.logger.error("%s - %s" % (self.name, e))
            return
        return resp.iter_lines(chunk_size=10, delimiter='\n')

    def process(self):
        """Process available events."""
        if not self.kube_api_stream_handle:
            self.logger.error("%s - Event handler not found. "
                              "Cannot process its events. Re-registering event handler" % self.name)
            self.register_monitor()
            return

        resp = self.kube_api_resp
        fp = resp.raw._fp.fp
        if fp is None:
            self.logger.error("%s - Kube API Resp FP not found. "
                              "Cannot process events. Re-registering event handler" % self.name)
            self.register_monitor()
            return

        try:
            line = next(self.kube_api_stream_handle)
            if not line:
                return
        except StopIteration:
            return
        except requests.exceptions.ChunkedEncodingError as e:
            self.logger.error("%s - %s" % (self.name, e))
            return

        try:
            self.process_event(json.loads(line))
        except ValueError:
            self.logger.error(
                "Invalid JSON data from response stream:%s" % line)
        except Exception as e:
            string_buf = StringIO()
            cgitb_hook(file=string_buf, format="text")
            err_msg = string_buf.getvalue()
            self.logger.error("%s - %s" % (self.name, err_msg))

    def process_event(self, event):
        """Process an event."""
        pass
