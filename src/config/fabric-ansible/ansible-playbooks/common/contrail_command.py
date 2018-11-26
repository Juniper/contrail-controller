#!/usr/bin/python

import requests
from ironicclient import client as ironicclient
from urlparse import urlparse
from keystoneauth1.identity import v3
from keystoneauth1 import session
from keystoneclient.v3 import client
import ironic_inspector_client
import json
import time
import pprint


class CreateCCResource(object):

    auth_token = None
    auth_url = None

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
        'auth_port': '9091',
        'user_domain_name': 'default',
        'project_domain_name': 'default',
        'project_name': 'admin',
        'auth_path': "/keystone/v3"
    }

    auth_headers = {
        'Content-Type': 'application/json'
    }

    def __init__(self, auth_args):
        if not auth_args:
            return
        for key,val in auth_args.iteritems():
            if key in self.auth_args:
                self.auth_args[key] = val

        if not self.auth_args['auth_url']:
            self.auth_url = '%s://%s:%s%s' % (self.auth_args['auth_protocol'],
                                              self.auth_args['auth_host'],
                                              self.auth_args['auth_port'],
                                              self.auth_args[
                                                  'auth_path'])
        else:
            self.auth_url = self.auth_args['auth_url']

        self.set_ks_auth_sess()
        self.auth_token = self.keystone_session.get_token()

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


class CreateCCNode(CreateCCResource):

    node_obj = None
    port_obj = None
    node_list = []

    def __init__(self, auth_args):
        super(CreateCCNode, self).__init__(auth_args)

    def get_rest_api_response(self, url, headers, data=None, request_type=None):
        response = None
        print data
        if request_type == "post":
            response = requests.post(url, headers=headers, data=data,
                                     verify=False)
        elif request_type == "get":
            response = requests.get(url, headers=headers, data=data,
                                        verify=False)
        print response
        print response.content
        response.raise_for_status()
        return response

    def create_cc_node(self, node_payload):
        cc_url = '%s://%s:%s%s' % (self.auth_args['auth_protocol'],
                                   self.auth_args['auth_host'],
                                   self.auth_args['auth_port'],
                                   '/sync')
        self.auth_headers['X-Auth-Token'] = self.auth_token
        response = self.get_rest_api_response(cc_url,
                                              headers=self.auth_headers,
                                              data=json.dumps(node_payload),
                                              request_type="post")
        print response
        print response.content

    def get_cc_nodes(self ):
        cc_url = '%s://%s:%s%s' % (self.auth_args['auth_protocol'],
                                   self.auth_args['auth_host'],
                                   self.auth_args['auth_port'],
                                   '/nodes?detail=true')
        self.auth_headers['X-Auth-Token'] = self.auth_token

        response = self.get_rest_api_response(cc_url,
                                              headers=self.auth_headers,
                                              request_type="get")
        print response
        print response.content
        pprint.pprint(json.loads(response.content))
        return json.loads( response.content)

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
        print e.message

