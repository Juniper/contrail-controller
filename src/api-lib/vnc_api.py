#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import logging
import requests
from requests.exceptions import ConnectionError

import ConfigParser
import pprint
import json
import sys
import time
import __main__ as main

import gen.resource_common
from gen.resource_xsd import *
from gen.resource_client import *
from gen.vnc_api_client_gen import VncApiClientGen

from cfgm_common import rest
from cfgm_common.exceptions import *

from pprint import pformat


def CamelCase(input):
    words = input.replace('_', '-').split('-')
    name = ''
    for w in words:
        name += w.capitalize()
    return name
#end CamelCase


def str_to_class(class_name):
    try:
        return reduce(getattr, class_name.split("."), sys.modules[__name__])
    except Exception as e:
        logger = logging.getLogger(__name__)
        logger.warn("Exception: %s", str(e))
        return None
#end str_to_class


def _read_cfg(cfg_parser, section, option, default):
        try:
            val = cfg_parser.get(section, option)
        except (AttributeError,
                ConfigParser.NoOptionError,
                ConfigParser.NoSectionError):
            val = default

        return val
#end _read_cfg


class VncApi(VncApiClientGen):
    _DEFAULT_WEB_SERVER = "127.0.0.1"

    _DEFAULT_HEADERS = {
        'Content-type': 'application/json; charset="UTF-8"',
        'X-Contrail-Useragent': getattr(main, '__file__', ''),
    }

    _AUTHN_SUPPORTED_TYPES = ["keystone"]
    _DEFAULT_AUTHN_TYPE = "keystone"
    _DEFAULT_AUTHN_HEADERS = _DEFAULT_HEADERS
    _DEFAULT_AUTHN_PROTOCOL = "http"
    _DEFAULT_AUTHN_SERVER = _DEFAULT_WEB_SERVER
    _DEFAULT_AUTHN_PORT = 35357
    _DEFAULT_AUTHN_URL = "/v2.0/tokens"
    _DEFAULT_AUTHN_USER = ""
    _DEFAULT_AUTHN_PASSWORD = ""
    _DEFAULT_AUTHN_TENANT = VncApiClientGen._tenant_name

    # Connection to api-server through Quantum
    _DEFAULT_WEB_PORT = 8082
    _DEFAULT_BASE_URL = "/"

    def __init__(self, username=None, password=None, tenant_name=None,
                 api_server_host='127.0.0.1', api_server_port='8082',
                 api_server_url=None, conf_file=None, user_info=None,
                 auth_token=None, auth_host=None, auth_port=None,
                 auth_protocol = None, auth_url=None):
        # TODO allow for username/password to be present in creds file

        super(VncApi, self).__init__(self._obj_serializer_diff)

        cfg_parser = ConfigParser.ConfigParser()
        clen = len(cfg_parser.read(conf_file or
                                   "/etc/contrail/vnc_api_lib.ini"))

        # keystone
        self._authn_type = _read_cfg(cfg_parser, 'auth', 'AUTHN_TYPE',
                                     self._DEFAULT_AUTHN_TYPE)
        if self._authn_type == 'keystone':
            self._authn_protocol = auth_protocol or \
                _read_cfg(cfg_parser, 'auth', 'AUTHN_PROTOCOL',
                                           self._DEFAULT_AUTHN_PROTOCOL)
            self._authn_server = auth_host or \
                _read_cfg(cfg_parser, 'auth', 'AUTHN_SERVER',
                                           self._DEFAULT_AUTHN_SERVER)
            self._authn_port = auth_port or \
                _read_cfg(cfg_parser, 'auth', 'AUTHN_PORT',
                                         self._DEFAULT_AUTHN_PORT)
            self._authn_url = auth_url or \
                _read_cfg(cfg_parser, 'auth', 'AUTHN_URL',
                                        self._DEFAULT_AUTHN_URL)
            self._username = username or \
                _read_cfg(cfg_parser, 'auth', 'AUTHN_USER',
                          self._DEFAULT_AUTHN_USER)
            self._password = password or \
                _read_cfg(cfg_parser, 'auth', 'AUTHN_PASSWORD',
                          self._DEFAULT_AUTHN_PASSWORD)
            self._tenant_name = tenant_name or \
                _read_cfg(cfg_parser, 'auth', 'AUTHN_TENANT',
                          self._DEFAULT_AUTHN_TENANT)
            self._authn_body = \
                '{"auth":{"passwordCredentials":{' + \
                '"username": "%s",' % (self._username) + \
                ' "password": "%s"},' % (self._password) + \
                ' "tenantName":"%s"}}' % (self._tenant_name)
            self._user_info = user_info

        if not api_server_host:
            self._web_host = _read_cfg(cfg_parser, 'global', 'WEB_SERVER',
                                       self._DEFAULT_WEB_SERVER)
        else:
            self._web_host = api_server_host

        if not api_server_port:
            self._web_port = _read_cfg(cfg_parser, 'global', 'WEB_PORT',
                                       self._DEFAULT_WEB_PORT)
        else:
            self._web_port = api_server_port

        # Where client's view of world begins
        if not api_server_url:
            self._base_url = _read_cfg(cfg_parser, 'global', 'BASE_URL',
                                       self._DEFAULT_BASE_URL)
        else:
            self._base_url = api_server_url

        # Where server says its root is when _base_url is fetched
        self._srv_root_url = None

        # Type-independent actions offered by server
        self._action_uri = {}

        self._headers = self._DEFAULT_HEADERS.copy()
        self._headers[rest.hdr_client_tenant()] = self._tenant_name

        self._auth_token_input = False

        if auth_token:
            self._auth_token = auth_token
            self._auth_token_input = True
            self._headers['X-AUTH-TOKEN'] = self._auth_token

        # user information for quantum
        if self._user_info:
            if 'user_id' in self._user_info:
                self._headers['X-API-USER-ID'] = self._user_info['user_id']
            if 'user' in self._user_info:
                self._headers['X-API-USER'] = self._user_info['user']
            if 'role' in self._user_info:
                self._headers['X-API-ROLE'] = self._user_info['role']

        #self._http = HTTPClient(self._web_host, self._web_port,
        #                        network_timeout = 300)

        self._create_api_server_session()

        homepage = self._request_server(rest.OP_GET, self._base_url,
                                        retry_on_error=False)
        self._cfg_root_url = self._parse_homepage(homepage)
    #end __init__

    def _obj_serializer_diff(self, obj):
        if hasattr(obj, 'serialize_to_json'):
            return obj.serialize_to_json(obj.get_pending_updates())
        else:
            return dict((k, v) for k, v in obj.__dict__.iteritems())
    #end _obj_serializer_diff

    def _obj_serializer_all(self, obj):
        if hasattr(obj, 'serialize_to_json'):
            return obj.serialize_to_json()
        else:
            return dict((k, v) for k, v in obj.__dict__.iteritems())
    #end _obj_serializer_all

    def _create_api_server_session(self):
        self._api_server_session = requests.Session()

        adapter = requests.adapters.HTTPAdapter(pool_connections=100,
                                                pool_maxsize=100)
        self._api_server_session.mount("http://", adapter)
        self._api_server_session.mount("https://", adapter)
    #end _create_api_server_session

    # Authenticate with configured service
    def _authenticate(self, response=None, headers=None):
        if self._authn_type is None:
            return headers
        url = "%s://%s:%s%s" % (self._authn_protocol, self._authn_server, self._authn_port,
                                  self._authn_url)
        try:
            response = requests.post(url, data=self._authn_body,
                                 headers=self._DEFAULT_AUTHN_HEADERS)
        except Exception as e:
            raise RuntimeError('Unable to connect to keystone for authentication. Verify keystone server details')

        if response.status_code == 200:
            # plan is to re-issue original request with new token
            new_headers = headers or {}
            authn_content = json.loads(response.text)
            self._auth_token = authn_content['access']['token']['id']
            new_headers['X-AUTH-TOKEN'] = self._auth_token
            return new_headers
        else:
            raise RuntimeError('Authentication Failure')
    #end _authenticate

    def _http_get(self, uri, headers=None, query_params=None):
        url = "http://%s:%s%s" \
              % (self._web_host, self._web_port, uri)
        response = self._api_server_session.get(url, headers=headers,
                                                params=query_params)
        #print 'Sending Request URL: ' + pformat(url)
        #print '                Headers: ' + pformat(headers)
        #print '                QParams: ' + pformat(query_params)
        #response = self._api_server_session.get(url, headers = headers,
        #                                        params = query_params)
        #print 'Received Response: ' + pformat(response.text)
        return (response.status_code, response.text)
    #end _http_get

    def _http_post(self, uri, body, headers):
        url = "http://%s:%s%s" \
              % (self._web_host, self._web_port, uri)
        response = self._api_server_session.post(url, data=body,
                                                 headers=headers)
        return (response.status_code, response.text)
    #end _http_post

    def _http_delete(self, uri, body, headers):
        url = "http://%s:%s%s" \
              % (self._web_host, self._web_port, uri)
        response = self._api_server_session.delete(url, data=body,
                                                   headers=headers)
        return (response.status_code, response.text)
    #end _http_delete

    def _http_put(self, uri, body, headers):
        url = "http://%s:%s%s" \
              % (self._web_host, self._web_port, uri)
        response = self._api_server_session.put(url, data=body,
                                                headers=headers)
        return (response.status_code, response.text)
    #end _http_delete

    def _parse_homepage(self, json_body):
        py_obj = json.loads(json_body)

        srv_root_url = py_obj['href']
        self._srv_root_url = srv_root_url

        for link in py_obj['links']:
            # strip base from *_url to get *_uri
            uri = link['link']['href'].replace(srv_root_url, '')
            if link['link']['rel'] == 'collection':
                class_name = "%s" % (CamelCase(link['link']['name']))
                cls = str_to_class(class_name)
                if not cls:
                    continue
                cls.create_uri = uri
            elif link['link']['rel'] == 'resource-base':
                class_name = "%s" % (CamelCase(link['link']['name']))
                cls = str_to_class(class_name)
                if not cls:
                    continue
                resource_type = link['link']['name']
                cls.resource_uri_base[resource_type] = uri
            elif link['link']['rel'] == 'action':
                act_type = link['link']['name']
                self._action_uri[act_type] = uri
    #end _parse_homepage

    def _find_url(self, json_body, resource_name):
        rname = unicode(resource_name)
        py_obj = json.loads(json_body)
        pprint.pprint(py_obj)
        for link in py_obj['links']:
            if link['link']['name'] == rname:
                return link['link']['href']

        return None
    #end _find_url

    def _read_args_to_id(self, obj_type, fq_name=None, fq_name_str=None,
                         id=None, ifmap_id=None):
        arg_count = ((fq_name is not None) + (fq_name_str is not None) +
                     (id is not None) + (ifmap_id is not None))

        if (arg_count == 0):
            return (False, "at least one of the arguments has to be provided")
        elif (arg_count > 1):
            return (False, "only one of the arguments should be provided")

        if id:
            return (True, id)
        if fq_name:
            return (True, self.fq_name_to_id(obj_type, fq_name))
        if fq_name_str:
            return (True, self.fq_name_to_id(obj_type, fq_name_str.split(':')))
        if ifmap_id:
            return (True, self.ifmap_to_id(ifmap_id))
    #end _read_args_to_id

    def _request_server(self, op, url, data=None, retry_on_error=True, retry_after_authn=False):
        while True:
            try:
                if (op == rest.OP_GET):
                    (status, content) = self._http_get(url, headers=self._headers,
                                                       query_params=data)
                elif (op == rest.OP_POST):
                    (status, content) = self._http_post(url, body=data,
                                                        headers=self._headers)
                elif (op == rest.OP_DELETE):
                    (status, content) = self._http_delete(url, body=data,
                                                          headers=self._headers)
                elif (op == rest.OP_PUT):
                    (status, content) = self._http_put(url, body=data,
                                                       headers=self._headers)
                else:
                    raise ValueError
            except ConnectionError:
                if not retry_on_error:
                    raise ConnectionError
     
                time.sleep(1)
                self._create_api_server_session()
                continue
     
            if status == 200:
                return content
     
            # Exception Response, see if it can be resolved
            if ((status == 401) and (not self._auth_token_input) and (not retry_after_authn)):
                self._headers = self._authenticate(content, self._headers)
                # Recursive call after authentication (max 1 level)
                content = self._request_server(op, url, data=data, retry_after_authn=True)
     
                return content
            elif status == 404:
                raise NoIdError('Error: oper %s url %s body %s response %s'
                                % (op, url, data, content))
            elif status == 403:
                raise PermissionDenied(content)
            elif status == 409:
                raise RefsExistError(content)
            elif status == 503 or status == 504:
                time.sleep(1)
                continue
            else:  # Unknown Error
                raise HttpError(status, content)
        # end while True

    #end _request_server

    def ref_update(self, obj_type, obj_uuid, ref_type, ref_uuid, ref_fq_name, operation, attr=None):
        if ref_type.endswith('_refs'):
            ref_type = ref_type[:-5].replace('_', '-')
        json_body = json.dumps({'type': obj_type, 'uuid': obj_uuid,
                                'ref-type': ref_type, 'ref-uuid': ref_uuid,
                                'ref-fq-name': ref_fq_name,
                                'operation': operation, 'attr': attr},
                               default=self._obj_serializer_diff)
        uri = self._action_uri['ref-update']
        try:
            content = self._request_server(rest.OP_POST, uri, data=json_body)
        except HttpError as he:
            if he.status_code == 404:
                return None
            raise he

        return json.loads(content)['uuid']
    #end ref_update

    def obj_to_id(self, obj):
        return self.fq_name_to_id(obj.get_type(), obj.get_fq_name())
    #end obj_to_id

    def fq_name_to_id(self, obj_type, fq_name):
        json_body = json.dumps({'type': obj_type, 'fq_name': fq_name})
        uri = self._action_uri['name-to-id']
        try:
            content = self._request_server(rest.OP_POST, uri, data=json_body)
        except HttpError as he:
            if he.status_code == 404:
                return None
            raise he

        return json.loads(content)['uuid']
    #end fq_name_to_id

    def id_to_fq_name(self, id):
        json_body = json.dumps({'uuid': id})
        uri = self._action_uri['id-to-name']
        content = self._request_server(rest.OP_POST, uri, data=json_body)

        return json.loads(content)['fq_name']
    #end id_to_fq_name

    def id_to_fq_name_type(self, id):
        json_body = json.dumps({'uuid': id})
        uri = self._action_uri['id-to-name']
        content = self._request_server(rest.OP_POST, uri, data=json_body)

        json_rsp = json.loads(content)
        return (json_rsp['fq_name'], json_rsp['type'])

    # This is required only for helping ifmap-subscribers using rest publish
    def ifmap_to_id(self, ifmap_id):
        json_body = json.dumps({'ifmap_id': ifmap_id})
        uri = self._action_uri['ifmap-to-id']
        try:
            content = self._request_server(rest.OP_POST, uri, data=json_body)
        except HttpError as he:
            if he.status_code == 404:
                return None

        return json.loads(content)['uuid']
    #end ifmap_to_id

    def obj_to_json(self, obj):
        return json.dumps(obj, default=self._obj_serializer_all)
    # end obj_to_json

    def obj_to_dict(self, obj):
        return json.loads(self.obj_to_json(obj))
    # end obj_to_dict

    def fetch_records(self):
        json_body = json.dumps({'fetch_records': None})
        uri = self._action_uri['fetch-records']
        content = self._request_server(rest.OP_POST, uri, data=json_body)

        return json.loads(content)['results']
    #end fetch_records

    def restore_config(self, create, resource, json_body):
        class_name = "%s" % (CamelCase(resource))
        cls = str_to_class(class_name)
        if not cls:
            return None

        if create:
            uri = cls.create_uri
            content = self._request_server(rest.OP_POST, uri, data=json_body)
        else:
            obj_dict = json.loads(json_body)
            uri = cls.resource_uri_base[resource] + '/'
            uri += obj_dict[resource]['uuid']
            content = self._request_server(rest.OP_PUT, uri, data=json_body)

        return json.loads(content)
    #end restore_config

    def kv_store(self, key, value):
        # TODO move oper value to common
        json_body = json.dumps({'operation': 'STORE',
                                'key': key,
                                'value': value})
        uri = self._action_uri['useragent-keyvalue']
        self._request_server(rest.OP_POST, uri, data=json_body)
    #end kv_store

    def kv_retrieve(self, key=None):
        # if key is None, entire collection is retrieved, use with caution!
        # TODO move oper value to common
        json_body = json.dumps({'operation': 'RETRIEVE',
                                'key': key})
        uri = self._action_uri['useragent-keyvalue']
        content = self._request_server(rest.OP_POST, uri, data=json_body)

        return json.loads(content)['value']
    #end kv_retrieve

    def kv_delete(self, key):
        # TODO move oper value to common
        json_body = json.dumps({'operation': 'DELETE',
                                'key': key})
        uri = self._action_uri['useragent-keyvalue']
        self._request_server(rest.OP_POST, uri, data=json_body)
    #end kv_delete

    # reserve block of IP address from a VN
    # expected format {"subnet" : "2.1.1.0/24", "count" : 4}
    def virtual_network_ip_alloc(self, vnobj, count=1, subnet=None):
        json_body = json.dumps({'count': count, 'subnet': subnet})
        uri = self._action_uri['virtual-network-ip-alloc'] % vnobj.uuid
        content = self._request_server(rest.OP_POST, uri, data=json_body)
        return json.loads(content)['ip_addr']
    #end virtual_network_ip_alloc

    # free previously reserved block of IP address from a VN
    # Expected format "subnet" : "2.1.1.0/24",
    #                 "ip_addr" : ["2.1.1.239", "2.1.1.238"]
    def virtual_network_ip_free(self, vnobj, ip_list, subnet=None):
        json_body = json.dumps({'ip_addr': ip_list, 'subnet': subnet})
        uri = self._action_uri['virtual-network-ip-free'] % vnobj.uuid
        rv = self._request_server(rest.OP_POST, uri, data=json_body)
        return rv
    #end virtual_network_ip_free

    # return no of ip instances from a given VN/Subnet
    # Expected format "subne_list" : ["2.1.1.0/24", "2.2.2.0/24"]
    def virtual_network_subnet_ip_count(self, vnobj, subnet_list):
        json_body = json.dumps({'subnet_list': subnet_list})
        uri = self._action_uri['virtual-network-subnet-ip-count'] % vnobj.uuid
        rv = self._request_server(rest.OP_POST, uri, data=json_body)
        return rv
    #end virtual_network_subnet_ip_count

    def get_auth_token(self):
        if self._auth_token:
            return self._auth_token
        self._headers = self._authenticate(headers=self._headers)
        return self._auth_token

    #end get_auth_token

#end class VncApi
