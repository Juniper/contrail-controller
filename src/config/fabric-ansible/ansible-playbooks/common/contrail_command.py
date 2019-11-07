#!/usr/bin/python

from __future__ import print_function

from builtins import object
from future import standard_library
standard_library.install_aliases()  # noqa
import json
import pprint
from urllib.parse import urlparse

from keystoneauth1 import session
from keystoneauth1.identity import v3
from keystoneclient.v3 import client
import requests


class CreateCCResource(object):

    auth_token = None
    auth_url = None
    auth_uri = None

    keystone_auth = None
    keystone_session = None
    keystone_client = None

    # Default Auth Args
    auth_args = {
        'auth_url': None,
        'auth_host': None,
        'auth_protocol': 'https',
        'username': "",
        'password': "",
        'auth_port': '443',
        'user_domain_name': 'default',
        'project_domain_name': 'default',
        'project_name': 'admin',
        'auth_path': "/keystone/v3",
        'cluster_id': None,
        'cluster_token': None
    }

    auth_headers = {
        'Content-Type': 'application/json'
    }

    def __init__(self, auth_host,
                 cluster_id=None,
                 cluster_token=None,
                 cc_username=None,
                 cc_password=None,
                 auth_token=None):
        """Initialization.

        :param auth_host
        :param cluster_id
        :param cluster_token
        :param cc_username
        :param cc_password
        :param auth_token
        """
        if not auth_host:
            return
        auth_host_data = auth_host.split(':')
        self.auth_args['auth_host'] = auth_host_data[0]
        if len(auth_host_data) == 2:
            self.auth_args['auth_port'] = auth_host_data[1]

        if cluster_id:
            self.auth_args['cluster_id'] = cluster_id
        if cluster_token:
            self.auth_args['cluster_token'] = cluster_token
        if cc_username:
            self.auth_args['username'] = cc_username
        if cc_password:
            self.auth_args['password'] = cc_password
        if not self.auth_args['auth_url']:
            self.auth_url = '%s://%s:%s%s' % (self.auth_args['auth_protocol'],
                                              self.auth_args['auth_host'],
                                              self.auth_args['auth_port'],
                                              self.auth_args[
                                                  'auth_path'])
            self.auth_uri = '%s://%s:%s' % (self.auth_args['auth_protocol'],
                                            self.auth_args['auth_host'],
                                            self.auth_args['auth_port'])
        else:
            self.auth_url = self.auth_args['auth_url']
            parsed_auth_url = urlparse(self.auth_url)
            self.auth_args['auth_protocol'] = parsed_auth_url.scheme
            self.auth_args['auth_host'] = parsed_auth_url.hostname
            self.auth_args['auth_port'] = parsed_auth_url.port
            self.auth_args['auth_url_version'] = parsed_auth_url.path
            self.auth_uri = '%s://%s:%s' % (self.auth_args['auth_protocol'],
                                            self.auth_args['auth_host'],
                                            self.auth_args['auth_port'])

        if not auth_token and self.auth_args['username'] and \
                self.auth_args['password']:
            self.set_ks_auth_sess()
            self.auth_token = self.keystone_session.get_token()
        elif auth_token:
            self.auth_token = auth_token
        elif self.auth_args['cluster_token'] and self.auth_args['cluster_id']:
            self.auth_token = self.get_cc_token_via_cluster()

        self.auth_headers['X-Auth-Token'] = self.auth_token

    def get_cc_token_via_cluster(self):
        cc_token_url = '%s%s' % (self.auth_uri, '/keystone/v3/auth/tokens')
        auth_data = {
            "auth": {
                "identity": {
                    "methods": ["cluster_token"],
                    "cluster": {
                        "id": self.auth_args['cluster_id'],
                        "token": {
                            "id": self.auth_args['cluster_token']
                        }
                    }
                }
            }
        }
        response = self.get_rest_api_response(cc_token_url,
                                              headers=self.auth_headers,
                                              data=json.dumps(auth_data),
                                              request_type="post")

        if response.headers.get('X-Subject-Token'):
            cc_token = response.headers.get('X-Subject-Token')
        else:
            cc_token = ""

        return cc_token

    def set_ks_auth_sess(self):
        self.keystone_auth = v3.Password(
            auth_url=self.auth_url,
            username=self.auth_args['username'],
            password=self.auth_args['password'],
            project_name=self.auth_args['project_name'],
            user_domain_name=self.auth_args['user_domain_name'],
            project_domain_name=self.auth_args['project_domain_name']
        )
        self.keystone_session = session.Session(
            auth=self.keystone_auth,
            verify=False)
        self.keystone_client = client.Client(
            session=self.keystone_session)

    def get_rest_api_response(self, url, headers, data=None,
                              request_type=None):
        response = None
        print(data)
        if request_type == "post":
            response = requests.post(url, headers=headers, data=data,
                                     verify=False)
        elif request_type == "get":
            response = requests.get(url, headers=headers, data=data,
                                    verify=False)
        response.raise_for_status()
        return response

    def create_cc_resource(self, resource_payload):
        cc_url = '%s%s' % (self.auth_uri, '/sync')
        response = self.get_rest_api_response(cc_url,
                                              headers=self.auth_headers,
                                              data=json.dumps(
                                                  resource_payload),
                                              request_type="post")
        return response.content


class CreateCCNode(CreateCCResource):

    def __init__(self, auth_host,
                 cluster_id=None,
                 cluster_token=None,
                 cc_username=None,
                 cc_password=None):
        """Initialization.

        :param auth_host
        :param cluster_id
        :param cluster_token
        :param cc_username
        :param cc_password
        """
        super(CreateCCNode, self).__init__(auth_host,
                                           cluster_id,
                                           cluster_token,
                                           cc_username,
                                           cc_password)

    def create_cc_node(self, node_payload):

        response = self.create_cc_resource(node_payload)
        return response

    def get_cc_nodes(self):
        cc_url = '%s%s' % (self.auth_uri, '/nodes?detail=true')
        response = self.get_rest_api_response(cc_url,
                                              headers=self.auth_headers,
                                              request_type="get")
        pprint.pprint(json.loads(response.content))
        return json.loads(response.content)


class CreateCCNodeProfile(CreateCCResource):

    def __init__(self, auth_host,
                 cluster_id=None,
                 cluster_token=None,
                 cc_username=None,
                 cc_password=None):
        """Initialization.

        :param auth_host
        :param cluster_id
        :param cluster_token
        :param cc_username
        :param cc_password
        """
        super(CreateCCNodeProfile, self).__init__(auth_host,
                                                  cluster_id,
                                                  cluster_token,
                                                  cc_username,
                                                  cc_password)

    def create_cc_node_profile(self, node_profile_payload):
        response = self.create_cc_resource(node_profile_payload)
        return response

    def get_cc_node_profiles(self):
        cc_url = '%s%s' % (self.auth_uri, '/node-profiles?detail=true')
        response = self.get_rest_api_response(cc_url,
                                              headers=self.auth_headers,
                                              request_type="get")
        pprint.pprint(json.loads(response.content))
        return json.loads(response.content)


def main(cc_auth_args=None):
    return


if __name__ == '__main__':
    my_auth_args = {
        'auth_host': "1.1.1.1",
        'username': "admin",
        'password': "password"
    }
    try:
        main(cc_auth_args=my_auth_args)
    except Exception as e:
        print(e.message)
