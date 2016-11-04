import eventlet
import json
import sys
import requests

class KubeMonitor(object):

    def __init__(self, args=None, logger=None, q=None):
        self.args = args
        self.logger = logger
        self.q = q
        self.url = "http://%s:%s/api/v1" % (self.args.kubernetes_api_server,
            self.args.kubernetes_api_port)
        self.beta_url = "http://%s:%s/apis/extensions/v1beta1" % (
            self.args.kubernetes_api_server, self.args.kubernetes_api_port)

    def register_monitor(self, resource_type, beta=False):
        if beta:
            url = "%s/%s" % (self.beta_url, resource_type)
        else:
            url = "%s/%s" % (self.url, resource_type)
        resp = requests.get(url, params={'watch': 'true'}, stream=True)
        if resp.status_code != 200:
            return
        return resp.iter_lines(chunk_size=10, delimiter='\n')
