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
import __main__ as main
import ssl

import gen.resource_common
from gen.resource_xsd import *
from gen.resource_client import *
from gen.vnc_api_client_gen import VncApiClientGen

from cfgm_common import rest, utils
from cfgm_common.exceptions import *
from cfgm_common import ssl_adapter

from pprint import pformat

def str_to_class(class_name):
    try:
        return reduce(getattr, class_name.split("."), sys.modules[__name__])
    except Exception as e:
        logger = logging.getLogger(__name__)
        logger.warn("Exception: %s", str(e))
        return None
#end str_to_class

def compare_refs(old_refs, new_refs):
    # compare refs in an object
    old_ref_dict = dict((':'.join(ref['to']), ref['attr']) for ref in old_refs)
    new_ref_dict = dict((':'.join(ref['to']), ref['attr']) for ref in new_refs)
    return old_ref_dict == new_ref_dict
# end compare_refs

def _read_cfg(cfg_parser, section, option, default):
        try:
            val = cfg_parser.get(section, option)
        except (AttributeError,
                ConfigParser.NoOptionError,
                ConfigParser.NoSectionError):
            val = default

        return val
#end _read_cfg

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
            self.vnc_api._cfg_root_url = self.vnc_api._parse_homepage(homepage)
            return dict.__getitem__(self, key)


