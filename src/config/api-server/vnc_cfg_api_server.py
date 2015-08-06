#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
"""
This is the main module in vnc_cfg_api_server package. It manages interaction
between http/rest, address management, authentication and database interfaces.
"""

from gevent import monkey
monkey.patch_all()
from gevent import hub

# from neutron plugin to api server, the request URL could be large. fix the const
import gevent.pywsgi
gevent.pywsgi.MAX_REQUEST_LINE = 65535

import sys
reload(sys)
sys.setdefaultencoding('UTF8')
import functools
import re
import logging
import logging.config
import signal
import os
import socket
from cfgm_common import jsonutils as json
import uuid
import copy
import argparse
import ConfigParser
from pprint import pformat
import cgitb
from cStringIO import StringIO
from lxml import etree
#import GreenletProfiler

logger = logging.getLogger(__name__)

"""
Following is needed to silence warnings on every request when keystone\
    auth_token middleware + Sandesh is used. Keystone or Sandesh alone\
    do not produce these warnings.

Exception AttributeError: AttributeError(
    "'_DummyThread' object has no attribute '_Thread__block'",)
    in <module 'threading' from '/usr/lib64/python2.7/threading.pyc'> ignored

See http://stackoverflow.com/questions/13193278/understand-python-threading-bug
for more information.
"""
import threading
threading._DummyThread._Thread__stop = lambda x: 42

CONFIG_VERSION = '1.0'

import bottle
bottle.BaseRequest.MEMFILE_MAX = 1024000

import utils
import vnc_cfg_types
from vnc_cfg_ifmap import VncDbClient

from cfgm_common import ignore_exceptions, imid
from cfgm_common.uve.vnc_api.ttypes import VncApiCommon, VncApiReadLog,\
    VncApiConfigLog, VncApiError
from cfgm_common.uve.virtual_network.ttypes import UveVirtualNetworkConfig,\
    UveVirtualNetworkConfigTrace
from cfgm_common import illegal_xml_chars_RE
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, Module2NodeType,\
    NodeTypeNames, INSTANCE_ID_DEFAULT, API_SERVER_DISCOVERY_SERVICE_NAME,\
    IFMAP_SERVER_DISCOVERY_SERVICE_NAME

from provision_defaults import Provision
from vnc_quota import *
from gen.resource_xsd import *
from gen.resource_common import *
from gen.resource_server import *
import gen.vnc_api_server_gen
import cfgm_common
from cfgm_common.rest import LinkObject, hdr_server_tenant
from cfgm_common.exceptions import *
from cfgm_common.vnc_extensions import ExtensionManager, ApiHookManager
import gen.resource_xsd
import vnc_addr_mgmt
import vnc_auth
import vnc_auth_keystone
import vnc_perms
from cfgm_common import vnc_cpu_info

from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
import discoveryclient.client as client
#from gen_py.vnc_api.ttypes import *
import netifaces
from pysandesh.connection_info import ConnectionState
from cfgm_common.uve.cfgm_cpuinfo.ttypes import NodeStatusUVE, \
    NodeStatus

from sandesh.traces.ttypes import RestApiTrace
from vnc_bottle import get_bottle_server

_ACTION_RESOURCES = [
    {'uri': '/ref-update', 'link_name': 'ref-update',
     'method_name': 'ref_update_http_post'},
    {'uri': '/fqname-to-id', 'link_name': 'name-to-id',
     'method_name': 'fq_name_to_id_http_post'},
    {'uri': '/id-to-fqname', 'link_name': 'id-to-name',
     'method_name': 'id_to_fq_name_http_post'},
    # ifmap-to-id only for ifmap subcribers using rest for publish
    {'uri': '/ifmap-to-id', 'link_name': 'ifmap-to-id',
     'method_name': 'ifmap_to_id_http_post'},
    {'uri': '/useragent-kv', 'link_name': 'useragent-keyvalue',
     'method_name': 'useragent_kv_http_post'},
    {'uri': '/db-check', 'link_name': 'database-check',
     'method_name': 'db_check'},
    {'uri': '/fetch-records', 'link_name': 'fetch-records',
     'method_name': 'fetch_records'},
    {'uri': '/start-profile', 'link_name': 'start-profile',
     'method_name': 'start_profile'},
    {'uri': '/stop-profile', 'link_name': 'stop-profile',
     'method_name': 'stop_profile'},
    {'uri': '/list-bulk-collection', 'link_name': 'list-bulk-collection',
     'method_name': 'list_bulk_collection_http_post'},
]


@bottle.error(400)
def error_400(err):
    return err.body
# end error_400


@bottle.error(403)
def error_403(err):
    return err.body
# end error_403


@bottle.error(404)
def error_404(err):
    return err.body
# end error_404


@bottle.error(409)
def error_409(err):
    return err.body
# end error_409


@bottle.error(500)
def error_500(err):
    return err.body
# end error_500


@bottle.error(503)
def error_503(err):
    return err.body
# end error_503


# Masking of password from openstack/common/log.py
_SANITIZE_KEYS = ['adminPass', 'admin_pass', 'password', 'admin_password']

# NOTE(ldbragst): Let's build a list of regex objects using the list of
# _SANITIZE_KEYS we already have. This way, we only have to add the new key
# to the list of _SANITIZE_KEYS and we can generate regular expressions
# for XML and JSON automatically.
_SANITIZE_PATTERNS = []
_FORMAT_PATTERNS = [r'(%(key)s\s*[=]\s*[\"\']).*?([\"\'])',
                    r'(<%(key)s>).*?(</%(key)s>)',
                    r'([\"\']%(key)s[\"\']\s*:\s*[\"\']).*?([\"\'])',
                    r'([\'"].*?%(key)s[\'"]\s*:\s*u?[\'"]).*?([\'"])']

for key in _SANITIZE_KEYS:
    for pattern in _FORMAT_PATTERNS:
        reg_ex = re.compile(pattern % {'key': key}, re.DOTALL)
        _SANITIZE_PATTERNS.append(reg_ex)

def mask_password(message, secret="***"):
    """Replace password with 'secret' in message.
    :param message: The string which includes security information.
    :param secret: value with which to replace passwords.
    :returns: The unicode value of message with the password fields masked.

    For example:

    >>> mask_password("'adminPass' : 'aaaaa'")
    "'adminPass' : '***'"
    >>> mask_password("'admin_pass' : 'aaaaa'")
    "'admin_pass' : '***'"
    >>> mask_password('"password" : "aaaaa"')
    '"password" : "***"'
    >>> mask_password("'original_password' : 'aaaaa'")
    "'original_password' : '***'"
    >>> mask_password("u'original_password' :   u'aaaaa'")
    "u'original_password' :   u'***'"
    """
    if not any(key in message for key in _SANITIZE_KEYS):
        return message

    secret = r'\g<1>' + secret + r'\g<2>'
    for pattern in _SANITIZE_PATTERNS:
        message = re.sub(pattern, secret, message)
    return message

