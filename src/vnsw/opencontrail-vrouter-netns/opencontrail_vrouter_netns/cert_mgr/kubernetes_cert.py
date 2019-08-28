from __future__ import absolute_import
from future import standard_library
standard_library.install_aliases()
from builtins import object
import os
from six.moves import configparser
import logging
import exceptions

import requests
import json
import base64

from .tls import TLS

class KubernetesCert(object):

    def __init__(self, auth_conf=None):
        if auth_conf:
            self.auth_conf = auth_conf
        else:
            self.auth_conf = '/etc/contrail/contrail-lbaas-auth.conf'
        self.parse_args()

        self.headers = {'Connection': 'Keep-Alive'}
        self.verify = False
        if self.kubernetes_token:
            protocol = "https"
            header = {'Authorization': "Bearer " + self.kubernetes_token}
            self.headers.update(header)
            self.verify = False
            self.kubernetes_api_server_port = self.kubernetes_api_secure_port
        else:
            protocol = "http"
            self.kubernetes_api_server_port = self.kubernetes_api_port

        # URL to the api server.
        self.url = "%s://%s:%s" % (protocol,
                                   self.kubernetes_api_server,
                                   self.kubernetes_api_server_port)
        # URL to the v1-components in api server.
        self.v1_url = "%s/api/v1" % (self.url)
        # URL to v1-beta1 components to api server.
        self.beta_url = "%s/apis/extensions/v1beta1" % (self.url)

    def parse_args(self):
        config = configparser.SafeConfigParser()
        config.read(self.auth_conf)

        self.kubernetes_token = config.get('KUBERNETES', 'kubernetes_token')
        self.kubernetes_api_server = config.get('KUBERNETES', 'kubernetes_api_server')
        self.kubernetes_api_port = config.get('KUBERNETES', 'kubernetes_api_port')
        self.kubernetes_api_secure_port = config.get('KUBERNETES', 'kubernetes_api_secure_port')

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

    def get_tls_certificates(self, secret, ns_name):
        try:
            json = self.get_resource('secrets', secret, ns_name)
            data = json['data']
        except exceptions.Exception as e:
            msg = "Error in getting secrets %s - %s" %(ns_name - secret)
            logging.exception(msg)
            logging.error(e.__class__)
            logging.error(e.__doc__)
            logging.error(e.message)
            return None
        certificate = base64.b64decode(data['tls.crt'])
        private_key = base64.b64decode(data['tls.key'])
        intermediates = ''
        primary_cn = TLS.get_primary_cn(certificate)
        return TLS(
            primary_cn=primary_cn,
            private_key=private_key,
            certificate=certificate,
            intermediates=intermediates)

    def update_ssl_config(self, haproxy_config, dest_dir):
        updated_config = haproxy_config
        for line in haproxy_config.split('\n'):
            if 'ssl crt' in line:
                try:
                    items = [x for x in line.split(' ') if x.startswith('crt__')]
                except IndexError:
                    return None
                for item in items or []:
                    ns_name, secret = item[5:].split('__') # len('crt__')
                    tls = self.get_tls_certificates(secret, ns_name)
                    pem_file_name = tls.create_pem_file(dest_dir)
                    if pem_file_name is None:
                        return None
                    updated_config = updated_config.replace(item, 'crt ' + pem_file_name)

        return updated_config
