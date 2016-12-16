import eventlet
import json
import sys
import requests
import os

class KubeMonitor(object):

    def __init__(self, args=None, logger=None, q=None, db=None):
        self.args = args
        self.logger = logger
        self.q = q
        self.cloud_orchestrator = os.getenv("CLOUD_ORCHESTRATOR")
        self.token = os.getenv("TOKEN") # valid only for OpenShift

        # Use Kube DB if kube object caching is enabled in config.
        if args.kube_object_cache == 'True':
            self.db = db
        else:
            self.db = None

        if self.cloud_orchestrator == "openshift":
            protocol = "https"
        else: # kubernetes
            protocol = "http"

        self.url = "%s://%s:%s/api/v1" % (protocol,
                                          self.args.kubernetes_api_server,
                                          self.args.kubernetes_api_port)
        self.beta_url = "%s://%s:%s/apis/extensions/v1beta1" % (
                                          protocol,
                                          self.args.kubernetes_api_server, 
                                          self.args.kubernetes_api_port)

        self.logger.info("KubeMonitor init done.");

    def register_monitor(self, resource_type, beta=False):
        if beta:
            url = "%s/%s" % (self.beta_url, resource_type)
        else:
            url = "%s/%s" % (self.url, resource_type)
        if self.cloud_orchestrator == "openshift":
            headers = {'Authorization': "Bearer " + self.token}
            resp = requests.get(url, params={'watch': 'true'}, stream=True, headers=headers, verify=False)
        else: # kubernetes
            resp = requests.get(url, params={'watch': 'true'}, stream=True)

        if resp.status_code != 200:
            return
        return resp.iter_lines(chunk_size=10, delimiter='\n')