class VncApi(VncApiClientGen):
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
    _DEFAULT_AUTHN_TENANT = VncApiClientGen._tenant_name
    _DEFAULT_DOMAIN_ID = "default"

    # Keystone and and vnc-api SSL support
    # contrail-api will remain to be on http
    # with LB (haproxy/F5/nginx..etc) configured for
    # ssl termination on port 8082(default contrail-api port)
    _DEFAULT_API_SERVER_CONNECT="http"
    _DEFAULT_API_SERVER_SSL_CONNECT="https"
    _DEFAULT_KS_CERT_BUNDLE="/tmp/keystonecertbundle.pem"
    _DEFAULT_API_CERT_BUNDLE="/tmp/apiservercertbundle.pem"

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
                 api_server_host='127.0.0.1', api_server_port='8082',
                 api_server_url=None, conf_file=None, user_info=None,
                 auth_token=None, auth_host=None, auth_port=None,
                 auth_protocol = None, auth_url=None, auth_type=None,
                 wait_for_connect=False, api_server_use_ssl=False):
        # TODO allow for username/password to be present in creds file

        super(VncApi, self).__init__(self._obj_serializer_diff)

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

            #contrail-api SSL support
            try:
               self._apiinsecure = cfg_parser.getboolean('global','insecure')
            except (AttributeError,
                    ConfigParser.NoOptionError,
                    ConfigParser.NoSectionError):
               self._apiinsecure = False
            apicertfile=_read_cfg(cfg_parser,'global','certfile','')
            apikeyfile=_read_cfg(cfg_parser,'global','keyfile','')
            apicafile=_read_cfg(cfg_parser,'global','cafile','')

            self._use_api_certs=False
            if apicertfile and apikeyfile \
               and apicafile and api_server_use_ssl:
                    certs=[apicertfile, apikeyfile, apicafile]
                    self._apicertbundle=utils.getCertKeyCaBundle(VncApi._DEFAULT_API_CERT_BUNDLE,certs)
                    self._use_api_certs=True

            # keystone SSL support
            try:
              self._ksinsecure = cfg_parser.getboolean('auth', 'insecure')
            except (AttributeError,
                    ConfigParser.NoOptionError,
                    ConfigParser.NoSectionError):
              self._ksinsecure = False
            kscertfile=_read_cfg(cfg_parser,'auth','certfile','')
            kskeyfile=_read_cfg(cfg_parser,'auth','keyfile','')
            kscafile=_read_cfg(cfg_parser,'auth','cafile','')

            self._use_ks_certs=False
            if kscertfile and kskeyfile and kscafile \
               and self._authn_protocol == 'https':
                   certs=[kscertfile, kskeyfile, kscafile]
                   self._kscertbundle=utils.getCertKeyCaBundle(VncApi._DEFAULT_KS_CERT_BUNDLE,certs)
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
                               ' "domain": { "id": "%s" },' % (self._DEFAULT_DOMAIN_ID) + \
                               ' "password": "%s"' % (self._password) + \
                             '}' + \
                            '}' + \
                          '},' + \
                          ' "scope":{' + \
                            ' "project":{' + \
                              ' "domain": { "id": "%s" },' % (self._DEFAULT_DOMAIN_ID) + \
                              ' "name": "%s"' % (self._username) + \
                            '}' + \
                          '}' + \
                        '}' + \
                     '}'
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

        self._max_pools = int(_read_cfg(
            cfg_parser, 'global', 'MAX_POOLS',
            self._DEFAULT_MAX_POOLS))
        self._max_conns_per_pool = int(_read_cfg(
            cfg_parser, 'global', 'MAX_CONNS_PER_POOL',
            self._DEFAULT_MAX_CONNS_PER_POOL))

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
                self._headers['X-API-ROLE'] = self._user_info['role']

        #self._http = HTTPClient(self._web_host, self._web_port,
        #                        network_timeout = 300)

        self._create_api_server_session()

        retry_count = 6
        while retry_count:
            try:
                homepage = self._request(rest.OP_GET, self._base_url,
                                         retry_on_error=False)
                self._cfg_root_url = self._parse_homepage(homepage)
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

        adapter = requests.adapters.HTTPAdapter(pool_connections=self._max_conns_per_pool,
                                                pool_maxsize=self._max_pools)
        ssladapter = ssl_adapter.SSLAdapter(ssl.PROTOCOL_SSLv23)
        ssladapter.init_poolmanager(connections=self._max_conns_per_pool,maxsize=self._max_pools)
        self._api_server_session.mount("http://", adapter)
        self._api_server_session.mount("https://", ssladapter)
    #end _create_api_server_session

    # Authenticate with configured service
    def _authenticate(self, response=None, headers=None):
        if self._authn_type is None:
            return headers
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
            raise RuntimeError('Unable to connect to keystone for authentication. Verify keystone server details')

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
              % (self._api_connect_protocol, self._web_host, self._web_port, uri)
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
              % (self._api_connect_protocol, self._web_host, self._web_port, uri)
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
              % (self._api_connect_protocol, self._web_host, self._web_port, uri)
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

    def _parse_homepage(self, json_body):
        py_obj = json.loads(json_body)

        srv_root_url = py_obj['href']
        self._srv_root_url = srv_root_url

        for link in py_obj['links']:
            # strip base from *_url to get *_uri
            uri = link['link']['href'].replace(srv_root_url, '')
            if link['link']['rel'] == 'collection':
                class_name = "%s" % (utils.CamelCase(link['link']['name']))
                cls = str_to_class(class_name)
                if not cls:
                    continue
                cls.create_uri = uri
            elif link['link']['rel'] == 'resource-base':
                class_name = "%s" % (utils.CamelCase(link['link']['name']))
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

    def _request_server(self, op, url, data=None, retry_on_error=True,
                        retry_after_authn=False, retry_count=30):
        if not hasattr(self, '_cfg_root_url'):
            homepage = self._request(rest.OP_GET, self._base_url,
                                     retry_on_error=False)
            self._cfg_root_url = self._parse_homepage(homepage)

        return self._request(op, url, data=data, retry_on_error=retry_on_error,
                      retry_after_authn=retry_after_authn,
                      retry_count=retry_count)

    def _request(self, op, url, data=None, retry_on_error=True,
                 retry_after_authn=False, retry_count=30):
        retried = 0
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
                content = self._request(op, url, data=data, retry_after_authn=True)
     
                return content
            elif status == 404:
                raise NoIdError('Error: oper %s url %s body %s response %s'
                                % (op, url, data, content))
            elif status == 403:
                raise PermissionDenied(content)
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
        class_name = "%s" % (utils.CamelCase(resource))
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

    def resource_list(self, obj_type, parent_id=None, parent_fq_name=None,
                      back_ref_id=None, obj_uuids=None, fields=None,
                      detail=False, count=False, filters=None):
        if not obj_type:
            raise ResourceTypeUnknownError(obj_type)

        class_name = "%s" % (utils.CamelCase(obj_type))
        obj_class = str_to_class(class_name)
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

        if do_post_for_list:
            uri = self._action_uri.get('list-bulk-collection')
            if not uri:
                raise

            # use same keys as in GET with additional 'type'
            query_params['type'] = obj_type
            json_body = json.dumps(query_params)
            content = self._request_server(rest.OP_POST,
                                           uri, json_body)
        else: # GET /<collection>
            try:
                content = self._request_server(rest.OP_GET,
                               obj_class.create_uri,
                               data = query_params)
            except NoIdError:
                # dont allow NoIdError propagate to user
                return []

        if not detail:
            return json.loads(content)

        resource_dicts = json.loads(content)['%ss' %(obj_type)]
        resource_objs = []
        for resource_dict in resource_dicts:
            obj_dict = resource_dict['%s' %(obj_type)]
            resource_obj = obj_class.from_dict(**obj_dict)
            resource_obj.clear_pending_updates()
            resource_obj.set_server_conn(self)
            resource_objs.append(resource_obj)

        return resource_objs
    #end resource_list

#end class VncApi