class VncApiServer(object):
    """
    This is the manager class co-ordinating all classes present in the package
    """
    _INVALID_NAME_CHARS = set(':')

    def __new__(cls, *args, **kwargs):
        obj = super(VncApiServer, cls).__new__(cls, *args, **kwargs)
        bottle.route('/', 'GET', obj.homepage_http_get)

        cls._generate_resource_crud_methods(obj)
        cls._generate_resource_crud_uri(obj)
        for act_res in _ACTION_RESOURCES:
            method = getattr(obj, act_res['method_name'])
            obj.route(act_res['uri'], 'POST', method)
        return obj
    # end __new__

    def _validate_props_in_request(self, resource_class, obj_dict):
        for prop_name in resource_class.prop_fields:
            is_simple, prop_type = resource_class.prop_field_types[prop_name]
            # TODO validate primitive types
            if is_simple:
                continue
            prop_dict = obj_dict.get(prop_name)
            if not prop_dict:
                continue

            buf = cStringIO.StringIO()
            prop_cls = cfgm_common.utils.str_to_class(prop_type, __name__)
            try:
                tmp_prop = prop_cls(**prop_dict)
                tmp_prop.export(buf)
                node = etree.fromstring(buf.getvalue())
                tmp_prop = prop_cls()
                tmp_prop.build(node)
            except Exception as e:
                err_msg = 'Error validating property %s value %s ' \
                          %(prop_name, prop_dict)
                err_msg += str(e)
                return False, err_msg

        return True, ''
    # end _validate_props_in_request

    def _validate_refs_in_request(self, resource_class, obj_dict):
        for ref_name in resource_class.ref_fields:
            _, ref_link_type, _ = \
                resource_class.ref_field_types[ref_name]
            if ref_link_type == 'None':
                continue
            for ref_dict in obj_dict.get(ref_name) or []:
                buf = cStringIO.StringIO()
                attr_cls = cfgm_common.utils.str_to_class(ref_link_type, __name__)
                tmp_attr = attr_cls(**ref_dict['attr'])
                tmp_attr.export(buf)
                node = etree.fromstring(buf.getvalue())
                try:
                    tmp_attr.build(node)
                except Exception as e:
                    err_msg = 'Error validating reference %s value %s ' \
                              %(ref_name, ref_dict)
                    err_msg += str(e)
                    return False, err_msg

        return True, ''
    # end _validate_refs_in_request

    def _validate_perms_in_request(self, resource_class, obj_type, obj_dict):
        for ref_name in resource_class.ref_fields:
            for ref in obj_dict.get(ref_name) or []:
                ref_type, _, _ = resource_class.ref_field_types[ref_name]
                ref_uuid = self._db_conn.fq_name_to_uuid(ref_type, ref['to'])
                (ok, status) = self._permissions.check_perms_link(
                    bottle.request, ref_uuid)
                if not ok:
                    (code, err_msg) = status
                    bottle.abort(code, err_msg)
    # end _validate_perms_in_request

    def http_resource_create(self, resource_type):
        r_class = self.get_resource_class(resource_type)
        obj_type = resource_type.replace('-', '_')
        obj_dict = bottle.request.json[resource_type]
        self._post_validate(obj_type, obj_dict=obj_dict)
        fq_name = obj_dict['fq_name']

        try:
            self._extension_mgrs['resourceApi'].map_method(
                 'pre_%s_create' %(obj_type), obj_dict)
        except RuntimeError:
            # lack of registered extension leads to RuntimeError
            pass
        except Exception as e:
            err_msg = 'In pre_%s_create an extension had error for %s' \
                      %(obj_type, obj_dict)
            err_msg += str(e)
            self.config_log(err_msg, level=SandeshLevel.SYS_NOTICE)

        # properties validator
        ok, result = self._validate_props_in_request(r_class, obj_dict)
        if not ok:
            result = 'Bad property in create: ' + result
            bottle.abort(400, result)

        # references validator
        ok, result = self._validate_refs_in_request(r_class, obj_dict)
        if not ok:
            result = 'Bad reference in create: ' + result
            bottle.abort(400, result)

        # common handling for all resource create
        (ok, result) = self._post_common(
            bottle.request, resource_type, obj_dict)
        if not ok:
            (code, msg) = result
            fq_name_str = ':'.join(obj_dict.get('fq_name', []))
            self.config_object_error(None, fq_name_str, obj_type, 'http_post', msg)
            bottle.abort(code, msg)

        name = obj_dict['fq_name'][-1]
        fq_name = obj_dict['fq_name']

        db_conn = self._db_conn

        # if client gave parent_type of config-root, ignore and remove
        if 'parent_type' in obj_dict and obj_dict['parent_type'] == 'config-root':
            del obj_dict['parent_type']

        if 'parent_type' in obj_dict:
            # non config-root child, verify parent exists
            parent_type = obj_dict['parent_type']
            parent_fq_name = obj_dict['fq_name'][:-1]
            try:
                parent_uuid = self._db_conn.fq_name_to_uuid(parent_type, parent_fq_name)
                (ok, status) = self._permissions.check_perms_write(
                    bottle.request, parent_uuid)
                if not ok:
                    (code, err_msg) = status
                    bottle.abort(code, err_msg)
                self._permissions.set_user_role(bottle.request, obj_dict)
            except NoIdError:
                err_msg = 'Parent ' + pformat(parent_fq_name) + ' type ' + parent_type + ' does not exist'
                fq_name_str = ':'.join(parent_fq_name)
                self.config_object_error(None, fq_name_str, obj_type, 'http_post', err_msg)
                bottle.abort(400, err_msg)

        # Validate perms on references
        try:
            self._validate_perms_in_request(r_class, obj_type, obj_dict)
        except NoIdError:
            bottle.abort(400, 'Unknown reference in resource create %s.' %(obj_dict))

        # State modification starts from here. Ensure that cleanup is done for all state changes
        cleanup_on_failure = []
        # Alloc and Store id-mappings before creating entry on pubsub store.
        # Else a subscriber can ask for an id mapping before we have stored it
        uuid_requested = result
        (ok, result) = db_conn.dbe_alloc(resource_type, obj_dict, uuid_requested)
        if not ok:
            (code, msg) = result
            fq_name_str = ':'.join(obj_dict['fq_name'])
            self.config_object_error(None, fq_name_str, obj_type, 'http_post', result)
            bottle.abort(code, msg)
        cleanup_on_failure.append((db_conn.dbe_release, [obj_type, fq_name]))

        obj_ids = result

        env = bottle.request.headers.environ
        tenant_name = env.get(hdr_server_tenant(), 'default-project')

        # type-specific hook
        try:
            (ok, result) = r_class.http_post_collection(tenant_name, obj_dict, db_conn)
        except Exception as e:
            ok = False
            result = (500, str(e))
        if not ok:
            for fail_cleanup_callable, cleanup_args in cleanup_on_failure:
                fail_cleanup_callable(*cleanup_args)
            (code, msg) = result
            fq_name_str = ':'.join(fq_name)
            self.config_object_error(None, fq_name_str, obj_type, 'http_post', msg)
            bottle.abort(code, msg)

        callable = getattr(r_class, 'http_post_collection_fail', None)
        if callable:
            cleanup_on_failure.append((callable, [tenant_name, obj_dict, db_conn]))

        try:
            (ok, result) = \
                 db_conn.dbe_create(resource_type, obj_ids, obj_dict)
        except Exception as e:
            ok = False
            result = str(e)

        if not ok:
            for fail_cleanup_callable, cleanup_args in cleanup_on_failure:
                fail_cleanup_callable(*cleanup_args)
            fq_name_str = ':'.join(fq_name)
            self.config_object_error(None, fq_name_str, obj_type, 'http_post', result)
            bottle.abort(404, result)

        rsp_body = {}
        rsp_body['name'] = name
        rsp_body['fq_name'] = fq_name
        rsp_body['uuid'] = obj_ids['uuid']
        rsp_body['href'] = self.generate_url(resource_type, obj_ids['uuid'])
        if 'parent_type' in obj_dict:
            # non config-root child, send back parent uuid/href
            rsp_body['parent_uuid'] = parent_uuid
            rsp_body['parent_href'] = self.generate_url(parent_type, parent_uuid)

        try:
            self._extension_mgrs['resourceApi'].map_method(
                'post_%s_create' %(obj_type), obj_dict)
        except RuntimeError:
            # lack of registered extension leads to RuntimeError
            pass
        except Exception as e:
            err_msg = 'In post_%s_create an extension had error for %s' \
                      %(obj_type, obj_dict)
            err_msg += str(e)
            self.config_log(err_msg, level=SandeshLevel.SYS_NOTICE)

        return {resource_type: rsp_body}
    # end http_resource_create

    def http_resource_read(self, resource_type, id):
        r_class = self.get_resource_class(resource_type)
        obj_type = resource_type.replace('-', '_')
        try:
            self._extension_mgrs['resourceApi'].map_method(
                'pre_%s_read' %(obj_type), id)
        except Exception as e:
            pass

        etag = bottle.request.headers.get('If-None-Match')
        db_conn = self._db_conn
        try:
            req_obj_type = db_conn.uuid_to_obj_type(id)
            if req_obj_type != obj_type:
                bottle.abort(
                    404, 'No %s object found for id %s' %(resource_type, id))
            fq_name = db_conn.uuid_to_fq_name(id)
        except NoIdError as e:
            bottle.abort(404, str(e))

        # common handling for all resource get
        (ok, result) = self._get_common(bottle.request, id)
        if not ok:
            (code, msg) = result
            self.config_object_error(
                id, None, obj_type, 'http_get', msg)
            bottle.abort(code, msg)

        # type-specific hook
        r_class.http_get(id)

        db_conn = self._db_conn
        if etag:
            obj_ids = {'uuid': id}
            (ok, result) = db_conn.dbe_is_latest(obj_ids, etag.replace('"', ''))
            if not ok:
                # Not present in DB
                self.config_object_error(
                    id, None, obj_type, 'http_get', result)
                bottle.abort(404, result)

            is_latest = result
            if is_latest:
                # send Not-Modified, caches use this for read optimization
                response.status = 304
                return
        #end if etag

        obj_ids = {'uuid': id}

        # Generate field list for db layer
        if 'fields' in bottle.request.query:
            obj_fields = bottle.request.query.fields.split(',')
        else: # default props + children + refs + backrefs
            obj_fields = list(r_class.prop_fields) + list(r_class.ref_fields)
            if 'exclude_back_refs' not in bottle.request.query:
                obj_fields = obj_fields + list(r_class.backref_fields)
            if 'exclude_children' not in bottle.request.query:
                obj_fields = obj_fields + list(r_class.children_fields)

        try:
            (ok, result) = db_conn.dbe_read(resource_type, obj_ids, obj_fields)
            if not ok:
                self.config_object_error(id, None, obj_type, 'http_get', result)
        except NoIdError as e:
            # Not present in DB
            bottle.abort(404, str(e))
        if not ok:
            bottle.abort(500, result)

        # check visibility
        if (not result['id_perms'].get('user_visible', True) and
            not self.is_admin_request()):
            result = 'This object is not visible by users: %s' % id
            self.config_object_error(id, None, obj_type, 'http_get', result)
            bottle.abort(404, result)

        rsp_body = {}
        rsp_body['uuid'] = id
        rsp_body['href'] = self.generate_url(resource_type, id)
        rsp_body['name'] = result['fq_name'][-1]
        rsp_body.update(result)
        id_perms = result['id_perms']
        bottle.response.set_header('ETag', '"' + id_perms['last_modified'] + '"')
        try:
            self._extension_mgrs['resourceApi'].map_method(
                'post_%s_read' %(obj_type), id, rsp_body)
        except Exception as e:
            pass

        return {resource_type: rsp_body}
    # end http_resource_read

    def http_resource_update(self, resource_type, id):
        r_class = self.get_resource_class(resource_type)
        obj_type = resource_type.replace('-', '_')
        obj_dict = bottle.request.json[resource_type]
        try:
            self._extension_mgrs['resourceApi'].map_method(
                'pre_%s_update' %(obj_type), id, obj_dict)
        except RuntimeError:
            # lack of registered extension leads to RuntimeError
            pass
        except Exception as e:
            err_msg = 'In pre_%s_update an extension had error for %s' \
                      %(obj_type, obj_dict)
            err_msg += str(e)
            self.config_log(err_msg, level=SandeshLevel.SYS_NOTICE)

        db_conn = self._db_conn
        try:
            req_obj_type = db_conn.uuid_to_obj_type(id)
            if req_obj_type != obj_type:
                bottle.abort(
                    404, 'No %s object found for id %s' %(resource_type, id))
            fq_name = db_conn.uuid_to_fq_name(id)
        except NoIdError as e:
            bottle.abort(404, str(e))

        # properties validator
        ok, result = self._validate_props_in_request(r_class, obj_dict)
        if not ok:
            result = 'Bad property in update: ' + result
            bottle.abort(400, result)

        # references validator
        ok, result = self._validate_refs_in_request(r_class, obj_dict)
        if not ok:
            result = 'Bad reference in update: ' + result
            bottle.abort(400, result)

        # common handling for all resource put
        (ok, result) = self._put_common(
            bottle.request, obj_type, id, fq_name, obj_dict)
        if not ok:
            (code, msg) = result
            self.config_object_error(id, None, obj_type, 'http_put', msg)
            bottle.abort(code, msg)

        # Validate perms on references
        try:
            self._validate_perms_in_request(r_class, obj_type, obj_dict)
        except NoIdError:
            bottle.abort(400,
                'Unknown reference in resource update %s %s.'
                %(obj_type, obj_dict))

        # State modification starts from here. Ensure that cleanup is done for all state changes
        cleanup_on_failure = []
        # type-specific hook
        (ok, put_result) = r_class.http_put(id, fq_name, obj_dict, self._db_conn)
        if not ok:
            (code, msg) = put_result
            self.config_object_error(id, None, obj_type, 'http_put', msg)
            bottle.abort(code, msg)
        callable = getattr(r_class, 'http_put_fail', None)
        if callable:
            cleanup_on_failure.append((callable, [id, fq_name, obj_dict, self._db_conn]))

        obj_ids = {'uuid': id}
        try:
            (ok, result) = db_conn.dbe_update(resource_type, obj_ids, obj_dict)
        except Exception as e:
            ok = False
            result = str(e)
        if not ok:
            for fail_cleanup_callable, cleanup_args in cleanup_on_failure:
                    fail_cleanup_callable(*cleanup_args)
            self.config_object_error(id, None, obj_type, 'http_put', result)
            bottle.abort(404, result)

        rsp_body = {}
        rsp_body['uuid'] = id
        rsp_body['href'] = self.generate_url(resource_type, id)

        try:
            self._extension_mgrs['resourceApi'].map_method(
                'post_%s_update' %(obj_type), id, obj_dict)
        except RuntimeError:
            # lack of registered extension leads to RuntimeError
            pass
        except Exception as e:
            err_msg = 'In post_%s_update an extension had error for %s' \
                      %(obj_type, obj_dict)
            err_msg += str(e)
            self.config_log(err_msg, level=SandeshLevel.SYS_NOTICE)

        return {resource_type: rsp_body}
    # end http_resource_update

    def http_resource_delete(self, resource_type, id):
        r_class = self.get_resource_class(resource_type)
        obj_type = resource_type.replace('-', '_')
        db_conn = self._db_conn
        # if obj doesn't exist return early
        try:
            req_obj_type = db_conn.uuid_to_obj_type(id)
            if req_obj_type != obj_type:
                bottle.abort(
                    404, 'No %s object found for id %s' %(resource_type, id))
            _ = db_conn.uuid_to_fq_name(id)
        except NoIdError:
            bottle.abort(404, 'ID %s does not exist' %(id))

        try:
            self._extension_mgrs['resourceApi'].map_method(
                'pre_%s_delete' %(obj_type), id)
        except RuntimeError:
            # lack of registered extension leads to RuntimeError
            pass
        except Exception as e:
            err_msg = 'In pre_%s_delete an extension had error for %s' \
                      %(obj_type, id)
            err_msg += str(e)
            self.config_log(err_msg, level=SandeshLevel.SYS_NOTICE)

        # read in obj from db (accepting error) to get details of it
        obj_ids = {'uuid': id}
        obj_fields = list(r_class.children_fields) + \
                     list(r_class.backref_fields)
        try:
            (read_ok, read_result) = db_conn.dbe_read(
                resource_type, obj_ids, obj_fields)
        except NoIdError as e:
            bottle.abort(404, str(e))
        if not read_ok:
            self.config_object_error(
                id, None, obj_type, 'http_delete', read_result)
            # proceed down to delete the resource

        # common handling for all resource delete
        parent_type = read_result.get('parent_type')
        (ok, del_result) = self._delete_common(
            bottle.request, obj_type, id, parent_type)
        if not ok:
            (code, msg) = del_result
            self.config_object_error(id, None, obj_type, 'http_delete', msg)
            bottle.abort(code, msg)

        fq_name = read_result['fq_name']
        ifmap_id = imid.get_ifmap_id_from_fq_name(resource_type, fq_name)
        obj_ids['imid'] = ifmap_id
        if parent_type:
            parent_imid = cfgm_common.imid.get_ifmap_id_from_fq_name(parent_type, fq_name[:-1])
            obj_ids['parent_imid'] = parent_imid

        # State modification starts from here. Ensure that cleanup is done for all state changes
        cleanup_on_failure = []

        # type-specific hook
        r_class = self.get_resource_class(resource_type)
        # fail if non-default children or non-derived backrefs exist
        default_names = {}
        for child_field in r_class.children_fields:
            child_type, is_derived = r_class.children_field_types[child_field]
            if is_derived:
                continue
            child_cls = self.get_resource_class(child_type)
            default_child_name = 'default-%s' %(
                child_cls(parent_type=resource_type).get_type())
            default_names[child_type] = default_child_name
            exist_hrefs = []
            for child in read_result.get(child_field, []):
                if child['to'][-1] == default_child_name:
                    continue
                exist_hrefs.append(child['href'])
            if exist_hrefs:
                err_msg = 'Delete when children still present: %s' %(
                    exist_hrefs)
                self.config_object_error(
                    id, None, obj_type, 'http_delete', err_msg)
                bottle.abort(409, err_msg)

        for backref_field in r_class.backref_fields:
            _, _, is_derived = r_class.backref_field_types[backref_field]
            if is_derived:
                continue
            exist_hrefs = [backref['href']
                           for backref in read_result.get(backref_field, [])]
            if exist_hrefs:
                err_msg = 'Delete when resource still referred: %s' %(
                    exist_hrefs)
                self.config_object_error(
                    id, None, obj_type, 'http_delete', err_msg)
                bottle.abort(409, err_msg)

        # Delete default children first
        for child_field in r_class.children_fields:
            child_type, is_derived = r_class.children_field_types[child_field]
            if is_derived:
                continue
            cr_class = self.get_resource_class(child_type)
            if not cr_class.generate_default_instance:
                continue
            self.delete_default_children(child_type, read_result)

        (ok, del_result) = r_class.http_delete(id, read_result, db_conn)
        if not ok:
            (code, msg) = del_result
            self.config_object_error(id, None, obj_type, 'http_delete', msg)
            bottle.abort(code, msg)
        callable = getattr(r_class, 'http_delete_fail', None)
        if callable:
            cleanup_on_failure.append((callable, [id, read_result, db_conn]))

        try:
            (ok, del_result) = db_conn.dbe_delete(
                resource_type, obj_ids, read_result)
        except Exception as e:
            ok = False
            del_result = str(e)
        if not ok:
            for fail_cleanup_callable, cleanup_args in cleanup_on_failure:
                fail_cleanup_callable(*cleanup_args)
            self.config_object_error(id, None, obj_type, 'http_delete', del_result)
            bottle.abort(409, del_result)

        try:
            self._extension_mgrs['resourceApi'].map_method(
                'post_%s_delete' %(obj_type), id, read_result)
        except RuntimeError:
            # lack of registered extension leads to RuntimeError
            pass
        except Exception as e:
            err_msg = 'In pre_%s_delete an extension had error for %s' \
                      %(obj_type, id)
            err_msg += str(e)
            self.config_log(err_msg, level=SandeshLevel.SYS_NOTICE)
    # end http_resource_delete

    def http_resource_list(self, resource_type):
        r_class = self.get_resource_class(resource_type)
        obj_type = resource_type.replace('-', '_')
        db_conn = self._db_conn

        env = bottle.request.headers.environ
        tenant_name = env.get(hdr_server_tenant(), 'default-project')
        parent_uuids = None
        back_ref_uuids = None
        obj_uuids = None
        if (('parent_fq_name_str' in bottle.request.query) and
            ('parent_type' in bottle.request.query)):
            parent_fq_name = bottle.request.query.parent_fq_name_str.split(':')
            parent_type = bottle.request.query.parent_type
            parent_uuids = [self._db_conn.fq_name_to_uuid(parent_type, parent_fq_name)]
        elif 'parent_id' in bottle.request.query:
            parent_ids = bottle.request.query.parent_id.split(',')
            parent_uuids = [str(uuid.UUID(p_uuid)) for p_uuid in parent_ids]
        if 'back_ref_id' in bottle.request.query:
            back_ref_ids = bottle.request.query.back_ref_id.split(',')
            back_ref_uuids = [str(uuid.UUID(b_uuid)) for b_uuid in back_ref_ids]
        if 'obj_uuids' in bottle.request.query:
            obj_uuids = bottle.request.query.obj_uuids.split(',')

        # common handling for all resource get
        (ok, result) = self._get_common(bottle.request, parent_uuids)
        if not ok:
            (code, msg) = result
            self.config_object_error(
                None, None, '%ss' %(resource_type), 'http_get_collection', msg)
            bottle.abort(code, msg)

        if 'count' in bottle.request.query:
            is_count = 'true' in bottle.request.query.count.lower()
        else:
            is_count = False

        if 'detail' in bottle.request.query:
            is_detail = 'true' in bottle.request.query.detail.lower()
        else:
            is_detail = False

        if 'fields' in bottle.request.query:
            req_fields = bottle.request.query.fields.split(',')
        else:
            req_fields = []

        filter_params = bottle.request.query.filters
        if filter_params:
            try:
                ff_key_vals = filter_params.split(',')
                ff_names = [ff.split('==')[0] for ff in ff_key_vals]
                ff_values = [ff.split('==')[1] for ff in ff_key_vals]
                filters = {'field_names': ff_names, 'field_values': ff_values}
            except Exception as e:
                abort(400, 'Invalid filter ' + filter_params)
        else:
            filters = None

        return self._list_collection(resource_type,
            parent_uuids, back_ref_uuids, obj_uuids, is_count, is_detail,
            filters, req_fields)
    # end http_resource_list

    def create_default_children(self, resource_type, parent_obj):
        r_class = self.get_resource_class(resource_type)
        for child_field in r_class.children_fields:
            # Create a default child only if provisioned for
            child_type, is_derived = r_class.children_field_types[child_field]
            if is_derived:
                continue
            child_cls = self.get_resource_class(child_type)
            if not child_cls.generate_default_instance:
                continue
            child_obj = child_cls(parent_obj=parent_obj)
            child_dict = child_obj.__dict__
            child_dict['id_perms'] = self._get_default_id_perms(child_type)
            (ok, result) = self._db_conn.dbe_alloc(child_type, child_dict)
            if not ok:
                return (ok, result)

            obj_ids = result
            self._db_conn.dbe_create(child_type, obj_ids, child_dict)
            # recurse down type hierarchy
            self.create_default_children(child_type, child_obj)
    # end create_default_children

    def delete_default_children(self, resource_type, parent_dict):
        r_class = self.get_resource_class(resource_type)
        for child_field in r_class.children_fields:
            # Delete a default child only if provisioned for
            child_type, is_derived = r_class.children_field_types[child_field]
            child_cls = self.get_resource_class(child_type)
            if not child_cls.generate_default_instance:
                continue
            # first locate default child then delete it")
            default_child_name = 'default-%s' %(child_cls().get_type())
            child_infos = parent_dict.get(child_field, [])
            for child_info in child_infos:
                if child_info['to'][-1] == default_child_name:
                    default_child_id = has_info['href'].split('/')[-1]
                    del_method = getattr(self, '%s_http_delete' %(child_type))
                    del_method(default_child_id)
                    break
    # end delete_default_children

    @classmethod
    def _generate_resource_crud_methods(cls, obj):
        for resource_type in gen.vnc_api_server_gen.all_resource_types:
            obj_type = resource_type.replace('-', '_')
            create_method = functools.partial(obj.http_resource_create,
                                              resource_type)
            functools.update_wrapper(create_method, obj.http_resource_create)
            setattr(obj, '%ss_http_post' %(obj_type), create_method)

            read_method = functools.partial(obj.http_resource_read,
                                            resource_type)
            functools.update_wrapper(read_method, obj.http_resource_read)
            setattr(obj, '%s_http_get' %(obj_type), read_method)

            update_method = functools.partial(obj.http_resource_update,
                                              resource_type)
            functools.update_wrapper(update_method, obj.http_resource_update)
            setattr(obj, '%s_http_put' %(obj_type), update_method)

            delete_method = functools.partial(obj.http_resource_delete,
                                              resource_type)
            functools.update_wrapper(delete_method, obj.http_resource_delete)
            setattr(obj, '%s_http_delete' %(obj_type), delete_method)

            list_method = functools.partial(obj.http_resource_list,
                                            resource_type)
            functools.update_wrapper(list_method, obj.http_resource_list)
            setattr(obj, '%ss_http_get' %(obj_type), list_method)

            default_children_method = functools.partial(
                obj.create_default_children, resource_type)
            functools.update_wrapper(default_children_method,
                obj.create_default_children)
            setattr(obj, '_%s_create_default_children' %(obj_type),
                    default_children_method)

            default_children_method = functools.partial(
                obj.delete_default_children, resource_type)
            functools.update_wrapper(default_children_method,
                obj.delete_default_children)
            setattr(obj, '_%s_delete_default_children' %(obj_type),
                    default_children_method)
    # end _generate_resource_crud_methods

    @classmethod
    def _generate_resource_crud_uri(cls, obj):
        for resource_type in gen.vnc_api_server_gen.all_resource_types:
            # CRUD + list URIs of the form
            # obj.route('/virtual-network/<id>', 'GET', obj.virtual_network_http_get)
            # obj.route('/virtual-network/<id>', 'PUT', obj.virtual_network_http_put)
            # obj.route('/virtual-network/<id>', 'DELETE', obj.virtual_network_http_delete)
            # obj.route('/virtual-networks', 'POST', obj.virtual_networks_http_post)
            # obj.route('/virtual-networks', 'GET', obj.virtual_networks_http_get)

            obj_type = resource_type.replace('-', '_')
            # leaf resource
            obj.route('/%s/<id>' %(resource_type),
                      'GET',
                      getattr(obj, '%s_http_get' %(obj_type)))
            obj.route('/%s/<id>' %(resource_type),
                      'PUT',
                      getattr(obj, '%s_http_put' %(obj_type)))
            obj.route('/%s/<id>' %(resource_type),
                      'DELETE',
                      getattr(obj, '%s_http_delete' %(obj_type)))
            # collection of leaf
            obj.route('/%ss' %(resource_type),
                      'POST',
                      getattr(obj, '%ss_http_post' %(obj_type)))
            obj.route('/%ss' %(resource_type),
                      'GET',
                      getattr(obj, '%ss_http_get' %(obj_type)))
    # end _generate_resource_crud_uri

    def __init__(self, args_str=None):
        self._db_conn = None
        self._get_common = None
        self._post_common = None

        self._resource_classes = {}
        for resource_type in gen.vnc_api_server_gen.all_resource_types:
            class_name = '%sServerGen' %(cfgm_common.utils.CamelCase(resource_type))
            self.set_resource_class(resource_type,
                cfgm_common.utils.str_to_class(class_name, __name__))

        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        # set python logging level from logging_level cmdline arg
        if not self._args.logging_conf:
            logging.basicConfig(level = getattr(logging, self._args.logging_level))
        else:
            logging.config.fileConfig(self._args.logging_conf)

        self._base_url = "http://%s:%s" % (self._args.listen_ip_addr,
                                           self._args.listen_port)

        # Generate LinkObjects for all entities
        links = []
        # Link for root
        links.append(LinkObject('root', self._base_url , '/config-root',
                                'config-root'))

        for resource_type in gen.vnc_api_server_gen.all_resource_types:
            link = LinkObject('collection',
                           self._base_url , '/%ss' %(resource_type),
                           '%s' %(resource_type))
            links.append(link)

        for resource_type in gen.vnc_api_server_gen.all_resource_types:
            link = LinkObject('resource-base',
                           self._base_url , '/%s' %(resource_type),
                           '%s' %(resource_type))
            links.append(link)

        self._homepage_links = links

        self._pipe_start_app = None

        #GreenletProfiler.set_clock_type('wall')
        self._profile_info = None

        # REST interface initialization
        self._get_common = self._http_get_common
        self._put_common = self._http_put_common
        self._delete_common = self._http_delete_common
        self._post_validate = self._http_post_validate
        self._post_common = self._http_post_common

        # Type overrides from generated code
        self.set_resource_class('global-system-config',
            vnc_cfg_types.GlobalSystemConfigServer)
        self.set_resource_class('floating-ip', vnc_cfg_types.FloatingIpServer)
        self.set_resource_class('instance-ip', vnc_cfg_types.InstanceIpServer)
        self.set_resource_class('logical-router',
            vnc_cfg_types.LogicalRouterServer)
        self.set_resource_class('security-group',
            vnc_cfg_types.SecurityGroupServer)
        self.set_resource_class('virtual-machine-interface',
            vnc_cfg_types.VirtualMachineInterfaceServer)
        self.set_resource_class('virtual-network',
            vnc_cfg_types.VirtualNetworkServer)
        self.set_resource_class('network-policy',
            vnc_cfg_types.NetworkPolicyServer)
        self.set_resource_class('network-ipam',
            vnc_cfg_types.NetworkIpamServer)
        self.set_resource_class('virtual-DNS', vnc_cfg_types.VirtualDnsServer)
        self.set_resource_class('virtual-DNS-record',
            vnc_cfg_types.VirtualDnsRecordServer)
        self.set_resource_class('logical-interface',
            vnc_cfg_types.LogicalInterfaceServer)
        self.set_resource_class('physical-interface',
            vnc_cfg_types.PhysicalInterfaceServer)

        self.set_resource_class('virtual-ip', vnc_cfg_types.VirtualIpServer)
        self.set_resource_class('loadbalancer-healthmonitor',
            vnc_cfg_types.LoadbalancerHealthmonitorServer)
        self.set_resource_class('loadbalancer-member',
            vnc_cfg_types.LoadbalancerMemberServer)
        self.set_resource_class('loadbalancer-pool',
            vnc_cfg_types.LoadbalancerPoolServer)

        # TODO default-generation-setting can be from ini file
        self.get_resource_class('bgp-router').generate_default_instance = False
        self.get_resource_class(
            'virtual-router').generate_default_instance = False
        self.get_resource_class(
            'access-control-list').generate_default_instance = False
        self.get_resource_class(
            'floating-ip-pool').generate_default_instance = False
        self.get_resource_class('instance-ip').generate_default_instance = False
        self.get_resource_class('logical-router').generate_default_instance = False
        self.get_resource_class('security-group').generate_default_instance = False
        self.get_resource_class(
            'virtual-machine').generate_default_instance = False
        self.get_resource_class(
            'virtual-machine-interface').generate_default_instance = False
        self.get_resource_class(
            'service-template').generate_default_instance = False
        self.get_resource_class(
            'service-instance').generate_default_instance = False
        self.get_resource_class(
            'virtual-DNS-record').generate_default_instance = False
        self.get_resource_class('virtual-DNS').generate_default_instance = False
        self.get_resource_class(
            'global-vrouter-config').generate_default_instance = False
        self.get_resource_class(
            'loadbalancer-pool').generate_default_instance = False
        self.get_resource_class(
            'loadbalancer-member').generate_default_instance = False
        self.get_resource_class(
            'loadbalancer-healthmonitor').generate_default_instance = False
        self.get_resource_class(
            'virtual-ip').generate_default_instance = False
        self.get_resource_class('config-node').generate_default_instance = False
        self.get_resource_class('analytics-node').generate_default_instance = False
        self.get_resource_class('database-node').generate_default_instance = False
        self.get_resource_class('physical-router').generate_default_instance = False
        self.get_resource_class('physical-interface').generate_default_instance = False
        self.get_resource_class('logical-interface').generate_default_instance = False

        for act_res in _ACTION_RESOURCES:
            link = LinkObject('action', self._base_url, act_res['uri'],
                              act_res['link_name'])
            self._homepage_links.append(link)

        # Register for VN delete request. Disallow delete of system default VN
        self.route('/virtual-network/<id>', 'DELETE', self.virtual_network_http_delete)

        bottle.route('/documentation/<filename:path>',
                     'GET', self.documentation_http_get)
        self._homepage_links.insert(
            0, LinkObject('documentation', self._base_url,
                          '/documentation/index.html',
                          'documentation'))

        # APIs to reserve/free block of IP address from a VN/Subnet
        bottle.route('/virtual-network/<id>/ip-alloc',
                     'POST', self.vn_ip_alloc_http_post)
        self._homepage_links.append(
            LinkObject('action', self._base_url,
                       '/virtual-network/%s/ip-alloc',
                       'virtual-network-ip-alloc'))

        bottle.route('/virtual-network/<id>/ip-free',
                     'POST', self.vn_ip_free_http_post)
        self._homepage_links.append(
            LinkObject('action', self._base_url,
                       '/virtual-network/%s/ip-free',
                       'virtual-network-ip-free'))

        # APIs to find out number of ip instances from given VN subnet
        bottle.route('/virtual-network/<id>/subnet-ip-count',
                     'POST', self.vn_subnet_ip_count_http_post)
        self._homepage_links.append(
            LinkObject('action', self._base_url,
                       '/virtual-network/%s/subnet-ip-count',
                       'virtual-network-subnet-ip-count'))

        # Enable/Disable multi tenancy
        bottle.route('/multi-tenancy', 'GET', self.mt_http_get)
        bottle.route('/multi-tenancy', 'PUT', self.mt_http_put)

        # Initialize discovery client
        self._disc = None
        if self._args.disc_server_ip and self._args.disc_server_port:
            self._disc = client.DiscoveryClient(self._args.disc_server_ip,
                                                self._args.disc_server_port,
                                                ModuleNames[Module.API_SERVER])

        # sandesh init
        self._sandesh = Sandesh()
        module = Module.API_SERVER
        module_name = ModuleNames[Module.API_SERVER]
        node_type = Module2NodeType[module]
        node_type_name = NodeTypeNames[node_type]
        if self._args.worker_id:
            instance_id = self._args.worker_id
        else:
            instance_id = INSTANCE_ID_DEFAULT
        hostname = socket.gethostname()
        self._sandesh.init_generator(module_name, hostname,
                                     node_type_name, instance_id,
                                     self._args.collectors,
                                     'vnc_api_server_context',
                                     int(self._args.http_server_port),
                                     ['cfgm_common'], self._disc)
        self._sandesh.trace_buffer_create(name="VncCfgTraceBuf", size=1000)
        self._sandesh.trace_buffer_create(name="RestApiTraceBuf", size=1000)
        self._sandesh.trace_buffer_create(name="DBRequestTraceBuf", size=1000)
        self._sandesh.trace_buffer_create(name="MessageBusNotifyTraceBuf",
                                          size=1000)
        self._sandesh.trace_buffer_create(name="IfmapTraceBuf", size=1000)

        self._sandesh.set_logging_params(
            enable_local_log=self._args.log_local,
            category=self._args.log_category,
            level=self._args.log_level,
            file=self._args.log_file,
            enable_syslog=self._args.use_syslog,
            syslog_facility=self._args.syslog_facility)
        ConnectionState.init(self._sandesh, hostname, module_name,
                instance_id,
                staticmethod(ConnectionState.get_process_state_cb),
                NodeStatusUVE, NodeStatus)

        # Load extensions
        self._extension_mgrs = {}
        self._load_extensions()

        # Address Management interface
        addr_mgmt = vnc_addr_mgmt.AddrMgmt(self)
        vnc_cfg_types.LogicalRouterServer.addr_mgmt = addr_mgmt
        vnc_cfg_types.SecurityGroupServer.addr_mgmt = addr_mgmt
        vnc_cfg_types.VirtualMachineInterfaceServer.addr_mgmt = addr_mgmt
        vnc_cfg_types.FloatingIpServer.addr_mgmt = addr_mgmt
        vnc_cfg_types.InstanceIpServer.addr_mgmt = addr_mgmt
        vnc_cfg_types.VirtualNetworkServer.addr_mgmt = addr_mgmt
        vnc_cfg_types.InstanceIpServer.manager = self
        self._addr_mgmt = addr_mgmt

        # Authn/z interface
        if self._args.auth == 'keystone':
            auth_svc = vnc_auth_keystone.AuthServiceKeystone(self, self._args)
        else:
            auth_svc = vnc_auth.AuthService(self, self._args)

        self._pipe_start_app = auth_svc.get_middleware_app()
        if not self._pipe_start_app:
            self._pipe_start_app = bottle.app()
            # When the multi tenancy is disable, add 'admin' role into the
            # header for all requests to see all resources
            @self._pipe_start_app.hook('before_request')
            @bottle.hook('before_request')
            def set_admin_role(*args, **kwargs):
                if bottle.request.app != self._pipe_start_app:
                    return
                bottle.request.environ['HTTP_X_ROLE'] = 'admin'

        self._auth_svc = auth_svc

        # API/Permissions check
        self._permissions = vnc_perms.VncPermissions(self, self._args)

        # DB interface initialization
        if self._args.wipe_config:
            self._db_connect(True)
        else:
            self._db_connect(self._args.reset_config)
            self._db_init_entries()

        # Cpuinfo interface
        sysinfo_req = True
        config_node_ip = self.get_server_ip()
        cpu_info = vnc_cpu_info.CpuInfo(
            self._sandesh.module(), self._sandesh.instance_id(), sysinfo_req,
            self._sandesh, 60, config_node_ip)
        self._cpu_info = cpu_info

    # end __init__

    def _extensions_transform_request(self, request):
        extensions = self._extension_mgrs.get('resourceApi')
        if not extensions or not extensions.names():
            return None
        return extensions.map_method(
                    'transform_request', request)
    # end _extensions_transform_request

    def _extensions_validate_request(self, request):
        extensions = self._extension_mgrs.get('resourceApi')
        if not extensions or not extensions.names():
            return None
        return extensions.map_method(
                    'validate_request', request)
    # end _extensions_validate_request

    def _extensions_transform_response(self, request, response):
        extensions = self._extension_mgrs.get('resourceApi')
        if not extensions or not extensions.names():
            return None
        return extensions.map_method(
                    'transform_response', request, response)
    # end _extensions_transform_response

    @ignore_exceptions
    def _generate_rest_api_request_trace(self):
        method = bottle.request.method.upper()
        if method == 'GET':
            return None

        req_id = bottle.request.headers.get('X-Request-Id',
                                            'req-%s' %(str(uuid.uuid4())))
        gevent.getcurrent().trace_request_id = req_id
        url = bottle.request.url
        if method == 'DELETE':
            req_data = ''
        else:
            try:
                req_data = json.dumps(bottle.request.json)
            except Exception as e:
                req_data = '%s: Invalid request body' %(e)
        rest_trace = RestApiTrace(request_id=req_id)
        rest_trace.url = url
        rest_trace.method = method
        rest_trace.request_data = req_data
        return rest_trace
    # end _generate_rest_api_request_trace

    @ignore_exceptions
    def _generate_rest_api_response_trace(self, rest_trace, response):
        if not rest_trace:
            return

        rest_trace.status = bottle.response.status
        rest_trace.response_body = json.dumps(response)
        rest_trace.trace_msg(name='RestApiTraceBuf', sandesh=self._sandesh)
    # end _generate_rest_api_response_trace

    # Public Methods
    def route(self, uri, method, handler):
        def handler_trap_exception(*args, **kwargs):
            trace = None
            try:
                self._extensions_transform_request(bottle.request)
                self._extensions_validate_request(bottle.request)

                trace = self._generate_rest_api_request_trace()
                response = handler(*args, **kwargs)
                self._generate_rest_api_response_trace(trace, response)

                self._extensions_transform_response(bottle.request, response)

                return response
            except Exception as e:
                if trace:
                    trace.trace_msg(name='RestApiTraceBuf',
                        sandesh=self._sandesh)
                # don't log details of bottle.abort i.e handled error cases
                if not isinstance(e, bottle.HTTPError):
                    string_buf = StringIO()
                    cgitb.Hook(
                        file=string_buf,
                        format="text",
                        ).handle(sys.exc_info())
                    err_msg = mask_password(string_buf.getvalue())
                    self.config_log(err_msg, level=SandeshLevel.SYS_ERR)

                raise

        bottle.route(uri, method, handler_trap_exception)
    # end route

    def get_args(self):
        return self._args
    # end get_args

    def get_server_ip(self):
        ip_list = []
        for i in netifaces.interfaces():
            try:
                if netifaces.AF_INET in netifaces.ifaddresses(i):
                    addr = netifaces.ifaddresses(i)[netifaces.AF_INET][0][
                        'addr']
                    if addr != '127.0.0.1' and addr not in ip_list:
                        ip_list.append(addr)
            except ValueError, e:
                self.config_log("Skipping interface %s" % i,
                                level=SandeshLevel.SYS_DEBUG)
        return ip_list
    # end get_server_ip

    def get_listen_ip(self):
        return self._args.listen_ip_addr
    # end get_listen_ip

    def get_server_port(self):
        return self._args.listen_port
    # end get_server_port

    def get_pipe_start_app(self):
        return self._pipe_start_app
    # end get_pipe_start_app

    def is_admin_request(self):
        env = bottle.request.headers.environ
        for field in ('HTTP_X_API_ROLE', 'HTTP_X_ROLE'):
            if field in env:
                roles = env[field].split(',')
                return 'admin' in [x.lower() for x in roles]
        return False

    # Check for the system created VN. Disallow such VN delete
    def virtual_network_http_delete(self, id):
        db_conn = self._db_conn
        # if obj doesn't exist return early
        try:
            obj_type = db_conn.uuid_to_obj_type(id)
            if obj_type != 'virtual_network':
                bottle.abort(404, 'No virtual-network object found for id %s' %(id))
            vn_name = db_conn.uuid_to_fq_name(id)
        except NoIdError:
            bottle.abort(404, 'ID %s does not exist' %(id))
        if vn_name == cfgm_common.IP_FABRIC_VN_FQ_NAME or \
           vn_name == cfgm_common.LINK_LOCAL_VN_FQ_NAME:
            bottle.abort(409, 'Can not delete system created default virtual-network %s' %(id))
        super(VncApiServer, self).virtual_network_http_delete(id)
   # end

    def homepage_http_get(self):
        json_body = {}
        json_links = []
        # strip trailing '/' in url
        url = bottle.request.url[:-1]
        for link in self._homepage_links:
            # strip trailing '/' in url
            json_links.append(
                {'link': link.to_dict(with_url=url)}
            )

        json_body = \
            {"href": url,
             "links": json_links
             }

        return json_body
    # end homepage_http_get

    def documentation_http_get(self, filename):
        # ubuntu packaged path
        doc_root = '/usr/share/doc/contrail-config/doc/contrail-config/html/'
        if not os.path.exists(doc_root):
            # centos packaged path
            doc_root='/usr/share/doc/python-vnc_cfg_api_server/contrial-config/html/'

        return bottle.static_file(
                filename,
                root=doc_root)
    # end documentation_http_get

    def ref_update_http_post(self):
        self._post_common(bottle.request, None, None)
        # grab fields
        obj_type = bottle.request.json.get('type')
        obj_uuid = bottle.request.json.get('uuid')
        ref_type = bottle.request.json.get('ref-type')
        operation = bottle.request.json.get('operation')
        ref_uuid = bottle.request.json.get('ref-uuid')
        ref_fq_name = bottle.request.json.get('ref-fq-name')
        attr = bottle.request.json.get('attr')

        # validate fields
        if None in (obj_type, obj_uuid, ref_type, operation):
            err_msg = 'Bad Request: type/uuid/ref-type/operation is null: '
            err_msg += '%s, %s, %s, %s.' \
                        %(obj_type, obj_uuid, ref_type, operation)
            bottle.abort(400, err_msg)

        if operation.upper() not in ['ADD', 'DELETE']:
            err_msg = 'Bad Request: operation should be add or delete: %s' \
                      %(operation)
            bottle.abort(400, err_msg)

        if not ref_uuid and not ref_fq_name:
            err_msg = 'Bad Request: Either ref-uuid or ref-fq-name must be specified'
            bottle.abort(400, err_msg)

        ref_type = ref_type.replace('-', '_')
        if not ref_uuid:
            try:
                ref_uuid = self._db_conn.fq_name_to_uuid(ref_type, ref_fq_name)
            except NoIdError:
                bottle.abort(404, 'Name ' + pformat(ref_fq_name) + ' not found')

        # type-specific hook
        r_class = self.get_resource_class(obj_type)
        if r_class:
            try:
                fq_name = self._db_conn.uuid_to_fq_name(obj_uuid)
            except NoIdError:
                bottle.abort(404, 'UUID ' + obj_uuid + ' not found')
            try:
                (read_ok, read_result) = self._db_conn.dbe_read(
                                              obj_type, bottle.request.json)
            except NoIdError:
                bottle.abort(404, 'Object Not Found: ' + obj_uuid)
            except Exception as e:
                read_ok = False
                read_result = cfgm_common.utils.detailed_traceback()
            if not read_ok:
                self.config_object_error(obj_uuid, None, obj_type, 'ref_update', read_result)
                bottle.abort(500, read_result)

            obj_dict = read_result
            if operation == 'ADD':
                if ref_type+'_refs' not in obj_dict:
                    obj_dict[ref_type+'_refs'] = []
                obj_dict[ref_type+'_refs'].append({'to':ref_fq_name, 'uuid': ref_uuid, 'attr':attr})
            elif operation == 'DELETE':
                for old_ref in obj_dict.get(ref_type+'_refs', []):
                    if old_ref['to'] == ref_fq_name or old_ref['uuid'] == ref_uuid:
                        obj_dict[ref_type+'_refs'].remove(old_ref)
                        break

            (ok, put_result) = r_class.http_put(obj_uuid, fq_name, obj_dict, self._db_conn)
            if not ok:
                (code, msg) = put_result
                self.config_object_error(obj_uuid, None, obj_type, 'ref_update', msg)
                bottle.abort(code, msg)
        obj_type = obj_type.replace('-', '_')
        try:
            id = self._db_conn.ref_update(obj_type, obj_uuid, ref_type, ref_uuid, {'attr': attr}, operation)
        except NoIdError:
            bottle.abort(404, 'uuid ' + obj_uuid + ' not found')
        apiConfig = VncApiCommon()
        apiConfig.object_type = obj_type.replace('-', '_')
        fq_name = self._db_conn.uuid_to_fq_name(obj_uuid)
        apiConfig.identifier_name=':'.join(fq_name)
        apiConfig.identifier_uuid = obj_uuid
        apiConfig.operation = 'ref-update'
        apiConfig.body = str(bottle.request.json)

        self._set_api_audit_info(apiConfig)
        log = VncApiConfigLog(api_log=apiConfig, sandesh=self._sandesh)
        log.send(sandesh=self._sandesh)

        return {'uuid': id}
    # end ref_update_id_http_post

    def fq_name_to_id_http_post(self):
        self._post_common(bottle.request, None, None)
        obj_type = bottle.request.json['type'].replace('-', '_')
        fq_name = bottle.request.json['fq_name']

        try:
            id = self._db_conn.fq_name_to_uuid(obj_type, fq_name)
        except NoIdError:
            bottle.abort(404, 'Name ' + pformat(fq_name) + ' not found')

        return {'uuid': id}
    # end fq_name_to_id_http_post

    def id_to_fq_name_http_post(self):
        self._post_common(bottle.request, None, None)
        try:
            obj_uuid = bottle.request.json['uuid']
            fq_name = self._db_conn.uuid_to_fq_name(obj_uuid)
        except NoIdError:
            bottle.abort(404, 'UUID ' + obj_uuid + ' not found')

        obj_type = self._db_conn.uuid_to_obj_type(obj_uuid)
        return {'fq_name': fq_name, 'type': obj_type}
    # end id_to_fq_name_http_post

    def ifmap_to_id_http_post(self):
        self._post_common(bottle.request, None, None)
        uuid = self._db_conn.ifmap_id_to_uuid(bottle.request.json['ifmap_id'])
        return {'uuid': uuid}
    # end ifmap_to_id_http_post

    # Enables a user-agent to store and retrieve key-val pair
    # TODO this should be done only for special/quantum plugin
    def useragent_kv_http_post(self):
        self._post_common(bottle.request, None, None)

        oper = bottle.request.json['operation']
        key = bottle.request.json['key']
        val = bottle.request.json.get('value', '')

        # TODO move values to common
        if oper == 'STORE':
            self._db_conn.useragent_kv_store(key, val)
        elif oper == 'RETRIEVE':
            try:
                result = self._db_conn.useragent_kv_retrieve(key)
                return {'value': result}
            except NoUserAgentKey:
                bottle.abort(404, "Unknown User-Agent key " + key)
        elif oper == 'DELETE':
            result = self._db_conn.useragent_kv_delete(key)
        else:
            bottle.abort(404, "Invalid Operation " + oper)

    # end useragent_kv_http_post

    def db_check(self):
        """ Check database for inconsistencies. No update to database """
        check_result = self._db_conn.db_check()

        return {'results': check_result}
    # end db_check

    def fetch_records(self):
        """ Retrieve and return all records """
        result = self._db_conn.db_read()
        return {'results': result}
    # end fetch_records

    def start_profile(self):
        #GreenletProfiler.start()
        pass
    # end start_profile

    def stop_profile(self):
        pass
        #GreenletProfiler.stop()
        #stats = GreenletProfiler.get_func_stats()
        #self._profile_info = stats.print_all()

        #return self._profile_info
    # end stop_profile

    def get_profile_info(self):
        return self._profile_info
    # end get_profile_info

    def get_resource_class(self, resource_type):
        if resource_type.replace('-', '_') in self._resource_classes:
            return self._resource_classes[resource_type.replace('-', '_')]

        cls_name = '%sServerGen' %(cfgm_common.utils.CamelCase(resource_type))
        return cfgm_common.utils.str_to_class(cls_name, __name__)
    # end get_resource_class

    def set_resource_class(self, resource_type, resource_class):
        obj_type = resource_type.replace('-', '_')
        self._resource_classes[obj_type]  = resource_class
    # end set_resource_class

    def list_bulk_collection_http_post(self):
        """ List collection when requested ids don't fit in query params."""

        res_type = bottle.request.json.get('type') # e.g. virtual-network
        if not res_type:
            bottle.abort(400, "Bad Request, no 'type' in POST body")

        obj_class = self.get_resource_class(res_type)
        if not obj_class:
            bottle.abort(400,
                   "Bad Request, Unknown type %s in POST body" %(res_type))

        try:
            parent_ids = bottle.request.json['parent_id'].split(',')
            parent_uuids = [str(uuid.UUID(p_uuid)) for p_uuid in parent_ids]
        except KeyError:
            parent_uuids = None

        try:
            back_ref_ids = bottle.request.json['back_ref_id'].split(',')
            back_ref_uuids = [str(uuid.UUID(b_uuid)) for b_uuid in back_ref_ids]
        except KeyError:
            back_ref_uuids = None

        try:
            obj_ids = bottle.request.json['obj_uuids'].split(',')
            obj_uuids = [str(uuid.UUID(b_uuid)) for b_uuid in obj_ids]
        except KeyError:
            obj_uuids = None

        is_count = bottle.request.json.get('count', False)
        is_detail = bottle.request.json.get('detail', False)

        filter_params = bottle.request.json.get('filters', {})
        if filter_params:
            try:
               ff_key_vals = filter_params.split(',')
               ff_names = [ff.split('==')[0] for ff in ff_key_vals]
               ff_values = [ff.split('==')[1] for ff in ff_key_vals]
               filters = {'field_names': ff_names, 'field_values': ff_values}
            except Exception as e:
               bottle.abort(400, 'Invalid filter ' + filter_params)
        else:
            filters = None

        req_fields = bottle.request.json.get('fields', [])
        if req_fields:
            req_fields = req_fields.split(',')

        return self._list_collection(res_type, parent_uuids, back_ref_uuids,
                                     obj_uuids, is_count, is_detail, filters,
                                     req_fields)
    # end list_bulk_collection_http_post

    # Private Methods
    def _parse_args(self, args_str):
        '''
        Eg. python vnc_cfg_api_server.py --ifmap_server_ip 192.168.1.17
                                         --ifmap_server_port 8443
                                         --ifmap_username test
                                         --ifmap_password test
                                         --cassandra_server_list\
                                             10.1.2.3:9160 10.1.2.4:9160
                                         --redis_server_ip 127.0.0.1
                                         --redis_server_port 6382
                                         --collectors 127.0.0.1:8086
                                         --http_server_port 8090
                                         --listen_ip_addr 127.0.0.1
                                         --listen_port 8082
                                         --admin_port 8095
                                         --log_local
                                         --log_level SYS_DEBUG
                                         --logging_level DEBUG
                                         --logging_conf <logger-conf-file>
                                         --log_category test
                                         --log_file <stdout>
                                         --trace_file /var/log/contrail/vnc_openstack.err
                                         --use_syslog
                                         --syslog_facility LOG_USER
                                         --disc_server_ip 127.0.0.1
                                         --disc_server_port 5998
                                         --worker_id 1
                                         --rabbit_max_pending_updates 4096
                                         --cluster_id <testbed-name>
                                         [--auth keystone]
                                         [--ifmap_server_loc
                                          /home/contrail/source/ifmap-server/]
                                         [--default_encoding ascii ]
        '''
        self._args, _ = utils.parse_args(args_str)
    # end _parse_args

    # sigchld handler is currently not engaged. See comment @sigchld
    def sigchld_handler(self):
        # DB interface initialization
        self._db_connect(reset_config=False)
        self._db_init_entries()
    # end sigchld_handler

    def sigterm_handler(self):
        self.cleanup()
        exit()

    def _load_extensions(self):
        try:
            conf_sections = self._args.config_sections
            self._extension_mgrs['resync'] = ExtensionManager(
                'vnc_cfg_api.resync', api_server_ip=self._args.listen_ip_addr,
                api_server_port=self._args.listen_port,
                conf_sections=conf_sections, sandesh=self._sandesh)
            self._extension_mgrs['resourceApi'] = ExtensionManager(
                'vnc_cfg_api.resourceApi',
                propagate_map_exceptions=True,
                api_server_ip=self._args.listen_ip_addr,
                api_server_port=self._args.listen_port,
                conf_sections=conf_sections, sandesh=self._sandesh)
            self._extension_mgrs['neutronApi'] = ExtensionManager(
                'vnc_cfg_api.neutronApi',
                api_server_ip=self._args.listen_ip_addr,
                api_server_port=self._args.listen_port,
                conf_sections=conf_sections, sandesh=self._sandesh)
        except Exception as e:
            self.config_log("Exception in extension load: %s" %(str(e)),
                level=SandeshLevel.SYS_ERR)
    # end _load_extensions

    def _db_connect(self, reset_config):
        ifmap_ip = self._args.ifmap_server_ip
        ifmap_port = self._args.ifmap_server_port
        user = self._args.ifmap_username
        passwd = self._args.ifmap_password
        cass_server_list = self._args.cassandra_server_list
        redis_server_ip = self._args.redis_server_ip
        redis_server_port = self._args.redis_server_port
        zk_server = self._args.zk_server_ip
        rabbit_servers = self._args.rabbit_server
        rabbit_port = self._args.rabbit_port
        rabbit_user = self._args.rabbit_user
        rabbit_password = self._args.rabbit_password
        rabbit_vhost = self._args.rabbit_vhost
        rabbit_ha_mode = self._args.rabbit_ha_mode

        db_conn = VncDbClient(self, ifmap_ip, ifmap_port, user, passwd,
                              cass_server_list, rabbit_servers, rabbit_port,
                              rabbit_user, rabbit_password, rabbit_vhost,
                              rabbit_ha_mode, reset_config,
                              zk_server, self._args.cluster_id)
        self._db_conn = db_conn
    # end _db_connect

    def _ensure_id_perms_present(self, obj_type, obj_uuid, obj_dict):
        """
        Called at resource creation to ensure that id_perms is present in obj
        """
        # retrieve object and permissions
        id_perms = self._get_default_id_perms(obj_type)

        if (('id_perms' not in obj_dict) or
                (obj_dict['id_perms'] is None)):
            # Resource creation
            if obj_uuid is None:
                obj_dict['id_perms'] = id_perms
                return
            # Resource already exist
            try:
                obj_dict['id_perms'] = self._db_conn.uuid_to_obj_perms(obj_uuid)
            except NoIdError:
                obj_dict['id_perms'] = id_perms

            return

        # retrieve the previous version of the id_perms
        # from the database and update the id_perms with
        # them.
        if obj_uuid is not None:
            try:
                old_id_perms = self._db_conn.uuid_to_obj_perms(obj_uuid)
                for field, value in old_id_perms.items():
                    if value is not None:
                        id_perms[field] = value
            except NoIdError:
                pass

        # not all fields can be updated
        if obj_uuid:
            field_list = ['enable', 'description']
        else:
            field_list = ['enable', 'description', 'user_visible', 'creator']

        # Start from default and update from obj_dict
        req_id_perms = obj_dict['id_perms']
        for key in field_list:
            if key in req_id_perms:
                id_perms[key] = req_id_perms[key]
        # TODO handle perms present in req_id_perms

        obj_dict['id_perms'] = id_perms
    # end _ensure_id_perms_present

    def _get_default_id_perms(self, obj_type):
        id_perms = copy.deepcopy(Provision.defaults.perms)
        id_perms_json = json.dumps(id_perms, default=lambda o: dict((k, v)
                                   for k, v in o.__dict__.iteritems()))
        id_perms_dict = json.loads(id_perms_json)
        return id_perms_dict
    # end _get_default_id_perms

    def _db_init_entries(self):
        # create singleton defaults if they don't exist already in db
        glb_sys_cfg = self._create_singleton_entry(
            GlobalSystemConfig(autonomous_system=64512,
                               config_version=CONFIG_VERSION))
        def_domain = self._create_singleton_entry(Domain())
        ip_fab_vn = self._create_singleton_entry(
            VirtualNetwork(cfgm_common.IP_FABRIC_VN_FQ_NAME[-1]))
        self._create_singleton_entry(RoutingInstance('__default__', ip_fab_vn))
        link_local_vn = self._create_singleton_entry(
            VirtualNetwork(cfgm_common.LINK_LOCAL_VN_FQ_NAME[-1]))
        self._create_singleton_entry(
            RoutingInstance('__link_local__', link_local_vn))

        self._db_conn.db_resync()
        try:
            self._extension_mgrs['resync'].map(self._resync_domains_projects)
        except Exception as e:
            pass
    # end _db_init_entries

    def _resync_domains_projects(self, ext):
        ext.obj.resync_domains_projects()
    # end _resync_domains_projects

    def _create_singleton_entry(self, singleton_obj):
        s_obj = singleton_obj
        obj_type = s_obj.get_type().replace('-', '_')
        fq_name = s_obj.get_fq_name()

        # TODO remove backward compat create mapping in zk
        # for singleton START
        try:
            cass_uuid = self._db_conn._cassandra_db.fq_name_to_uuid(obj_type, fq_name)
            try:
                zk_uuid = self._db_conn.fq_name_to_uuid(obj_type, fq_name)
            except NoIdError:
                # doesn't exist in zookeeper but does so in cassandra,
                # migrate this info to zookeeper
                self._db_conn._zk_db.create_fq_name_to_uuid_mapping(obj_type, fq_name, str(cass_uuid))
        except NoIdError:
            # doesn't exist in cassandra as well as zookeeper, proceed normal
            pass
        # TODO backward compat END


        # create if it doesn't exist yet
        try:
            id = self._db_conn.fq_name_to_uuid(obj_type, fq_name)
        except NoIdError:
            obj_dict = s_obj.serialize_to_json()
            obj_dict['id_perms'] = self._get_default_id_perms(s_obj.get_type())
            (ok, result) = self._db_conn.dbe_alloc(obj_type, obj_dict)
            obj_ids = result
            self._db_conn.dbe_create(obj_type, obj_ids, obj_dict)
            method = '_%s_create_default_children' %(obj_type)
            def_children_method = getattr(self, method)
            def_children_method(s_obj)

        return s_obj
    # end _create_singleton_entry

    def _list_collection(self, resource_type, parent_uuids=None,
                         back_ref_uuids=None, obj_uuids=None,
                         is_count=False, is_detail=False, filters=None,
                         req_fields=None):
        obj_type = resource_type.replace('-', '_') # e.g. virtual_network

        (ok, result) = self._db_conn.dbe_list(obj_type,
                             parent_uuids, back_ref_uuids, obj_uuids, is_count,
                             filters)
        if not ok:
            self.config_object_error(None, None, '%ss' %(obj_type),
                                     'dbe_list', result)
            bottle.abort(404, result)

        # If only counting, return early
        if is_count:
            return {'%ss' %(resource_type): {'count': result}}

        fq_names_uuids = result
        obj_dicts = []
        if not is_detail:
            if not self.is_admin_request():
                obj_ids_list = [{'uuid': obj_uuid} 
                                for _, obj_uuid in fq_names_uuids]
                obj_fields = [u'id_perms']
                (ok, result) = self._db_conn.dbe_read_multi(
                                    obj_type, obj_ids_list, obj_fields)
                if not ok:
                    bottle.abort(404, result)
                for obj_result in result:
                    if obj_result['id_perms'].get('user_visible', True):
                        obj_dict = {}
                        obj_dict['uuid'] = obj_result['uuid']
                        obj_dict['href'] = self.generate_url(resource_type,
                                                         obj_result['uuid'])
                        obj_dict['fq_name'] = obj_result['fq_name']
                        obj_dicts.append(obj_dict)
            else: # admin
                for fq_name, obj_uuid in fq_names_uuids:
                    obj_dict = {}
                    obj_dict['uuid'] = obj_uuid
                    obj_dict['href'] = self.generate_url(resource_type,
                                                         obj_uuid)
                    obj_dict['fq_name'] = fq_name
                    for field in req_fields or []:
                       try:
                           obj_dict[field] = obj_results[obj_uuid][field]
                       except KeyError:
                           pass
                    obj_dicts.append(obj_dict)
        else: #detail
            obj_ids_list = [{'uuid': obj_uuid}
                            for _, obj_uuid in fq_names_uuids]

            obj_class = self.get_resource_class(obj_type)
            obj_fields = list(obj_class.prop_fields) + \
                         list(obj_class.ref_fields)
            if 'fields' in bottle.request.query:
                obj_fields.extend(bottle.request.query.fields.split(','))
            (ok, result) = self._db_conn.dbe_read_multi(
                                obj_type, obj_ids_list, obj_fields)

            if not ok:
                bottle.abort(404, result)

            for obj_result in result:
                obj_dict = {}
                obj_dict['name'] = obj_result['fq_name'][-1]
                obj_dict['href'] = self.generate_url(
                                        resource_type, obj_result['uuid'])
                obj_dict.update(obj_result)
                if 'id_perms' not in obj_dict:
                    # It is possible that the object was deleted, but received
                    # an update after that. We need to ignore it for now. In
                    # future, we should clean up such stale objects
                    continue
                if (obj_dict['id_perms'].get('user_visible', True) or
                    self.is_admin_request()):
                    obj_dicts.append({resource_type: obj_dict})

        return {'%ss' %(resource_type): obj_dicts}
    # end _list_collection

    def get_db_connection(self):
        return self._db_conn
    # end get_db_connection

    def generate_url(self, obj_type, obj_uuid):
        obj_uri_type = obj_type.replace('_', '-')
        try:
            url_parts = bottle.request.urlparts
            return '%s://%s/%s/%s'\
                % (url_parts.scheme, url_parts.netloc, obj_uri_type, obj_uuid)
        except Exception as e:
            return '%s/%s/%s' % (self._base_url, obj_uri_type, obj_uuid)
    # end generate_url

    def config_object_error(self, id, fq_name_str, obj_type,
                            operation, err_str):
        apiConfig = VncApiCommon()
        if obj_type is not None:
            apiConfig.object_type = obj_type.replace('-', '_')
        apiConfig.identifier_name = fq_name_str
        apiConfig.identifier_uuid = id
        apiConfig.operation = operation
        if err_str:
            apiConfig.error = "%s:%s" % (obj_type, err_str)
        self._set_api_audit_info(apiConfig)

        log = VncApiConfigLog(api_log=apiConfig, sandesh=self._sandesh)
        log.send(sandesh=self._sandesh)
    # end config_object_error

    def config_log(self, err_str, level=SandeshLevel.SYS_INFO):
        VncApiError(api_error_msg=err_str, level=level, sandesh=self._sandesh).send(
            sandesh=self._sandesh)
    # end config_log

    def _set_api_audit_info(self, apiConfig):
        apiConfig.url = bottle.request.url
        apiConfig.remote_ip = bottle.request.headers.get('Host')
        useragent = bottle.request.headers.get('X-Contrail-Useragent')
        if not useragent:
            useragent = bottle.request.headers.get('User-Agent')
        apiConfig.useragent = useragent
        apiConfig.user = bottle.request.headers.get('X-User-Name')
        apiConfig.project = bottle.request.headers.get('X-Project-Name')
        apiConfig.domain = bottle.request.headers.get('X-Domain-Name', 'None')
        if apiConfig.domain.lower() == 'none':
            apiConfig.domain = 'default-domain'
        if int(bottle.request.headers.get('Content-Length', 0)) > 0:
            apiConfig.body = str(bottle.request.json)
    # end _set_api_audit_info

    # uuid is parent's for collections
    def _http_get_common(self, request, uuid=None):
        # TODO check api + resource perms etc.
        if self._args.multi_tenancy and uuid:
            if isinstance(uuid, list):
                for u_id in uuid:
                    ok, result = self._permissions.check_perms_read(request,
                                                                    u_id)
                    if not ok:
                        return ok, result
            else:
                return self._permissions.check_perms_read(request, uuid)

        return (True, '')
    # end _http_get_common

    def _http_put_common(self, request, obj_type, obj_uuid, obj_fq_name,
                         obj_dict):
        # If not connected to zookeeper do not allow operations that
        # causes the state change
        if not self._db_conn._zk_db.is_connected():
            return (False,
                    (503, "Not connected to zookeeper. Not able to perform requested action"))

        # If there are too many pending updates to rabbit, do not allow
        # operations that cause state change
        npending = self._db_conn.dbe_oper_publish_pending()
        if (npending >= int(self._args.rabbit_max_pending_updates)):
            err_str = str(MaxRabbitPendingError(npending))
            return (False, (500, err_str))

        if obj_dict:
            fq_name_str = ":".join(obj_fq_name)

            # TODO keep _id_perms.uuid_xxlong immutable in future
            # dsetia - check with ajay regarding comment above
            # if 'id_perms' in obj_dict:
            #    del obj_dict['id_perms']
            if 'id_perms' in obj_dict and obj_dict['id_perms']['uuid']:
                if not self._db_conn.match_uuid(obj_dict, obj_uuid):
                    log_msg = 'UUID mismatch from %s:%s' \
                        % (request.environ['REMOTE_ADDR'],
                           request.environ['HTTP_USER_AGENT'])
                    self.config_object_error(
                        obj_uuid, fq_name_str, obj_type, 'put', log_msg)
                    self._db_conn.set_uuid(obj_type, obj_dict,
                                           uuid.UUID(obj_uuid),
                                           persist=False)

            # TODO remove this when the generator will be adapted to
            # be consistent with the post method
            obj_type = obj_type.replace('_', '-')

            # Ensure object has at least default permissions set
            self._ensure_id_perms_present(obj_type, obj_uuid, obj_dict)

            apiConfig = VncApiCommon()
            apiConfig.object_type = obj_type.replace('-', '_')
            apiConfig.identifier_name = fq_name_str
            apiConfig.identifier_uuid = obj_uuid
            apiConfig.operation = 'put'
            self._set_api_audit_info(apiConfig)
            log = VncApiConfigLog(api_log=apiConfig,
                    sandesh=self._sandesh)
            log.send(sandesh=self._sandesh)

        # TODO check api + resource perms etc.
        if self._args.multi_tenancy:
            return self._permissions.check_perms_write(request, obj_uuid)

        return (True, '')
    # end _http_put_common

    # parent_type needed for perms check. None for derived objects (eg.
    # routing-instance)
    def _http_delete_common(self, request, obj_type, uuid, parent_type):
        # If not connected to zookeeper do not allow operations that
        # causes the state change
        if not self._db_conn._zk_db.is_connected():
            return (False,
                    (503, "Not connected to zookeeper. Not able to perform requested action"))

        # If there are too many pending updates to rabbit, do not allow
        # operations that cause state change
        npending = self._db_conn.dbe_oper_publish_pending()
        if (npending >= int(self._args.rabbit_max_pending_updates)):
            err_str = str(MaxRabbitPendingError(npending))
            return (False, (500, err_str))

        fq_name_str = ":".join(self._db_conn.uuid_to_fq_name(uuid))
        apiConfig = VncApiCommon()
        apiConfig.object_type=obj_type.replace('-', '_')
        apiConfig.identifier_name=fq_name_str
        apiConfig.identifier_uuid = uuid
        apiConfig.operation = 'delete'
        self._set_api_audit_info(apiConfig)
        log = VncApiConfigLog(api_log=apiConfig, sandesh=self._sandesh)
        log.send(sandesh=self._sandesh)

        # TODO check api + resource perms etc.
        if not self._args.multi_tenancy or not parent_type:
            return (True, '')

        """
        Validate parent allows write access. Implicitly trust
        parent info in the object since coming from our DB.
        """
        obj_dict = self._db_conn.uuid_to_obj_dict(uuid)
        parent_fq_name = json.loads(obj_dict['fq_name'])[:-1]
        try:
            parent_uuid = self._db_conn.fq_name_to_uuid(
                parent_type, parent_fq_name)
        except NoIdError:
            # parent uuid could be null for derived resources such as
            # routing-instance
            return (True, '')
        return self._permissions.check_perms_write(request, parent_uuid)
    # end _http_delete_common

    def _http_post_validate(self, obj_type=None, obj_dict=None):
        if not obj_dict:
            return

        def _check_field_present(fname):
            fval = obj_dict.get(fname)
            if not fval:
                bottle.abort(400, "Bad Request, no %s in POST body" %(fname))
            return fval
        fq_name = _check_field_present('fq_name')

        # well-formed name checks
        if illegal_xml_chars_RE.search(fq_name[-1]):
            bottle.abort(400,
                "Bad Request, name has illegal xml characters")
        if obj_type[:].replace('-','_') == 'route_target':
            invalid_chars = self._INVALID_NAME_CHARS - set(':')
        else:
            invalid_chars = self._INVALID_NAME_CHARS
        if any((c in invalid_chars) for c in fq_name[-1]):
            bottle.abort(400,
                "Bad Request, name has one of invalid chars %s"
                %(invalid_chars))
    # end _http_post_validate

    def _http_post_common(self, request, obj_type, obj_dict):
        # If not connected to zookeeper do not allow operations that
        # causes the state change
        if not self._db_conn._zk_db.is_connected():
            return (False,
                    (503, "Not connected to zookeeper. Not able to perform requested action"))
        if not obj_dict:
            # TODO check api + resource perms etc.
            return (True, None)

        # If there are too many pending updates to rabbit, do not allow
        # operations that cause state change
        npending = self._db_conn.dbe_oper_publish_pending()
        if (npending >= int(self._args.rabbit_max_pending_updates)):
            err_str = str(MaxRabbitPendingError(npending))
            return (False, (500, err_str))

        # Fail if object exists already
        try:
            obj_uuid = self._db_conn.fq_name_to_uuid(
                obj_type, obj_dict['fq_name'])
            bottle.abort(
                409, '' + pformat(obj_dict['fq_name']) +
                ' already exists with uuid: ' + obj_uuid)
        except NoIdError:
            pass

        # Ensure object has at least default permissions set
        self._ensure_id_perms_present(obj_type, None, obj_dict)

        # TODO check api + resource perms etc.

        uuid_in_req = obj_dict.get('uuid', None)

        # Set the display name
        if (('display_name' not in obj_dict) or
            (obj_dict['display_name'] is None)):
            obj_dict['display_name'] = obj_dict['fq_name'][-1]

        fq_name_str = ":".join(obj_dict['fq_name'])
        apiConfig = VncApiCommon()
        apiConfig.object_type = obj_type.replace('-', '_')
        apiConfig.identifier_name=fq_name_str
        apiConfig.identifier_uuid = uuid_in_req
        apiConfig.operation = 'post'
        apiConfig.body = str(request.json)
        if uuid_in_req:
            try:
                fq_name = self._db_conn.uuid_to_fq_name(uuid_in_req)
                bottle.abort(
                    409, uuid_in_req + ' already exists with fq_name: ' +
                    pformat(fq_name))
            except NoIdError:
                pass
            apiConfig.identifier_uuid = uuid_in_req

        self._set_api_audit_info(apiConfig)
        log = VncApiConfigLog(api_log=apiConfig, sandesh=self._sandesh)
        log.send(sandesh=self._sandesh)

        return (True, uuid_in_req)
    # end _http_post_common

    def cleanup(self):
        # TODO cleanup sandesh context
        pass
    # end cleanup

    # allocate block of IP addresses from VN. Subnet info expected in request
    # body
    def vn_ip_alloc_http_post(self, id):
        try:
            vn_fq_name = self._db_conn.uuid_to_fq_name(id)
        except NoIdError:
            bottle.abort(404, 'Virtual Network ' + id + ' not found!')

        # expected format {"subnet" : "2.1.1.0/24", "count" : 4}
        req_dict = bottle.request.json
        count = req_dict['count'] if 'count' in req_dict else 1
        subnet = req_dict['subnet'] if 'subnet' in req_dict else None
        try:
            result = vnc_cfg_types.VirtualNetworkServer.ip_alloc(
                vn_fq_name, subnet, count)
        except vnc_addr_mgmt.AddrMgmtSubnetUndefined as e:
            bottle.abort(404, str(e))
        except vnc_addr_mgmt.AddrMgmtSubnetExhausted as e:
            bottle.abort(409, str(e))

        return result
    # end vn_ip_alloc_http_post

    # free block of ip addresses to subnet
    def vn_ip_free_http_post(self, id):
        try:
            vn_fq_name = self._db_conn.uuid_to_fq_name(id)
        except NoIdError:
            bottle.abort(404, 'Virtual Network ' + id + ' not found!')

        """
          {
            "subnet" : "2.1.1.0/24",
            "ip_addr": [ "2.1.1.239", "2.1.1.238", "2.1.1.237", "2.1.1.236" ]
          }
        """

        req_dict = bottle.request.json
        ip_list = req_dict['ip_addr'] if 'ip_addr' in req_dict else []
        subnet = req_dict['subnet'] if 'subnet' in req_dict else None
        result = vnc_cfg_types.VirtualNetworkServer.ip_free(
            vn_fq_name, subnet, ip_list)
        return result
    # end vn_ip_free_http_post

    # return no. of  IP addresses from VN/Subnet
    def vn_subnet_ip_count_http_post(self, id):
        try:
            vn_fq_name = self._db_conn.uuid_to_fq_name(id)
        except NoIdError:
            bottle.abort(404, 'Virtual Network ' + id + ' not found!')

        # expected format {"subnet" : ["2.1.1.0/24", "1.1.1.0/24"]
        req_dict = bottle.request.json
        try:
            (ok, result) = self._db_conn.dbe_read('virtual-network', {'uuid': id})
        except NoIdError as e:
            bottle.abort(404, str(e))
        except Exception as e:
            ok = False
            result = cfgm_common.utils.detailed_traceback()
        if not ok:
            bottle.abort(500, result)

        obj_dict = result
        subnet_list = req_dict[
            'subnet_list'] if 'subnet_list' in req_dict else []
        result = vnc_cfg_types.VirtualNetworkServer.subnet_ip_count(
            obj_dict, subnet_list)
        return result
    # end vn_subnet_ip_count_http_post

    def mt_http_get(self):
        pipe_start_app = self.get_pipe_start_app()
        mt = False
        try:
            mt = pipe_start_app.get_mt()
        except AttributeError:
            pass
        return {'enabled': mt}
    # end

    def mt_http_put(self):
        multi_tenancy = bottle.request.json['enabled']
        user_token = bottle.request.get_header('X-Auth-Token')
        if user_token is None:
            bottle.abort(403, " Permission denied")

        data = self._auth_svc.verify_signed_token(user_token)
        if data is None:
            bottle.abort(403, " Permission denied")

        pipe_start_app = self.get_pipe_start_app()
        try:
            pipe_start_app.set_mt(multi_tenancy)
        except AttributeError:
            pass
        self._args.multi_tenancy = multi_tenancy
        return {}
    # end

    def publish_self_to_discovery(self):
        # publish API server
        data = {
            'ip-address': self._args.ifmap_server_ip,
            'port': self._args.listen_port,
        }
        self.api_server_task = self._disc.publish(
            API_SERVER_DISCOVERY_SERVICE_NAME, data)

    def publish_ifmap_to_discovery(self):
        # publish ifmap server
        data = {
            'ip-address': self._args.ifmap_server_ip,
            'port': self._args.ifmap_server_port,
        }
        self.ifmap_task = self._disc.publish(
            IFMAP_SERVER_DISCOVERY_SERVICE_NAME, data)
    # end publish_ifmap_to_discovery

    def un_publish_self_to_discovery(self):
        # un publish api server
        data = {
            'ip-address': self._args.ifmap_server_ip,
            'port': self._args.listen_port,
        }
        self._disc.un_publish(API_SERVER_DISCOVERY_SERVICE_NAME, data)

    def un_publish_ifmap_to_discovery(self):
        # un publish ifmap server
        data = {
            'ip-address': self._args.ifmap_server_ip,
            'port': self._args.ifmap_server_port,
        }
        self._disc.un_publish(IFMAP_SERVER_DISCOVERY_SERVICE_NAME, data)
    # end un_publish_ifmap_to_discovery

