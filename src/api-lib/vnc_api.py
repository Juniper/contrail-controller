#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import logging
import requests
from requests.exceptions import ConnectionError

import ConfigParser
import pprint
from cfgm_common import jsonutils as json
import sys
import time
import platform
import functools
import __main__ as main
import ssl
import re
import os

import gen.resource_common
import gen.vnc_api_client_gen
from gen.vnc_api_client_gen import all_resource_type_tuples
from gen.resource_xsd import *
from gen.resource_client import *
from gen.generatedssuper import GeneratedsSuper

import cfgm_common
from cfgm_common import rest, utils
from cfgm_common.exceptions import *
from cfgm_common import ssl_adapter

from pprint import pformat


def check_homepage(func):
    @functools.wraps(func)
    def wrapper(self, *args, **kwargs):
        if not self._srv_root_url:
            homepage = self._request(rest.OP_GET, self._base_url,
                                    retry_on_error=False)
            self._parse_homepage(homepage)
        return func(self, *args, **kwargs)
    return wrapper

def compare_refs(old_refs, new_refs):
    # compare refs in an object
    old_ref_dict = dict((':'.join(ref['to']), ref['attr']) for ref in old_refs or [])
    new_ref_dict = dict((':'.join(ref['to']), ref['attr']) for ref in new_refs or [])
    return old_ref_dict == new_ref_dict
# end compare_refs

def get_object_class(res_type):
    cls_name = '%s' %(utils.CamelCase(res_type))
    return utils.str_to_class(cls_name, __name__)
# end get_object_class

def _read_cfg(cfg_parser, section, option, default):
        try:
            val = cfg_parser.get(section, option)
        except (AttributeError,
                ConfigParser.NoOptionError,
                ConfigParser.NoSectionError):
            val = default

        return val
#end _read_cfg

def _cfg_curl_logging(api_curl_log_file=None):
    formatter = logging.Formatter('%(asctime)s %(levelname)-8s %(message)s',datefmt='%Y/%m/%d %H:%M:%S')
    curl_logger = logging.getLogger('log_curl')
    curl_logger.setLevel(logging.DEBUG)
    api_curl_log_file="/var/log/contrail/%s" %(api_curl_log_file)
    if os.path.exists(api_curl_log_file):
        curl_log_handler = logging.FileHandler(api_curl_log_file,mode='a')
    else:
        curl_log_handler = logging.FileHandler(api_curl_log_file,mode='w')
    curl_log_handler.setFormatter(formatter)
    curl_logger.addHandler(curl_log_handler)
    return curl_logger
# End _cfg_curl_logging

class ActionUriDict(dict):
    """Action uri dictionary with operator([]) overloading to parse home page
       and populate the action_uri, if not populated already.
    """
    def __init__(self, vnc_api,  *args, **kwargs):
        dict.__init__(self, args, **kwargs)
        self.vnc_api = vnc_api

    def __getitem__(self, key):
        try:
            return dict.__getitem__(self, key)
        except KeyError:
            homepage = self.vnc_api._request(rest.OP_GET, self.vnc_api._base_url,
                                    retry_on_error=False)
            self.vnc_api._parse_homepage(homepage)
            return dict.__getitem__(self, key)