# end class VncApiServer

server = None
def main(args_str=None):
    vnc_api_server = VncApiServer(args_str)
    # set module var for uses with import e.g unit test
    global server
    server = vnc_api_server

    pipe_start_app = vnc_api_server.get_pipe_start_app()

    server_ip = vnc_api_server.get_listen_ip()
    server_port = vnc_api_server.get_server_port()

    # Advertise services
    if (vnc_api_server._args.disc_server_ip and
            vnc_api_server._args.disc_server_port):
        vnc_api_server.publish_self_to_discovery()

    """ @sigchld
    Disable handling of SIG_CHLD for now as every keystone request to validate
    token sends SIG_CHLD signal to API server.
    """
    #hub.signal(signal.SIGCHLD, vnc_api_server.sigchld_handler)
    hub.signal(signal.SIGTERM, vnc_api_server.sigterm_handler)

    try:
        bottle.run(app=pipe_start_app, host=server_ip, port=server_port,
                   server=get_bottle_server(server._args.max_requests))
    except KeyboardInterrupt:
        # quietly handle Ctrl-C
        pass
    except:
        # dump stack on all other exceptions
        raise
    finally:
        # always cleanup gracefully
        vnc_api_server.cleanup()

# end main

def server_main(args_str=None):
    import cgitb
    cgitb.enable(format='text')

    main()
#server_main

if __name__ == "__main__":
    server_main()