class VncApi(object):
    _DEFAULT_WEB_SERVER = "127.0.0.1"

    hostname = platform.node()
    _DEFAULT_HEADERS = {
        'Content-type': 'application/json; charset="UTF-8"',
        'X-Contrail-Useragent': '%s:%s'
             %(hostname, getattr(main, '__file__', '')),
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
    _DEFAULT_AUTHN_TENANT = 'default-tenant'
    _DEFAULT_DOMAIN_ID = "default"

    # Keystone and and vnc-api SSL support
    # contrail-api will remain to be on http
    # with LB (haproxy/F5/nginx..etc) configured for
    # ssl termination on port 8082(default contrail-api port)
    _DEFAULT_API_SERVER_CONNECT="http"
    _DEFAULT_API_SERVER_SSL_CONNECT="https"
    _DEFAULT_KS_CERT_BUNDLE="keystonecertbundle.pem"
    _DEFAULT_API_CERT_BUNDLE="apiservercertbundle.pem"

    # Connection to api-server through Quantum
    _DEFAULT_WEB_PORT = 8082
    _DEFAULT_BASE_URL = "/"

    # The number of items beyond which instead of GET /<collection>
    # a POST /list-bulk-collection is issued
    POST_FOR_LIST_THRESHOLD = 25

    # Number of pools and number of pool per conn to api-server
    _DEFAULT_MAX_POOLS = 100
    _DEFAULT_MAX_CONNS_PER_POOL = 100

    def __init__(self, username=None, password=None, tenant_name=None,
                 api_server_host=None, api_server_port=None,
                 api_server_url=None, conf_file=None, user_info=None,
                 auth_token=None, auth_host=None, auth_port=None,
                 auth_protocol = None, auth_url=None, auth_type=None,
                 wait_for_connect=False, api_server_use_ssl=False,
                 domain_name=None, exclude_hrefs=None, auth_token_url=None,
                 apicertfile=None, apikeyfile=None, apicafile=None,
                 kscertfile=None, kskeyfile=None, kscafile=None,):
        # TODO allow for username/password to be present in creds file

        self._obj_serializer = self._obj_serializer_diff
        for object_type, resource_type in all_resource_type_tuples:
            for oper_str in ('_create', '_read', '_update', '_delete',
                         's_list', '_get_default_id'):
                method = getattr(self, '_object%s' %(oper_str))
                bound_method = functools.partial(method, resource_type)
                functools.update_wrapper(bound_method, method)
                if oper_str == '_get_default_id':
                    setattr(self, 'get_default_%s_id' % (object_type),
                            bound_method)
                else:
                    setattr(self, '%s%s' %(object_type, oper_str),
                            bound_method)

        cfg_parser = ConfigParser.ConfigParser()
        try:
            cfg_parser.read(conf_file or
                            "/etc/contrail/vnc_api_lib.ini")
        except Exception as e:
            logger = logging.getLogger(__name__)
            logger.warn("Exception: %s", str(e))

        self._api_connect_protocol = VncApi._DEFAULT_API_SERVER_CONNECT
        # API server SSL Support
        use_ssl = api_server_use_ssl
        if isinstance(api_server_use_ssl, basestring):
           use_ssl = (api_server_use_ssl.lower() == 'true')
        if use_ssl:
             self._api_connect_protocol = VncApi._DEFAULT_API_SERVER_SSL_CONNECT

        if not api_server_host:
            self._web_host = _read_cfg(cfg_parser, 'global', 'WEB_SERVER',
                                       self._DEFAULT_WEB_SERVER)
        else:
            self._web_host = api_server_host

        # keystone
        self._authn_type = auth_type or \
            _read_cfg(cfg_parser, 'auth', 'AUTHN_TYPE',
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
            self._domain_name = domain_name or \
                _read_cfg(cfg_parser, 'auth', 'AUTHN_DOMAIN',
                          self._DEFAULT_DOMAIN_ID)
            self._authn_token_url = auth_token_url or \
                _read_cfg(cfg_parser, 'auth', 'AUTHN_TOKEN_URL', None)

            #contrail-api SSL support
            try:
               self._apiinsecure = cfg_parser.getboolean('global','insecure')
            except (AttributeError,
                    ValueError,
                    ConfigParser.NoOptionError,
                    ConfigParser.NoSectionError):
               self._apiinsecure = False
            apicertfile = (apicertfile or
                           _read_cfg(cfg_parser,'global','certfile',''))
            apikeyfile = (apikeyfile or
                          _read_cfg(cfg_parser,'global','keyfile',''))
            apicafile = (apicafile or
                         _read_cfg(cfg_parser,'global','cafile',''))

            self._use_api_certs=False
            if apicafile and api_server_use_ssl:
                certs=[apicafile]
                if apikeyfile and apicertfile:
                    certs=[apicertfile, apikeyfile, apicafile]
                apicertbundle = os.path.join(
                    '/tmp', self._web_host.replace('.', '_'),
                     VncApi._DEFAULT_API_CERT_BUNDLE)
                self._apicertbundle=utils.getCertKeyCaBundle(apicertbundle,
                                                             certs)
                self._use_api_certs=True

            # keystone SSL support
            try:
              self._ksinsecure = cfg_parser.getboolean('auth', 'insecure')
            except (AttributeError,
                    ValueError,
                    ConfigParser.NoOptionError,
                    ConfigParser.NoSectionError):
              self._ksinsecure = False
            kscertfile = (kscertfile or
                          _read_cfg(cfg_parser,'auth','certfile',''))
            kskeyfile = (kskeyfile or
                         _read_cfg(cfg_parser,'auth','keyfile',''))
            kscafile = (kscafile or
                        _read_cfg(cfg_parser,'auth','cafile',''))

            self._use_ks_certs=False
            if kscafile and self._authn_protocol == 'https':
                certs=[kscafile]
                if kskeyfile and kscertfile:
                    certs=[kscertfile, kskeyfile, kscafile]
                kscertbundle = os.path.join(
                        '/tmp', self._web_host.replace('.', '_'),
                        VncApi._DEFAULT_KS_CERT_BUNDLE)
                self._kscertbundle=utils.getCertKeyCaBundle(kscertbundle,
                                                            certs)
                self._use_ks_certs=True

            if 'v2' in self._authn_url:
                self._authn_body = \
                    '{"auth":{"passwordCredentials":{' + \
                    '"username": "%s",' % (self._username) + \
                    ' "password": "%s"},' % (self._password) + \
                    ' "tenantName":"%s"}}' % (self._tenant_name)
            else:
                self._authn_body = \
                     '{"auth":{"identity":{' + \
                        '"methods": ["password"],' + \
                          ' "password":{' + \
                            ' "user":{' + \
                               ' "name": "%s",' % (self._username) + \
                               ' "domain": { "name": "%s" },' % (self._domain_name) + \
                               ' "password": "%s"' % (self._password) + \
                             '}' + \
                            '}' + \
                          '},' + \
                          ' "scope":{' + \
                            ' "project":{' + \
                              ' "domain": { "name": "%s" },' % (self._domain_name) + \
                              ' "name": "%s"' % (self._tenant_name) + \
                            '}' + \
                          '}' + \
                        '}' + \
                     '}'
            self._user_info = user_info

        if not api_server_port:
            self._web_port = _read_cfg(cfg_parser, 'global', 'WEB_PORT',
                                       self._DEFAULT_WEB_PORT)
        else:
            self._web_port = api_server_port

        self._max_pools = int(_read_cfg(
            cfg_parser, 'global', 'MAX_POOLS',
            self._DEFAULT_MAX_POOLS))
        self._max_conns_per_pool = int(_read_cfg(
            cfg_parser, 'global', 'MAX_CONNS_PER_POOL',
            self._DEFAULT_MAX_CONNS_PER_POOL))

        self._curl_logging = False
        if _read_cfg(cfg_parser, 'global', 'curl_log', False):
            self._curl_logging=True
            self._curl_logger=_cfg_curl_logging(_read_cfg(cfg_parser,'global','curl_log',False))

        # Where client's view of world begins
        if not api_server_url:
            self._base_url = _read_cfg(cfg_parser, 'global', 'BASE_URL',
                                       self._DEFAULT_BASE_URL)
        else:
            self._base_url = api_server_url

        # Where server says its root is when _base_url is fetched
        self._srv_root_url = None

        # Type-independent actions offered by server
        self._action_uri = ActionUriDict(self)

        self._headers = self._DEFAULT_HEADERS.copy()
        self._headers[rest.hdr_client_tenant()] = self._tenant_name

        self._auth_token_input = False
        self._auth_token = None

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
                self.set_user_roles([self._user_info['role']])

        self._exclude_hrefs = exclude_hrefs

        self._create_api_server_session()

        retry_count = 6
        while retry_count:
            try:
                homepage = self._request(rest.OP_GET, self._base_url,
                                         retry_on_error=False)
                self._parse_homepage(homepage)
            except ServiceUnavailableError as e:
                logger = logging.getLogger(__name__)
                logger.warn("Exception: %s", str(e))
                if wait_for_connect:
                    # Retry connect infinitely when http retcode 503
                    continue
                elif retry_count:
                    # Retry connect 60 times when http retcode 503
                    retry_count -= 1
                    time.sleep(1)
            else:
                # connected succesfully
                break
    #end __init__

    @check_homepage
    def _object_create(self, res_type, obj):
        obj_cls = get_object_class(res_type)

        obj._pending_field_updates |= obj._pending_ref_updates
        obj._pending_ref_updates = set([])
        # Ignore fields with None value in json representation
        # encode props + refs in object body
        obj_json_param = json.dumps(obj, default=self._obj_serializer)

        json_body = '{"%s":%s}' %(res_type, obj_json_param)
        content = self._request_server(rest.OP_POST,
                       obj_cls.create_uri,
                       data=json_body)

        obj_dict = json.loads(content)[res_type]
        obj.uuid = obj_dict['uuid']
        if 'parent_uuid' in obj_dict:
            obj.parent_uuid = obj_dict['parent_uuid']

        obj.set_server_conn(self)

        # encode any prop-<list|map> operations and
        # POST on /prop-collection-update
        prop_coll_body = {'uuid': obj.uuid,
                          'updates': []}

        operations = []
        for prop_name in obj._pending_field_list_updates:
            operations.extend(obj._pending_field_list_updates[prop_name])
        for prop_name in obj._pending_field_map_updates:
            operations.extend(obj._pending_field_map_updates[prop_name])

        for oper, elem_val, elem_pos in operations:
            if isinstance(elem_val, GeneratedsSuper):
                serialized_elem_value = elem_val.exportDict('')
            else:
                serialized_elem_value = elem_val

            prop_coll_body['updates'].append(
                {'field': prop_name, 'operation': oper,
                 'value': serialized_elem_value, 'position': elem_pos})

        # all pending fields picked up
        obj.clear_pending_updates()

        if prop_coll_body['updates']:
            prop_coll_json = json.dumps(prop_coll_body)
            self._request_server(rest.OP_POST,
                self._action_uri['prop-collection-update'],
                data=prop_coll_json)

        return obj.uuid
    # end _object_create

    @check_homepage
    def _object_read(self, res_type, fq_name=None, fq_name_str=None,
                     id=None, ifmap_id=None, fields=None):
        obj_cls = get_object_class(res_type)

        (args_ok, result) = self._read_args_to_id(
            res_type, fq_name, fq_name_str, id, ifmap_id)
        if not args_ok:
            return result

        id = result
        uri = obj_cls.resource_uri_base[res_type] + '/' + id

        if fields:
            comma_sep_fields = ','.join(f for f in fields)
            query_params = {'fields': comma_sep_fields}
        else:
            query_params = {'exclude_back_refs':True,
                            'exclude_children':True,}

        if self._exclude_hrefs is not None:
            query_params['exclude_hrefs'] = True

        response = self._request_server(rest.OP_GET, uri, query_params)

        obj_dict = response[res_type]
        obj = obj_cls.from_dict(**obj_dict)
        obj.clear_pending_updates()
        obj.set_server_conn(self)

        return obj
    # end _object_read

    @check_homepage
    def _object_update(self, res_type, obj):
        obj_cls = get_object_class(res_type)

        # Read in uuid from api-server if not specified in obj
        if not obj.uuid:
            obj.uuid = self.fq_name_to_id(res_type, obj.get_fq_name())

        # Generate PUT on object only if some attr was modified
        content = None
        if obj.get_pending_updates():
            # Ignore fields with None value in json representation
            obj_json_param = json.dumps(obj, default=self._obj_serializer)
            if obj_json_param:
                json_body = '{"%s":%s}' %(res_type, obj_json_param)
                uri = obj_cls.resource_uri_base[res_type] + '/' + obj.uuid
                content = self._request_server(rest.OP_PUT, uri, data=json_body)

        # Generate POST on /prop-collection-update if needed/pending
        prop_coll_body = {'uuid': obj.uuid,
                          'updates': []}

        operations = []
        for prop_name in obj._pending_field_list_updates:
            operations.extend(obj._pending_field_list_updates[prop_name])
        for prop_name in obj._pending_field_map_updates:
            operations.extend(obj._pending_field_map_updates[prop_name])

        for oper, elem_val, elem_pos in operations:
            if isinstance(elem_val, GeneratedsSuper):
                serialized_elem_value = elem_val.exportDict('')
            else:
                serialized_elem_value = elem_val

            prop_coll_body['updates'].append(
                {'field': prop_name, 'operation': oper,
                 'value': serialized_elem_value, 'position': elem_pos})

        if prop_coll_body['updates']:
            prop_coll_json = json.dumps(prop_coll_body)
            self._request_server(rest.OP_POST,
                self._action_uri['prop-collection-update'],
                data=prop_coll_json)

        # Generate POST on /ref-update if needed/pending
        for ref_name in obj._pending_ref_updates:
             ref_orig = set([(x.get('uuid'),
                             tuple(x.get('to', [])), x.get('attr'))
                        for x in getattr(obj, '_original_' + ref_name, [])])
             ref_new = set([(x.get('uuid'),
                            tuple(x.get('to', [])), x.get('attr'))
                       for x in getattr(obj, ref_name, [])])
             for ref in ref_orig - ref_new:
                 self.ref_update(res_type, obj.uuid, ref_name, ref[0],
                                 list(ref[1]), 'DELETE')
             for ref in ref_new - ref_orig:
                 self.ref_update(res_type, obj.uuid, ref_name, ref[0],
                                 list(ref[1]), 'ADD', ref[2])
        obj.clear_pending_updates()

        return content
    # end _object_update

    @check_homepage
    def _objects_list(self, res_type, parent_id=None, parent_fq_name=None,
                     obj_uuids=None, back_ref_id=None, fields=None,
                     detail=False, count=False, filters=None):
        return self.resource_list(res_type, parent_id=parent_id,
            parent_fq_name=parent_fq_name, back_ref_id=back_ref_id,
            obj_uuids=obj_uuids, fields=fields, detail=detail, count=count,
            filters=filters)
    # end _objects_list

    @check_homepage
    def _object_delete(self, res_type, fq_name=None, id=None, ifmap_id=None):
        obj_cls = get_object_class(res_type)

        (args_ok, result) = self._read_args_to_id(
            res_type=res_type, fq_name=fq_name, id=id, ifmap_id=ifmap_id)
        if not args_ok:
            return result

        id = result
        uri = obj_cls.resource_uri_base[res_type] + '/' + id

        content = self._request_server(rest.OP_DELETE, uri)
    # end _object_delete

    def _object_get_default_id(self, res_type):
        obj_cls = get_object_class(res_type)

        return self.fq_name_to_id(res_type, obj_cls().get_fq_name())
    # end _object_get_default_id

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

        adapter = requests.adapters.HTTPAdapter(
            pool_connections=self._max_conns_per_pool,
            pool_maxsize=self._max_pools)
        ssladapter = ssl_adapter.SSLAdapter(ssl.PROTOCOL_SSLv23)
        ssladapter.init_poolmanager(
            connections=self._max_conns_per_pool,
            maxsize=self._max_pools)
        self._api_server_session.mount("http://", adapter)
        self._api_server_session.mount("https://", ssladapter)
    #end _create_api_server_session

    # Authenticate with configured service
    def _authenticate(self, response=None, headers=None):
        if self._authn_type is None:
            return headers

        if self._authn_token_url:
            url = self._authn_token_url
        else:
            url = "%s://%s:%s%s" % (self._authn_protocol, self._authn_server, self._authn_port,
                                  self._authn_url)
        new_headers = headers or {}
        try:
           if self._ksinsecure:
                response = requests.post(url, data=self._authn_body,
                                     headers=self._DEFAULT_AUTHN_HEADERS, verify=False)
           elif not self._ksinsecure and self._use_ks_certs:
                response = requests.post(url, data=self._authn_body,
                                         headers=self._DEFAULT_AUTHN_HEADERS, verify=self._kscertbundle)
           else:
                response = requests.post(url, data=self._authn_body,
                                         headers=self._DEFAULT_AUTHN_HEADERS)
        except Exception as e:
            errmsg = 'Unable to connect to keystone for authentication. '
            errmsg += 'Exception %s' %(e)
            raise RuntimeError(errmsg)

        if (response.status_code == 200) or (response.status_code == 201):
            # plan is to re-issue original request with new token
            if 'v2' in self._authn_url:
                authn_content = json.loads(response.text)
                self._auth_token = authn_content['access']['token']['id']
            else:
                self._auth_token = response.headers['x-subject-token']
            new_headers['X-AUTH-TOKEN'] = self._auth_token
            return new_headers
        else:
            raise RuntimeError('Authentication Failure')
    #end _authenticate

    def _http_get(self, uri, headers=None, query_params=None):
        url = "%s://%s:%s%s" \
              % (self._api_connect_protocol,self._web_host, self._web_port, uri)
        if self._apiinsecure:
             response = self._api_server_session.get(url, headers=headers,
                                                      params=query_params,verify=False)
        elif not self._apiinsecure and self._use_api_certs:
             response = self._api_server_session.get(url, headers=headers,
                                                     params=query_params,verify=self._apicertbundle)
        else:
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
        url = "%s://%s:%s%s" \
              % (self._api_connect_protocol,self._web_host, self._web_port, uri)
        if self._apiinsecure:
             response = self._api_server_session.post(url, data=body,
                                                     headers=headers, verify=False)
        elif not self._apiinsecure and self._use_api_certs:
             response = self._api_server_session.post(url, data=body,
                                                      headers=headers, verify=self._apicertbundle)
        else:
             response = self._api_server_session.post(url, data=body,
                                                      headers=headers)
        return (response.status_code, response.text)
    #end _http_post

    def _http_delete(self, uri, body, headers):
        url = "%s://%s:%s%s" \
              % (self._api_connect_protocol,self._web_host, self._web_port, uri)
        if self._apiinsecure:
            response = self._api_server_session.delete(url, data=body,
                                                   headers=headers, verify=False)
        elif not self._apiinsecure and self._use_api_certs:
            response = self._api_server_session.delete(url, data=body,
                                                       headers=headers, verify=self._apicertbundle)
        else:
            response = self._api_server_session.delete(url, data=body,
                                                       headers=headers)
        return (response.status_code, response.text)
    #end _http_delete

    def _http_put(self, uri, body, headers):
        url = "%s://%s:%s%s" \
              % (self._api_connect_protocol,self._web_host, self._web_port, uri)
        if self._apiinsecure:
             response = self._api_server_session.put(url, data=body,
                                                headers=headers, verify=False)
        elif not self._apiinsecure and self._use_api_certs:
             response = self._api_server_session.put(url, data=body,
                                                     headers=headers, verify=self._apicertbundle)
        else:
             response = self._api_server_session.put(url, data=body,
                                                     headers=headers)
        return (response.status_code, response.text)
    #end _http_delete

    def _parse_homepage(self, py_obj):
        srv_root_url = py_obj['href']
        self._srv_root_url = srv_root_url

        for link in py_obj['links']:
            # strip base from *_url to get *_uri
            uri = link['link']['href'].replace(srv_root_url, '')
            if link['link']['rel'] == 'collection':
                cls = utils.obj_type_to_vnc_class(link['link']['name'], __name__)
                if not cls:
                    continue
                cls.create_uri = uri
            elif link['link']['rel'] == 'resource-base':
                cls = utils.obj_type_to_vnc_class(link['link']['name'], __name__)
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

    def _read_args_to_id(self, res_type, fq_name=None, fq_name_str=None,
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
            return (True, self.fq_name_to_id(res_type, fq_name))
        if fq_name_str:
            return (True, self.fq_name_to_id(res_type, fq_name_str.split(':')))
        if ifmap_id:
            return (True, self.ifmap_to_id(ifmap_id))
    #end _read_args_to_id

    def _request_server(self, op, url, data=None, retry_on_error=True,
                        retry_after_authn=False, retry_count=30):
        if not self._srv_root_url:
            raise ConnectionError("Unable to retrive the api server root url.")

        return self._request(op, url, data=data, retry_on_error=retry_on_error,
                      retry_after_authn=retry_after_authn,
                      retry_count=retry_count)
    #end _request_server

    def _log_curl(self, op, url, data=None):
        op_str = {rest.OP_GET: 'GET', rest.OP_POST: 'POST', rest.OP_DELETE: 'DELETE', rest.OP_PUT: 'PUT'}
        base_url="http://%s:%s" %(self._authn_server, self._web_port)
        cmd_url="%s%s" %(base_url, url)
        cmd_hdr=None
        cmd_op=str(op_str[op])
        cmd_data=None
        header_list = [ j + ":" + k for (j,k) in self._headers.items()]
        header_string = ''.join(['-H "' + str(i) + '" ' for i in header_list])
        pattern=re.compile(r'(.*-H "X-AUTH-TOKEN:)[0-9a-z]+(")')
        cmd_hdr=re.sub(pattern,r'\1$TOKEN\2',header_string)
        if op == rest.OP_GET:
            if data:
                query_string="?" + "&".join([ str(i)+ "=" + str(j) for (i,j) in data.items()])
                cmd_url=base_url+url+query_string
        elif op == rest.OP_DELETE:
            pass
        else:
            cmd_data=str(data)
        if cmd_data:
            cmd="curl -X %s %s -d '%s' %s" %(cmd_op, cmd_hdr, cmd_data, cmd_url)
        else:
            cmd="curl -X %s %s %s" %(cmd_op, cmd_hdr, cmd_url)
        self._curl_logger.debug(cmd)
    #End _log_curl

    def _request(self, op, url, data=None, retry_on_error=True,
                 retry_after_authn=False, retry_count=30):
        retried = 0
        if self._curl_logging:
            self._log_curl(op=op,url=url,data=data)
        while True:
            try:
                if (op == rest.OP_GET):
                    (status, content) = self._http_get(
                        url, headers=self._headers, query_params=data)
                    if status == 200:
                        content = json.loads(content)
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
                content = self._request(op, url, data=data, retry_after_authn=True)

                return content
            elif status == 404:
                raise NoIdError('Error: oper %s url %s body %s response %s'
                                % (op, url, data, content))
            elif status == 403:
                raise PermissionDenied(content)
            elif status == 412:
                raise OverQuota(content)
            elif status == 409:
                raise RefsExistError(content)
            elif status == 504:
                # Request sent to API server, but no response came within 50s
                raise TimeOutError('Gateway Timeout 504')
            elif status in [502, 503]:
                # 502: API server died after accepting request, so retry
                # 503: no API server available even before sending the request
                retried += 1
                if retried >= retry_count:
                    raise ServiceUnavailableError('Service Unavailable Timeout %d' % status)

                time.sleep(1)
                continue
            elif status == 400:
                raise BadRequest(status, content)
            else:  # Unknown Error
                raise HttpError(status, content)
        # end while True

    #end _request_server

    def _prop_collection_post(
        self, obj_uuid, obj_field, oper, value, position):
        uri = self._action_uri['prop-collection-update']
        if isinstance(value, GeneratedsSuper):
            serialized_value = value.exportDict('')
        else:
            serialized_value = value

        oper_param = {'field': obj_field,
                      'operation': oper,
                      'value': serialized_value}
        if position:
            oper_param['position'] = position
        dict_body = {'uuid': obj_uuid, 'updates': [oper_param]}
        return self._request_server(
            rest.OP_POST, uri, data=json.dumps(dict_body))
    # end _prop_collection_post

    def _prop_collection_get(self, obj_uuid, obj_field, position):
        uri = self._action_uri['prop-collection-get']
        query_params = {'uuid': obj_uuid, 'fields': obj_field}
        if position:
            query_params['position'] = position

        content = self._request_server(
            rest.OP_GET, uri, data=query_params)

        return content[obj_field]
    # end _prop_collection_get

    def _prop_map_get_elem_key(self, id, obj_field, elem):
        _, res_type = self.id_to_fq_name_type(id)
        obj_class = utils.obj_type_to_vnc_class(res_type, __name__)

        key_name = obj_class.prop_map_field_key_names[obj_field]
        if isinstance(value, GeneratedsSuper):
            return getattr(value, key_name)

        return value[key_name]
    # end _prop_map_get_elem_key

    @check_homepage
    def prop_list_add_element(self, obj_uuid, obj_field, value, position=None):
        return self._prop_collection_post(
            obj_uuid, obj_field, 'add', value, position)
    # end prop_list_add_element

    @check_homepage
    def prop_list_modify_element(self, obj_uuid, obj_field, value, position):
        return self._prop_collection_post(
            obj_uuid, obj_field, 'modify', value, position)
    # end prop_list_modify_element

    @check_homepage
    def prop_list_delete_element(self, obj_uuid, obj_field, position):
        return self._prop_collection_post(
            obj_uuid, obj_field, 'delete', None, position)
    # end prop_list_delete_element

    @check_homepage
    def prop_list_get(self, obj_uuid, obj_field, position=None):
        return self._prop_collection_get(obj_uuid, obj_field, position)
    # end prop_list_get

    @check_homepage
    def prop_map_set_element(self, obj_uuid, obj_field, value):
        position = self._prop_map_get_elem_key(obj_uuid, obj_field, value)
        return self._prop_collection_post(
            obj_uuid, obj_field, 'set', value, position)
    # end prop_map_set_element

    @check_homepage
    def prop_map_delete_element(self, obj_uuid, obj_field, position):
        return self._prop_collection_post(
            obj_uuid, obj_field, 'delete', None, position)
    # end prop_map_delete_element

    @check_homepage
    def prop_map_get(self, obj_uuid, obj_field, position=None):
        return self._prop_collection_get(obj_uuid, obj_field, position)
    # end prop_list_get

    @check_homepage
    def ref_update(self, obj_type, obj_uuid, ref_type, ref_uuid,
                   ref_fq_name, operation, attr=None):
        if ref_type.endswith(('_refs', '-refs')):
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

    @check_homepage
    def ref_relax_for_delete(self, obj_uuid, ref_uuid):
        # don't account for reference of <obj_uuid> in delete of <ref_uuid> in future
        json_body =  json.dumps({'uuid': obj_uuid, 'ref-uuid': ref_uuid})
        uri = self._action_uri['ref-relax-for-delete']

        try:
            content = self._request_server(rest.OP_POST, uri, data=json_body)
        except HttpError as he:
            if he.status_code == 404:
                return None
            raise he

        return json.loads(content)['uuid']
    # end ref_relax_for_delete

    def obj_to_id(self, obj):
        return self.fq_name_to_id(obj.get_type(), obj.get_fq_name())
    #end obj_to_id

    @check_homepage
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

    @check_homepage
    def id_to_fq_name(self, id):
        json_body = json.dumps({'uuid': id})
        uri = self._action_uri['id-to-name']
        content = self._request_server(rest.OP_POST, uri, data=json_body)

        return json.loads(content)['fq_name']
    #end id_to_fq_name

    @check_homepage
    def id_to_fq_name_type(self, id):
        json_body = json.dumps({'uuid': id})
        uri = self._action_uri['id-to-name']
        content = self._request_server(rest.OP_POST, uri, data=json_body)

        json_rsp = json.loads(content)
        return (json_rsp['fq_name'], json_rsp['type'])

    # This is required only for helping ifmap-subscribers using rest publish
    @check_homepage
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

    @check_homepage
    def fetch_records(self):
        json_body = json.dumps({'fetch_records': None})
        uri = self._action_uri['fetch-records']
        content = self._request_server(rest.OP_POST, uri, data=json_body)

        return json.loads(content)['results']
    #end fetch_records

    @check_homepage
    def restore_config(self, create, resource, json_body):
        cls = utils.obj_type_to_vnc_class(resource, __name__)
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

    @check_homepage
    def kv_store(self, key, value):
        # TODO move oper value to common
        json_body = json.dumps({'operation': 'STORE',
                                'key': key,
                                'value': value})
        uri = self._action_uri['useragent-keyvalue']
        self._request_server(rest.OP_POST, uri, data=json_body)
    #end kv_store

    @check_homepage
    def kv_retrieve(self, key=None):
        # if key is None, entire collection is retrieved, use with caution!
        # TODO move oper value to common
        json_body = json.dumps({'operation': 'RETRIEVE',
                                'key': key})
        uri = self._action_uri['useragent-keyvalue']
        content = self._request_server(rest.OP_POST, uri, data=json_body)

        return json.loads(content)['value']
    #end kv_retrieve

    @check_homepage
    def kv_delete(self, key):
        # TODO move oper value to common
        json_body = json.dumps({'operation': 'DELETE',
                                'key': key})
        uri = self._action_uri['useragent-keyvalue']
        self._request_server(rest.OP_POST, uri, data=json_body)
    #end kv_delete

    # reserve block of IP address from a VN
    # expected format {"subnet" : "subnet_uuid", "count" : 4}
    @check_homepage
    def virtual_network_ip_alloc(self, vnobj, count=1, subnet=None, family=None):
        json_body = json.dumps({'count': count, 'subnet': subnet, 'family':family})
        uri = self._action_uri['virtual-network-ip-alloc'] % vnobj.uuid
        content = self._request_server(rest.OP_POST, uri, data=json_body)
        return json.loads(content)['ip_addr']
    #end virtual_network_ip_alloc

    # free previously reserved block of IP address from a VN
    # Expected format "ip_addr" : ["2.1.1.239", "2.1.1.238"]
    @check_homepage
    def virtual_network_ip_free(self, vnobj, ip_list):
        json_body = json.dumps({'ip_addr': ip_list})
        uri = self._action_uri['virtual-network-ip-free'] % vnobj.uuid
        rv = self._request_server(rest.OP_POST, uri, data=json_body)
        return rv
    #end virtual_network_ip_free

    # return no of ip instances from a given VN/Subnet
    # Expected format "subne_list" : ["subnet_uuid1", "subnet_uuid2"]
    @check_homepage
    def virtual_network_subnet_ip_count(self, vnobj, subnet_list):
        json_body = json.dumps({'subnet_list': subnet_list})
        uri = self._action_uri['virtual-network-subnet-ip-count'] % vnobj.uuid
        rv = self._request_server(rest.OP_POST, uri, data=json_body)
        return rv
    #end virtual_network_subnet_ip_count

    def get_auth_token(self):
        self._headers = self._authenticate(headers=self._headers)
        return self._auth_token

    #end get_auth_token

    @check_homepage
    def resource_list(self, obj_type, parent_id=None, parent_fq_name=None,
                      back_ref_id=None, obj_uuids=None, fields=None,
                      detail=False, count=False, filters=None):
        if not obj_type:
            raise ResourceTypeUnknownError(obj_type)

        obj_class = utils.obj_type_to_vnc_class(obj_type, __name__)
        if not obj_class:
            raise ResourceTypeUnknownError(obj_type)

        query_params = {}
        do_post_for_list = False

        if parent_fq_name:
            parent_fq_name_str = ':'.join(parent_fq_name)
            query_params['parent_fq_name_str'] = parent_fq_name_str
        elif parent_id:
            if isinstance(parent_id, list):
                query_params['parent_id'] = ','.join(parent_id)
                if len(parent_id) > self.POST_FOR_LIST_THRESHOLD:
                    do_post_for_list = True
            else:
                query_params['parent_id'] = parent_id

        if back_ref_id:
            if isinstance(back_ref_id, list):
                query_params['back_ref_id'] = ','.join(back_ref_id)
                if len(back_ref_id) > self.POST_FOR_LIST_THRESHOLD:
                    do_post_for_list = True
            else:
                query_params['back_ref_id'] = back_ref_id

        if obj_uuids:
            comma_sep_obj_uuids = ','.join(u for u in obj_uuids)
            query_params['obj_uuids'] = comma_sep_obj_uuids
            if len(obj_uuids) > self.POST_FOR_LIST_THRESHOLD:
                do_post_for_list = True

        if fields:
            comma_sep_fields = ','.join(f for f in fields)
            query_params['fields'] = comma_sep_fields

        query_params['detail'] = detail

        query_params['count'] = count

        if filters:
            query_params['filters'] = ','.join(
                '%s==%s' %(k,json.dumps(v)) for k,v in filters.items())

        if self._exclude_hrefs is not None:
            query_params['exclude_hrefs'] = True

        if do_post_for_list:
            uri = self._action_uri.get('list-bulk-collection')
            if not uri:
                raise

            # use same keys as in GET with additional 'type'
            query_params['type'] = obj_type
            json_body = json.dumps(query_params)
            content = self._request_server(rest.OP_POST,
                                           uri, json_body)
            response = json.loads(content)
        else: # GET /<collection>
            try:
                response = self._request_server(rest.OP_GET,
                               obj_class.create_uri,
                               data = query_params)
            except NoIdError:
                # dont allow NoIdError propagate to user
                return []

        if not detail:
            return response

        resource_dicts = response['%ss' %(obj_type)]
        resource_objs = []
        for resource_dict in resource_dicts:
            obj_dict = resource_dict['%s' %(obj_type)]
            resource_obj = obj_class.from_dict(**obj_dict)
            resource_obj.clear_pending_updates()
            resource_obj.set_server_conn(self)
            resource_objs.append(resource_obj)

        return resource_objs
    #end resource_list

    def set_auth_token(self, token):
        """Park user token for forwarding to API server for RBAC."""
        self._headers['X-AUTH-TOKEN'] = token
    #end set_auth_token

    def set_user_roles(self, roles):
        """Park user roles for forwarding to API server for RBAC.

        :param roles: list of roles
        """
        self._headers['X-API-ROLE'] = (',').join(roles)
    #end set_user_roles

    def set_exclude_hrefs(self):
        self._exclude_hrefs = True
    # end set_exclude_hrefs

    @check_homepage
    def obj_perms(self, token, obj_uuid=None):
        """
        validate user token. Optionally, check token authorization for an object.
        rv {'token_info': <token-info>, 'permissions': 'RWX'}
        """
        self._headers['X-USER-TOKEN'] = token
        query = 'uuid=%s' % obj_uuid if obj_uuid else ''
        try:
            rv = self._request_server(rest.OP_GET, "/obj-perms", data=query)
            return rv
        except PermissionDenied:
            rv = None
        finally:
            if 'X-USER-TOKEN' in self._headers:
                del self._headers['X-USER-TOKEN']
        return rv

    def is_cloud_admin_role(self):
        rv = self.obj_perms(self.get_auth_token()) or {}
        return rv.get('is_cloud_admin_role', False)

    def is_global_read_only_role(self):
        rv = self.obj_perms(self.get_auth_token()) or {}
        return rv.get('is_global_read_only_role', False)

    # change object ownsership
    def chown(self, obj_uuid, owner):
        payload = {'uuid': obj_uuid, 'owner': owner}
        content = self._request_server(rest.OP_POST,
            self._action_uri['chown'], data=json.dumps(payload))
        return content
    #end chown

    def chmod(self, obj_uuid, owner=None, owner_access=None, share=None, global_access=None):
        """
        owner: tenant UUID
        owner_access: octal permission for owner (int, 0-7)
        share: list of tuple of <uuid:octal-perms>, for example [(0ed5ea...700:7)]
        global_access: octal permission for global access (int, 0-7)
        """
        payload = {'uuid': obj_uuid}
        if owner:
            payload['owner'] = owner
        if owner_access is not None:
            payload['owner_access'] = owner_access
        if share is not None:
            payload['share'] = [{'tenant':item[0], 'tenant_access':item[1]} for item in share]
        if global_access is not None:
            payload['global_access'] = global_access
        content = self._request_server(rest.OP_POST,
            self._action_uri['chmod'], data=json.dumps(payload))
        return content

    def set_multi_tenancy(self, enabled):
        url = self._action_uri['multi-tenancy']
        data = {'enabled': enabled}
        content = self._request_server(rest.OP_PUT, url, json.dumps(data))
        return json.loads(content)

    def set_aaa_mode(self, mode):
        if mode not in cfgm_common.AAA_MODE_VALID_VALUES:
            raise HttpError(400, 'Invalid AAA mode')
        url = self._action_uri['aaa-mode']
        data = {'aaa-mode': mode}
        content =  self._request_server(rest.OP_PUT, url, json.dumps(data))
        return json.loads(content)

#end class VncApi
