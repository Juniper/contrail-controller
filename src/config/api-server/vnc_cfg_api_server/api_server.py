#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
"""
This is the main module in vnc_cfg_api_server package. It manages interaction
between http/rest, address management, authentication and database interfaces.
"""
from __future__ import absolute_import

from future import standard_library
standard_library.install_aliases()

from builtins import str
from builtins import range
from past.builtins import basestring
from builtins import object
from gevent import monkey
monkey.patch_all()
from gevent import hub

# from neutron plugin to api server, the request URL could be large.
# fix the const
import gevent.pywsgi
gevent.pywsgi.MAX_REQUEST_LINE = 65535

import sys
if sys.version_info[0] < 3:
    reload(sys)
    sys.setdefaultencoding('UTF8')
from six import string_types
from six.moves import reload_module
from six.moves.configparser import SafeConfigParser, NoOptionError
import functools
import hashlib
import itertools
import signal
import netaddr
import os
import re
import random
import socket
import ast
from cfgm_common import jsonutils as json
from .provision_defaults import *
import uuid
import copy
from pprint import pformat
from io import StringIO
from vnc_api.utils import AAA_MODE_VALID_VALUES
# import GreenletProfiler
from cfgm_common import vnc_cgitb
from kazoo.exceptions import LockTimeout
from attrdict import AttrDict
from distutils.util import strtobool

from cfgm_common import has_role
from cfgm_common import _obj_serializer_all
from cfgm_common.utils import _DEFAULT_ZK_COUNTER_PATH_PREFIX
from cfgm_common.utils import _DEFAULT_ZK_LOCK_PATH_PREFIX
from cfgm_common import is_uuid_like
from cfgm_common import SG_NO_RULE_FQ_NAME

from cfgm_common.uve.vnc_api.ttypes import VncApiLatencyStats, VncApiLatencyStatsLog

import time
import requests
import xml.etree.ElementTree as etree
from functools import partial

"""
Following is needed to silence warnings on every request when keystone
    auth_token middleware + Sandesh is used. Keystone or Sandesh alone
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

from . import utils
from . import context
from .context import get_request, get_context, set_context, use_context
from .context import ApiContext
from .context import is_internal_request
from .resources import initialize_all_server_resource_classes
from .vnc_db import VncDbClient

import cfgm_common
from cfgm_common import ignore_exceptions
from cfgm_common.uve.vnc_api.ttypes import VncApiCommon, VncApiConfigLog,\
    VncApiDebug, VncApiInfo, VncApiNotice, VncApiError
from cfgm_common.uve.vnc_api.ttypes import FabricJobExecution, FabricJobUve, \
    PhysicalRouterJobExecution, PhysicalRouterJobUve
from cfgm_common import illegal_xml_chars_RE
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames, Module2NodeType,\
    NodeTypeNames, INSTANCE_ID_DEFAULT, TagTypeNameToId,\
    TAG_TYPE_NOT_UNIQUE_PER_OBJECT, TAG_TYPE_AUTHORIZED_ON_ADDRESS_GROUP,\
    POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT, SECURITY_OBJECT_TYPES

from .provision_defaults import Provision
from .vnc_quota import *
from vnc_api.gen.resource_xsd import *
from vnc_api.gen.resource_common import *
from vnc_api.gen.vnc_api_client_gen import all_resource_type_tuples
import cfgm_common
from cfgm_common.utils import cgitb_hook
from cfgm_common.rest import LinkObject, hdr_server_tenant
from cfgm_common.exceptions import *
from cfgm_common.vnc_extensions import ExtensionManager
from . import vnc_addr_mgmt
from . import vnc_auth
from . import vnc_auth_keystone
from . import vnc_perms
from . import vnc_rbac
from cfgm_common.uve.cfgm_cpuinfo.ttypes import ModuleCpuState, ModuleCpuStateTrace
from cfgm_common.buildinfo import build_info
from cfgm_common.vnc_api_stats import log_api_stats

from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
# from gen_py.vnc_api.ttypes import *
import netifaces
from pysandesh.connection_info import ConnectionState
from cfgm_common.uve.nodeinfo.ttypes import NodeStatusUVE, \
    NodeStatus

from .sandesh.traces.ttypes import RestApiTrace
from .vnc_bottle import get_bottle_server
from cfgm_common.vnc_greenlets import VncGreenlet
from cfgm_common.kombu_amqp import KombuAmqpClient
import ssl

_ACTION_RESOURCES = [
    {'uri': '/prop-collection-get', 'link_name': 'prop-collection-get',
     'method': 'GET', 'method_name': 'prop_collection_http_get'},
    {'uri': '/prop-collection-update', 'link_name': 'prop-collection-update',
     'method': 'POST', 'method_name': 'prop_collection_http_post'},
    {'uri': '/ref-update', 'link_name': 'ref-update',
     'method': 'POST', 'method_name': 'ref_update_http_post'},
    {'uri': '/ref-relax-for-delete', 'link_name': 'ref-relax-for-delete',
     'method': 'POST', 'method_name': 'ref_relax_for_delete_http_post'},
    {'uri': '/fqname-to-id', 'link_name': 'name-to-id',
     'method': 'POST', 'method_name': 'fq_name_to_id_http_post'},
    {'uri': '/id-to-fqname', 'link_name': 'id-to-name',
     'method': 'POST', 'method_name': 'id_to_fq_name_http_post'},
    {'uri': '/useragent-kv', 'link_name': 'useragent-keyvalue',
     'method': 'POST', 'method_name': 'useragent_kv_http_post'},
    {'uri': '/db-check', 'link_name': 'database-check',
     'method': 'POST', 'method_name': 'db_check'},
    {'uri': '/fetch-records', 'link_name': 'fetch-records',
     'method': 'POST', 'method_name': 'fetch_records'},
    {'uri': '/start-profile', 'link_name': 'start-profile',
     'method': 'POST', 'method_name': 'start_profile'},
    {'uri': '/stop-profile', 'link_name': 'stop-profile',
     'method': 'POST', 'method_name': 'stop_profile'},
    {'uri': '/list-bulk-collection', 'link_name': 'list-bulk-collection',
     'method': 'POST', 'method_name': 'list_bulk_collection_http_post'},
    {'uri': '/obj-perms', 'link_name': 'obj-perms',
     'method': 'GET', 'method_name': 'obj_perms_http_get'},
    {'uri': '/chown', 'link_name': 'chown',
     'method': 'POST', 'method_name': 'obj_chown_http_post'},
    {'uri': '/chmod', 'link_name': 'chmod',
     'method': 'POST', 'method_name': 'obj_chmod_http_post'},
    {'uri': '/aaa-mode', 'link_name': 'aaa-mode',
     'method': 'PUT', 'method_name': 'aaa_mode_http_put'},
    {'uri': '/obj-cache', 'link_name': 'obj-cache',
     'method': 'GET', 'method_name': 'dump_cache'},
    {'uri': '/obj-cache', 'link_name': 'obj-cache',
     'method': 'POST', 'method_name': 'dump_cache'},
    {'uri': '/execute-job', 'link_name': 'execute-job',
     'method': 'POST', 'method_name': 'execute_job_http_post'},
    {'uri': '/abort-job', 'link_name': 'abort-job',
     'method': 'POST', 'method_name': 'abort_job_http_post'},
    {'uri': '/amqp-publish', 'link_name': 'amqp-publish',
     'method': 'POST', 'method_name': 'amqp_publish_http_post'},
    {'uri': '/amqp-request', 'link_name': 'amqp-request',
     'method': 'POST', 'method_name': 'amqp_request_http_post'},
    {'uri': '/hbs-get', 'link_name': 'hbs-get',
     'method': 'POST', 'method_name': 'hbs_get'},
]

_MANDATORY_PROPS = [
    'loadbalancer_healthmonitor_properties',
]

# following allowed without authentication
_WHITE_LIST_URI = [
    '^/documentation',  # allow all documentation
    '^/$',              # allow discovery
]
_WHITE_LIST_URI_REGEX = re.compile(r'%s' % '|'.join(_WHITE_LIST_URI))


def error_400(err):
    return err.body
# end error_400

def error_403(err):
    return err.body
# end error_403

def error_404(err):
    return err.body
# end error_404

def error_405(err):
    return err.body
# end error_405

def error_409(err):
    return err.body
# end error_409

@bottle.error(412)
def error_412(err):
    return err.body
# end error_412

def error_500(err):
    return err.body
# end error_500

def error_503(err):
    return err.body
# end error_503

def traceback_err_msg(err_msg):
    '''Return the last row of traceback error message'''
    return err_msg.strip('\n').split('\n')[-1]
# end traceback_err_msg


class VncApiServer(object):
    """
    This is the manager class co-ordinating all classes present in the package
    """
    _INVALID_NAME_CHARS = set(':')
    _GENERATE_DEFAULT_INSTANCE = [
        'namespace',
        'project',
        'virtual_network', 'virtual-network',
        'network_ipam', 'network-ipam',
    ]

    JOB_REQUEST_EXCHANGE = "job_request_exchange"
    JOB_REQUEST_ROUTING_KEY = "job.request"
    JOB_ABORT_ROUTING_KEY = "job.abort"

    def __new__(cls, *args, **kwargs):
        obj = super(VncApiServer, cls).__new__(cls)
        obj.api_bottle = bottle.Bottle()
        obj.route('/', 'GET', obj.homepage_http_get)
        obj.api_bottle.error_handler = {
                400: error_400,
                403: error_403,
                404: error_404,
                405: error_405,
                409: error_409,
                500: error_500,
                503: error_503,
            }

        cls._generate_resource_crud_methods(obj)
        cls._generate_resource_crud_uri(obj)
        for act_res in _ACTION_RESOURCES:
            http_method = act_res.get('method', 'POST')
            method_name = getattr(obj, act_res['method_name'])
            obj.route(act_res['uri'], http_method, method_name)
        return obj
    # end __new__

    @classmethod
    def _validate_complex_type(cls, dict_cls, dict_body):
        if dict_body is None:
            return
        for key, value in list(dict_body.items()):
            if key not in dict_cls.attr_fields:
                raise ValueError('class %s does not have field %s' % (
                                  str(dict_cls), key))
            attr_type_vals = dict_cls.attr_field_type_vals[key]
            attr_type = attr_type_vals['attr_type']
            restrictions = attr_type_vals['restrictions']
            is_array = attr_type_vals.get('is_array', False)
            if not value:
                continue
            if is_array:
                if not isinstance(value, list):
                    raise ValueError('Field %s must be a list. Received value: %s'
                                     % (key, str(value)))
                values = value
            else:
                values = [value]
            if attr_type_vals['is_complex']:
                attr_cls = cfgm_common.utils.str_to_class(attr_type, __name__)
                for item in values:
                    if attr_type == 'AllowedAddressPair':
                        cls._validate_allowed_address_pair_prefix_len(item)
                    if attr_type == 'SubnetType':
                        cls._validate_subnet_type(item)
                    cls._validate_complex_type(attr_cls, item)
            else:
                simple_type = attr_type_vals['simple_type']
                for idx, item in enumerate(values):
                    values[idx] = cls._validate_simple_type(key, attr_type,
                                                            simple_type, item,
                                                            restrictions)
            if not is_array:
                dict_body[key] = values[0]
    # end _validate_complex_type

    @staticmethod
    def _validate_subnet_type(subnet):
        try:
            cidr_str = '%s/%s' % (subnet['ip_prefix'], subnet['ip_prefix_len'])
        except TypeError:
            raise ValueError("Subnet type is invalid")
        try:
            cidr = netaddr.IPNetwork(cidr_str)
        except netaddr.core.AddrFormatError:
            raise ValueError("Subnet type '%s' is invalid" % cidr_str)
        subnet['ip_prefix'] = str(cidr.network)
        subnet['ip_prefix_len'] = cidr.prefixlen

    @classmethod
    def _validate_allowed_address_pair_prefix_len(cls, value):
        '''Do not allow configuration of AAP with
           IPv4 prefix length less than 24 and 120 for IPv6.
           LP #1720118
        '''
        if value['address_mode'] == 'active-standby':
           ip_net_family = netaddr.IPNetwork(value['ip']['ip_prefix']).version
           if ip_net_family == 6 and value['ip']['ip_prefix_len'] < 120:
               raise ValueError('IPv6 Prefix length lesser than 120 is'
                                ' is not acceptable')
           if ip_net_family == 4 and value['ip']['ip_prefix_len'] < 24:
               raise ValueError('IPv4 Prefix length lesser than 24'
                                ' is not acceptable')
    # end _validate_allowed_address_pair_prefix_len

    @classmethod
    def _validate_communityattribute_type(cls, value):
        poss_values = ["no-export",
                       "accept-own",
                       "no-advertise",
                       "no-export-subconfed",
                       "no-reoriginate"]
        if value in poss_values:
            return

        res = re.match('[0-9]+:[0-9]+', value)
        if res is None:
            raise ValueError('Invalid community format %s. '
                             'Change to \'number:number\''
                              % value)

        asn = value.split(':')
        if int(asn[0]) > 65535:
            raise ValueError('Out of range ASN value %s. '
                             'ASN values cannot exceed 65535.'
                             % value)

    @classmethod
    def _validate_serviceinterface_type(cls, value):
        poss_values = ["management",
                       "left",
                       "right"]

        if value in poss_values:
            return

        res = re.match('other[0-9]*', value)
        if res is None:
            raise ValueError('Invalid service interface type %s. '
                             'Valid values are: management|left|right|other[0-9]*'
                              % value)

    def validate_execute_job_input_params(self, request_params):
        device_list = None
        job_template_id = request_params.get('job_template_id')
        job_template_fq_name = request_params.get('job_template_fq_name')

        if not (job_template_id or job_template_fq_name):
            err_msg = "Either job_template_id or job_template_fq_name" \
                      " required in request"
            raise cfgm_common.exceptions.HttpError(400, err_msg)

        # check if the job template id is a valid uuid
        if job_template_id:
            if self.invalid_uuid(job_template_id):
                msg = 'Invalid job-template uuid type %s. uuid type required' \
                      % job_template_id
                raise cfgm_common.exceptions.HttpError(400, msg)
            try:
                job_template_fqname = self._db_conn.uuid_to_fq_name(
                              job_template_id)
                request_params['job_template_fq_name'] = job_template_fqname
            except NoIdError as no_id_exec:
                raise cfgm_common.exceptions.HttpError(404, str(no_id_exec))
            except Exception as e:
                msg = "Error while reading job_template_id: " + str(e)
                raise cfgm_common.exceptions.HttpError(400, msg)
        else:
            # check if the job template fqname is a valid fq_name
            try:
                job_template_id = self._db_conn.fq_name_to_uuid(
                                  "job_template", job_template_fq_name)
                request_params['job_template_id'] = job_template_id
            except NoIdError as no_id_exec:
                raise cfgm_common.exceptions.HttpError(404, str(no_id_exec))
            except Exception as e:
                msg = "Error while reading job_template_fqname: " + str(e)
                raise cfgm_common.exceptions.HttpError(400, msg)

        extra_params = request_params.get('params')
        if extra_params is not None:
            device_list = extra_params.get('device_list')
            if device_list:
                if not isinstance(device_list, list):
                    err_msg = "malformed request param: device_list, " \
                              "expects list"
                    raise cfgm_common.exceptions.HttpError(400, err_msg)

                for device_id in device_list:
                    if not isinstance(device_id, basestring):
                        err_msg = "malformed request param: device_list, " \
                                  "expects list of string device_uuids," \
                                  " found device_uuid %s" % device_id
                        raise cfgm_common.exceptions.HttpError(400, err_msg)
                    # check if the device id passed is a valid uuid
                    if self.invalid_uuid(device_id):
                        msg = 'Invalid device uuid type %s.' \
                              ' uuid type required' % device_id
                        raise cfgm_common.exceptions.HttpError(400, msg)

    def execute_job_http_post(self):
        ''' Payload of execute_job
            job_template_id (Mandatory if no job_template_fq_name): <uuid> of
            the created job_template
            job_template_fq_name (Mandatory if no job_template_id): fqname in
            the format: ["<global-system-config-name>",
                         "<name of the job-template>"]
            input (Type json): Input Schema of the playbook under the
            job_template_id
            params (Type json): Extra_params for the job_manager
            (Eg. device_list)
            E.g. Payload:
            {
             "job_template_id": "<uuid>",
             "params": {
               "device_list": ["<device_uuid1>", "<device_uuid2>", ....
                               "<device_uuidn>"]
             }
            }
        '''
        try:
            self.config_log("Entered execute-job",
                            level=SandeshLevel.SYS_INFO)

            request_params = get_request().json
            msg = "Job Input %s " % json.dumps(request_params)
            self.config_log(msg, level=SandeshLevel.SYS_DEBUG)

            # some basic validation like checking if UUIDs in input are
            # syntactically valid
            self.validate_execute_job_input_params(request_params)

            # get the auth token
            auth_token = get_request().get_header('X-Auth-Token')
            request_params['auth_token'] = auth_token

            # get cluster id
            contrail_cluster_id = get_request().get_header('X-Cluster-ID')
            request_params['contrail_cluster_id'] = contrail_cluster_id

            # get the API config node ip list
            if not self._config_node_list:
                (ok, cfg_node_list, _) = self._db_conn.dbe_list(
                    'config_node', field_names=['config_node_ip_address'])
                if not ok:
                    raise cfgm_common.exceptions.HttpError(
                        500, 'Error in dbe_list while getting the '
                             'config_node_ip_address'
                             ' %s' % cfg_node_list)
                if not cfg_node_list:
                    err_msg = "Config-Node list empty"
                    raise cfgm_common.exceptions.HttpError(404, err_msg)
                for node in cfg_node_list:
                    self._config_node_list.append(node.get(
                        'config_node_ip_address'))
            request_params['api_server_host'] = self._config_node_list

            # generate the job execution id
            execution_id = str(int(round(time.time() * 1000))) + '_' + \
                       str(uuid.uuid4())
            request_params['job_execution_id'] = execution_id

            # publish message to RabbitMQ
            self.publish_job_request(request_params, execution_id)

            self.config_log("Published job message to RabbitMQ."
                            " Execution id: %s" % execution_id,
                            level=SandeshLevel.SYS_INFO)

            return {'job_execution_id': str(execution_id)}
        except cfgm_common.exceptions.HttpError as e:
            raise

    def publish_job_request(self, request_params, job_execution_id):
        try:
            self._amqp_client.publish(
                request_params, self.JOB_REQUEST_EXCHANGE,
                routing_key=self.JOB_REQUEST_ROUTING_KEY,
                serializer='json', retry=True,
                retry_policy={'max_retries': 12,
                              'interval_start': 2,
                              'interval_step': 5,
                              'interval_max': 15})
        except Exception as e:
            msg = "Failed to send job request via RabbitMQ" \
                  " %s %s" % (job_execution_id, repr(e))
            raise cfgm_common.exceptions.HttpError(500, msg)

    def abort_job_http_post(self):
        try:
            self.config_log("Entered abort-job",
                            level=SandeshLevel.SYS_INFO)

            request_params = get_request().json
            msg = "Abort Job Input %s " % json.dumps(request_params)
            self.config_log(msg, level=SandeshLevel.SYS_DEBUG)

            # get the auth token
            auth_token = get_request().get_header('X-Auth-Token')
            request_params['auth_token'] = auth_token

            # get cluster id
            contrail_cluster_id = get_request().get_header('X-Cluster-ID')
            request_params['contrail_cluster_id'] = contrail_cluster_id

            # get the API config node ip list
            if not self._config_node_list:
                (ok, cfg_node_list, _) = self._db_conn.dbe_list(
                    'config_node', field_names=['config_node_ip_address'])
                if not ok:
                    raise cfgm_common.exceptions.HttpError(
                        500, 'Error in dbe_list while getting the '
                             'config_node_ip_address'
                             ' %s' % cfg_node_list)
                if not cfg_node_list:
                    err_msg = "Config-Node list empty"
                    raise cfgm_common.exceptions.HttpError(404, err_msg)
                for node in cfg_node_list:
                    self._config_node_list.append(node.get(
                        'config_node_ip_address'))
            request_params['api_server_host'] = self._config_node_list

            # publish message to RabbitMQ
            self.publish_job_abort(request_params)

            self.config_log("Published job abort to RabbitMQ.",
                            level=SandeshLevel.SYS_INFO)

            return {}
        except cfgm_common.exceptions.HttpError as e:
            raise

    def publish_job_abort(self, request_params):
        try:
            self._amqp_client.publish(
                request_params, self.JOB_REQUEST_EXCHANGE,
                routing_key=self.JOB_ABORT_ROUTING_KEY,
                serializer='json', retry=True,
                retry_policy={'max_retries': 12,
                              'interval_start': 2,
                              'interval_step': 5,
                              'interval_max': 15})
        except Exception as e:
            msg = "Failed to send job abort via RabbitMQ" \
                  " %s" % (repr(e))
            raise cfgm_common.exceptions.HttpError(500, msg)

    def amqp_publish_http_post(self):
        ''' Payload of amqp-publish
            exchange (Type string) (mandatory): name of the exchange
            exchange_type (Type string) (mandatory): type of the exchange
            routing_key (Type string): routing key for the message
            headers (Type dict): headers for the message
            payload (Type object): the message
        '''
        self.config_log("Entered amqp-publish",
                        level=SandeshLevel.SYS_INFO)

        body = get_request().json
        msg = "Amqp publish %s " % json.dumps(body)
        self.config_log(msg, level=SandeshLevel.SYS_DEBUG)

        if self._amqp_client.get_exchange(body.get('exchange')) is None:
            self._amqp_client.add_exchange(body.get('exchange'),
                                           type=body.get('exchange_type'))
        self._amqp_client.publish(body.get('payload'),
                                  body.get('exchange'),
                                  routing_key=body.get('routing_key'),
                                  headers=body.get('headers'))
        bottle.response.status = 202
        self.config_log("Exiting amqp-publish", level=SandeshLevel.SYS_DEBUG)
    # end amqp_publish_http_post

    def amqp_request_http_post(self):
        ''' Payload of amqp-request
            exchange (Type string) (mandatory): name of the exchange
            exchange_type (Type string) (mandatory): type of the exchange
            routing_key (Type string): routing key for the message
            response_key (Type string): routing key for the response message
            headers (Type dict): headers for the message
            payload (Type object): the message
        '''
        self.config_log("Entered amqp-request",
                        level=SandeshLevel.SYS_INFO)

        body = get_request().json
        msg = "Amqp request %s " % json.dumps(body)
        self.config_log(msg, level=SandeshLevel.SYS_DEBUG)

        if self._amqp_client.get_exchange(body.get('exchange')) is None:
            self._amqp_client.add_exchange(body.get('exchange'),
                                           type=body.get('exchange_type'))

        consumer = 'amqp_request.%s.%s' % (socket.getfqdn(), str(uuid.uuid4()))
        amqp_worker = VncApiServer.AmqpWorker()
        self._amqp_client.add_consumer(consumer, body.get('exchange'),
            routing_key=body.get('response_key'),
            callback=amqp_worker.handle_message,
            wait=True, auto_delete=True)

        self._amqp_client.publish(body.get('payload'),
                                  body.get('exchange'),
                                  routing_key=body.get('routing_key'),
                                  headers=body.get('headers'))
        try:
            amqp_worker.queue.get(block=True, timeout=self._args.amqp_timeout)
            bottle.response.status = 200
        except gevent.queue.Empty:
            bottle.response.status = 500
        finally:
            self._amqp_client.remove_consumer(consumer)

        msg = "Amqp response, status %s, body %s " % (bottle.response.status,
            json.dumps(amqp_worker.body))
        self.config_log(msg, level=SandeshLevel.SYS_DEBUG)
        self.config_log("Exiting amqp-request", level=SandeshLevel.SYS_DEBUG)
        return amqp_worker.body
    # end amqp_request_http_post

    class AmqpWorker(object):
        def __init__(self):
            self.queue = gevent.queue.Queue(maxsize=1)
            self.body = None
        # end __init__

        def handle_message(self, body, message):
            message.ack()
            self.body = body
            self.queue.put_nowait(True)
        # end handle_message
    # end AmqpWorker

    @classmethod
    def _validate_simple_type(cls, type_name, xsd_type, simple_type, value, restrictions=None):
        if value is None:
            return
        elif xsd_type in ('unsignedLong', 'integer'):
            if not isinstance(value, (int, int)):
                # If value is not an integer, then try to convert it to integer
                try:
                    value = int(value)
                except (TypeError, ValueError):
                    raise ValueError('%s: integer value expected instead of %s' %(
                        type_name, value))
            if restrictions:
                if not (int(restrictions[0]) <= value <= int(restrictions[1])):
                    raise ValueError('%s: value must be between %s and %s' %(
                                type_name, restrictions[0], restrictions[1]))
        elif xsd_type == 'boolean':
            if not isinstance(value, bool):
                raise ValueError('%s: true/false expected instead of %s' %(
                    type_name, value))
        elif xsd_type == 'string' and simple_type == 'CommunityAttribute':
            cls._validate_communityattribute_type(value)
        elif xsd_type == 'string' and simple_type == 'ServiceInterfaceType':
            cls._validate_serviceinterface_type(value)
        else:
            if not isinstance(value, basestring):
                raise ValueError('%s: string value expected instead of %s' %(
                    type_name, value))
            if restrictions and value not in restrictions:
                raise ValueError('%s: value must be one of %s' % (
                    type_name, str(restrictions)))
        return value
    # end _validate_simple_type

    def _check_mandatory_props_list(self, prop_name):
        return prop_name in _MANDATORY_PROPS
    # end _check_mandatory_props_list

    def _validate_props_in_request(self, resource_class, obj_dict, operation):
        for prop_name in resource_class.prop_fields:
            prop_field_types = resource_class.prop_field_types[prop_name]
            is_simple = not prop_field_types['is_complex']
            prop_type = prop_field_types['xsd_type']
            restrictions = prop_field_types['restrictions']
            simple_type = prop_field_types['simple_type']
            is_list_prop = prop_name in resource_class.prop_list_fields
            is_map_prop = prop_name in resource_class.prop_map_fields

            prop_value = obj_dict.get(prop_name)
            if not prop_value:
                if operation == 'CREATE' and (
                    prop_field_types['required'] == 'required'):
                    if self._check_mandatory_props_list(prop_name):
                        err_msg = '%s property is missing' %prop_name
                        return False, err_msg
                continue

            if is_simple:
                try:
                    obj_dict[prop_name] = self._validate_simple_type(prop_name,
                                              prop_type, simple_type,
                                              prop_value, restrictions)
                except Exception as e:
                    err_msg = 'Error validating property ' + str(e)
                    return False, err_msg
                else:
                    continue

            prop_cls = cfgm_common.utils.str_to_class(prop_type, __name__)
            if isinstance(prop_value, dict):
                try:
                    self._validate_complex_type(prop_cls, prop_value)
                except Exception as e:
                    err_msg = 'Error validating property %s value %s ' %(
                        prop_name, prop_value)
                    err_msg += str(e)
                    return False, err_msg
            else: # complex-type + value isn't dict or wrapped in list or map
                err_msg = 'Error in property %s type %s value of %s ' %(
                    prop_name, prop_cls, prop_value)
                return False, err_msg
        # end for all properties

        return True, ''
    # end _validate_props_in_request

    def _validate_refs_in_request(self, resource_class, obj_dict):
        for ref_name in resource_class.ref_fields:
            ref_fld_types_list = list(resource_class.ref_field_types[ref_name])
            ref_link_type = ref_fld_types_list[1]
            if ref_link_type == 'None':
                continue

            attr_cls = cfgm_common.utils.str_to_class(ref_link_type, __name__)
            for ref_dict in obj_dict.get(ref_name) or []:
                try:
                    self._validate_complex_type(attr_cls, ref_dict['attr'])
                except Exception as e:
                    err_msg = 'Error validating reference %s value %s ' \
                              %(ref_name, ref_dict)
                    err_msg += str(e)
                    return False, err_msg

        return True, ''
    # end _validate_refs_in_request

    @staticmethod
    def _validate_tag_refs(obj_type, obj_dict):
        if not obj_dict:
            return True, ''

        refs_per_type = {}
        for ref in obj_dict.get('tag_refs', []):
            ref_type = ref['to'][-1].partition('=')[0]
            refs_per_type.setdefault(ref_type, []).append(ref)

        for tag_type, refs in list(refs_per_type.items()):
            # Tag type is unique per object, unless
            # TAG_TYPE_NOT_UNIQUE_PER_OBJECT tag type or object type is a
            # Firewall Rule
            if (len(refs) > 1 and
                    tag_type not in TAG_TYPE_NOT_UNIQUE_PER_OBJECT and
                    obj_type != FirewallRule.object_type):
                msg = ("Tag type '%s' cannot be set multiple times on the same "
                       "%s resource type" % (
                           tag_type, obj_type.replace('_', ' ').title()))
                return False, (400, msg)

            # address-group resource can only be associated with label tag type
            if (obj_type == 'address_group' and
                    tag_type not in TAG_TYPE_AUTHORIZED_ON_ADDRESS_GROUP):
                msg = ("Invalid tag type %s for object type %s" %
                       (tag_type, obj_type))
                return False, (400, msg)

        return True, ''

    def _validate_perms_in_request(self, resource_class, obj_type, obj_dict):
        for ref_name in resource_class.ref_fields:
            for ref in obj_dict.get(ref_name) or []:
                try:
                    ref_uuid = ref['uuid']
                except KeyError:
                    ref_uuid = self._db_conn.fq_name_to_uuid(ref_name[:-5],
                                                             ref['to'])
                (ok, status) = self._permissions.check_perms_link(
                    get_request(), ref_uuid)
                if not ok:
                    (code, err_msg) = status
                    raise cfgm_common.exceptions.HttpError(code, err_msg)
    # end _validate_perms_in_request

    def _validate_resource_type(self, type):
        try:
            r_class = self.get_resource_class(type)
            return r_class.resource_type, r_class
        except TypeError:
            raise cfgm_common.exceptions.HttpError(
                404, "Resource type '%s' not found" % type)
    # end _validate_resource_type

    def _ensure_services_conn(
            self, api_name, obj_type, obj_uuid=None, obj_fq_name=None):
        # If not connected to zookeeper do not allow operations that
        # causes the state change
        if not self._db_conn._zk_db.is_connected():
            errmsg = 'No connection to zookeeper.'
            fq_name_str = ':'.join(obj_fq_name or [])
            self.config_object_error(
                obj_uuid, fq_name_str, obj_type, api_name, errmsg)
            raise cfgm_common.exceptions.HttpError(503, errmsg)

        # If there are too many pending updates to rabbit, do not allow
        # operations that cause state change
        npending = self._db_conn.dbe_oper_publish_pending()
        if (npending >= int(self._args.rabbit_max_pending_updates)):
            err_str = str(MaxRabbitPendingError(npending))
            raise cfgm_common.exceptions.HttpError(500, err_str)
    # end _ensure_services_conn

    def undo(self, result, obj_type, id=None, fq_name=None, counter=None, value=0):
        (code, msg) = result
        if counter:
            counter = counter + value
        get_context().invoke_undo(code, msg, self.config_log)

        failed_stage = get_context().get_state()
        self.config_object_error(
            id, fq_name, obj_type, failed_stage, msg)
    # end undo

    # http_resource_<oper> - handlers invoked from
    # a. bottle route (on-the-wire) OR
    # b. internal requests
    # using normalized get_request() from ApiContext
    @log_api_stats
    def http_resource_create(self, obj_type):
        resource_type, r_class = self._validate_resource_type(obj_type)
        obj_dict = get_request().json[resource_type]

        # check visibility
        user_visible = (obj_dict.get('id_perms') or {}).get('user_visible', True)
        if not user_visible and not self.is_admin_request():
            result = 'This object is not visible by users'
            self.config_object_error(None, None, obj_type, 'http_post', result)
            raise cfgm_common.exceptions.HttpError(400, result)

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
            err_msg += cfgm_common.utils.detailed_traceback()
            self.config_log(err_msg, level=SandeshLevel.SYS_NOTICE)

        # properties validator
        ok, result = self._validate_props_in_request(r_class,
                     obj_dict, operation='CREATE')
        if not ok:
            result = 'Bad property in create: ' + result
            raise cfgm_common.exceptions.HttpError(400, result)

        # references validator
        ok, result = self._validate_refs_in_request(r_class, obj_dict)
        if not ok:
            result = 'Bad reference in create: ' + result
            raise cfgm_common.exceptions.HttpError(400, result)

        get_context().set_state('PRE_DBE_ALLOC')
        # type-specific hook
        ok, result = r_class.pre_dbe_alloc(obj_dict)
        if not ok:
            code, msg = result
            raise cfgm_common.exceptions.HttpError(code, msg)

        name = obj_dict['fq_name'][-1]
        fq_name = obj_dict['fq_name']

        # common handling for all resource create
        (ok, result) = self._post_common(obj_type, obj_dict)
        if not ok:
            (code, msg) = result
            fq_name_str = ':'.join(obj_dict.get('fq_name', []))
            self.config_object_error(None, fq_name_str, obj_type, 'http_post',
                                     msg)
            raise cfgm_common.exceptions.HttpError(code, msg)
        uuid_in_req = result

        # no ref to a pending deleted resource
        ok, result = r_class.no_pending_deleted_resource_in_refs(obj_dict)
        if not ok:
            code, msg = result
            raise cfgm_common.exceptions.HttpError(code, msg)

        # Can abort resource creation and retrun 202 status code
        get_context().set_state('PENDING_DBE_CREATE')
        ok, result = r_class.pending_dbe_create(obj_dict)
        if not ok:
            code, msg = result
            raise cfgm_common.exceptions.HttpError(code, msg)
        if ok and isinstance(result, tuple) and result[0] == 202:
            # Creation accepted but not applied, pending delete return 202 HTTP
            # OK code to aware clients
            pending_obj_dict = result[1]
            bottle.response.status = 202
            rsp_body = {}
            rsp_body['fq_name'] = pending_obj_dict['fq_name']
            rsp_body['uuid'] = pending_obj_dict['uuid']
            rsp_body['name'] = pending_obj_dict['fq_name'][-1]
            rsp_body['href'] = self.generate_url(resource_type,
                                                 pending_obj_dict['uuid'])
            rsp_body['parent_type'] = pending_obj_dict['parent_type']
            rsp_body['parent_uuid'] = pending_obj_dict['parent_uuid']
            rsp_body['parent_href'] = self.generate_url(
                pending_obj_dict['parent_type'],pending_obj_dict['parent_uuid'])
            return {resource_type: rsp_body}

        db_conn = self._db_conn

        # if client gave parent_type of config-root, ignore and remove
        if 'parent_type' in obj_dict and obj_dict['parent_type'] == 'config-root':
            del obj_dict['parent_type']

        parent_class = None
        if 'parent_type' in obj_dict:
            # non config-root child, verify parent exists
            parent_res_type, parent_class = self._validate_resource_type(
                 obj_dict['parent_type'])
            parent_obj_type = parent_class.object_type
            parent_res_type = parent_class.resource_type
            parent_fq_name = obj_dict['fq_name'][:-1]
            try:
                parent_uuid = self._db_conn.fq_name_to_uuid(parent_obj_type,
                                                            parent_fq_name)
                (ok, status) = self._permissions.check_perms_write(
                    get_request(), parent_uuid)
                if not ok:
                    (code, err_msg) = status
                    raise cfgm_common.exceptions.HttpError(code, err_msg)
                self._permissions.set_user_role(get_request(), obj_dict)
                obj_dict['parent_uuid'] = parent_uuid
            except NoIdError:
                err_msg = 'Parent %s type %s does not exist' % (
                    pformat(parent_fq_name), parent_res_type)
                fq_name_str = ':'.join(parent_fq_name)
                self.config_object_error(None, fq_name_str, obj_type, 'http_post', err_msg)
                raise cfgm_common.exceptions.HttpError(400, err_msg)

        # Validate perms on references
        try:
            self._validate_perms_in_request(r_class, obj_type, obj_dict)
        except NoIdError:
            raise cfgm_common.exceptions.HttpError(
                400, 'Unknown reference in resource create %s.' %(obj_dict))

        # State modification starts from here. Ensure that cleanup is done for all state changes
        cleanup_on_failure = []
        quota_counter = []

        def stateful_create():
            get_context().set_state('DBE_ALLOC')
            # Alloc and Store id-mappings before creating entry on pubsub store.
            # Else a subscriber can ask for an id mapping before we have stored it
            (ok, result) = db_conn.dbe_alloc(obj_type, obj_dict, uuid_in_req)
            if not ok:
                return (ok, result)
            get_context().push_undo(db_conn.dbe_release, obj_type, fq_name)

            obj_id = result
            env = get_request().headers.environ
            tenant_name = env.get(hdr_server_tenant()) or 'default-project'

            get_context().set_state('PRE_DBE_CREATE')
            # type-specific hook
            (ok, result) = r_class.pre_dbe_create(
                    tenant_name, obj_dict, db_conn)
            if not ok:
                return (ok, result)

            # All resource type can have tag refs but there is some constraints
            # Done after PRE_DBE_CREATE as tag refs can be modifed in that hook
            ok, result = self._validate_tag_refs(obj_type, obj_dict)
            if not ok:
                return False, result

            callable = getattr(r_class, 'http_post_collection_fail', None)
            if callable:
                cleanup_on_failure.append((callable, [tenant_name, obj_dict, db_conn]))

            ok, quota_limit, proj_uuid = r_class.get_quota_for_resource(obj_type,
                                                                        obj_dict, db_conn)
            if not ok:
                return ok, quota_limit

            get_context().set_state('DBE_CREATE')

            if quota_limit >= 0:
                path = self._path_prefix + proj_uuid + "/" + obj_type
                if not self.quota_counter.get(path):
                    # Init quota counter
                    path_prefix = self._path_prefix + proj_uuid
                    try:
                        QuotaHelper._zk_quota_counter_init(
                                   path_prefix, {obj_type: quota_limit}, proj_uuid,
                                   self._db_conn, self.quota_counter)
                    except NoIdError:
                        msg = "Error in initializing quota "\
                              "Internal error : Failed to read resource count"
                        return (False, (404, msg))

                (ok, result) = QuotaHelper.verify_quota_and_create_resource(
                                          db_conn, obj_dict, obj_type, obj_id,
                                          quota_limit, self.quota_counter[path])
                if not ok:
                    return (ok, result)
                else:
                    # To be used for reverting back count when undo() is called
                    try:
                        quota_counter.append(self.quota_counter[path])
                    except KeyError:
                        # project quota could be remove since
                        pass
            else:
                #normal execution
                (ok, result) = db_conn.dbe_create(obj_type, obj_id, obj_dict)
                if not ok:
                    return (ok, result)

            get_context().set_state('POST_DBE_CREATE')
            # type-specific hook
            try:
                ok, result = r_class.post_dbe_create(tenant_name, obj_dict, db_conn)
            except Exception as e:
                ok = False
                msg = ("%s:%s post_dbe_create had an exception: %s\n%s" %
                       (obj_type, obj_id, str(e),
                        cfgm_common.utils.detailed_traceback()))
                result = (None, msg)

            if not ok:
                # Create is done, log to system, no point in informing user
                self.config_log(result[1], level=SandeshLevel.SYS_ERR)

            return True, obj_id
        # end stateful_create

        try:
            ok, result = stateful_create()
        except Exception as e:
            ok = False
            err_msg = cfgm_common.utils.detailed_traceback()
            result = (500, traceback_err_msg(err_msg))
            self.config_log(err_msg, level=SandeshLevel.SYS_ERR)
        if not ok:
            fq_name_str = ':'.join(fq_name)
            self.undo(result, obj_type, fq_name=fq_name_str,
                      counter=quota_counter, value=-1)
            code, msg = result
            raise cfgm_common.exceptions.HttpError(code, msg)

        # Initialize quota counter if resource is project
        if resource_type == 'project' and 'quota' in obj_dict:
            proj_id = obj_dict['uuid']
            path_prefix = self._path_prefix + proj_id
            try:
                QuotaHelper._zk_quota_counter_init(
                    path_prefix,
                    QuotaHelper.get_quota_limits(obj_dict),
                    proj_id,
                    db_conn,
                    self.quota_counter)
            except NoIdError:
                msg = ("Error in initializing quota Internal error : %s" %
                       str(e))
                self.config_log(msg, level=SandeshLevel.SYS_ERR)

        rsp_body = {}
        rsp_body['name'] = name
        rsp_body['fq_name'] = fq_name
        rsp_body['uuid'] = result
        rsp_body['href'] = self.generate_url(resource_type, result)
        if parent_class:
            # non config-root child, send back parent uuid/href
            rsp_body['parent_type'] = obj_dict['parent_type']
            rsp_body['parent_uuid'] = parent_uuid
            rsp_body['parent_href'] = self.generate_url(parent_res_type,
                                                        parent_uuid)

        try:
            self._extension_mgrs['resourceApi'].map_method(
                'post_%s_create' %(obj_type), obj_dict)
        except RuntimeError:
            # lack of registered extension leads to RuntimeError
            pass
        except Exception as e:
            err_msg = 'In post_%s_create an extension had error for %s' \
                      %(obj_type, obj_dict)
            err_msg += cfgm_common.utils.detailed_traceback()
            self.config_log(err_msg, level=SandeshLevel.SYS_NOTICE)

        return {resource_type: rsp_body}
    # end http_resource_create

    @log_api_stats
    def http_resource_read(self, obj_type, id):
        resource_type, r_class = self._validate_resource_type(obj_type)
        try:
            self._extension_mgrs['resourceApi'].map_method(
                'pre_%s_read' %(obj_type), id)
        except Exception as e:
            pass

        etag = get_request().headers.get('If-None-Match')
        db_conn = self._db_conn
        try:
            req_obj_type = db_conn.uuid_to_obj_type(id)
            if req_obj_type != obj_type:
                raise cfgm_common.exceptions.HttpError(
                    404, 'No %s object found for id %s' %(resource_type, id))
            fq_name = db_conn.uuid_to_fq_name(id)
        except NoIdError as e:
            raise cfgm_common.exceptions.HttpError(404, str(e))

        # common handling for all resource get
        (ok, result) = self._get_common(get_request(), id)
        if not ok:
            (code, msg) = result
            self.config_object_error(
                id, None, obj_type, 'http_get', msg)
            raise cfgm_common.exceptions.HttpError(code, msg)

        db_conn = self._db_conn
        if etag:
            (ok, result) = db_conn.dbe_is_latest(id, etag.strip('"'))
            if not ok:
                # Not present in DB
                self.config_object_error(
                    id, None, obj_type, 'http_get', result)
                raise cfgm_common.exceptions.HttpError(404, result)

            is_latest = result
            if is_latest:
                # send Not-Modified, caches use this for read optimization
                bottle.response.status = 304
                return
        # end if etag

        # Generate field list for db layer
        obj_fields = r_class.prop_fields | r_class.ref_fields
        if 'fields' in get_request().query:
            obj_fields = set(get_request().query.fields.split(',')) & (
                obj_fields |
                r_class.backref_fields |
                r_class.children_fields
                ) | set(['id_perms', 'perms2'])
        else: # default props + children + refs + backrefs
            if 'exclude_back_refs' not in get_request().query:
                obj_fields |= r_class.backref_fields
            if 'exclude_children' not in get_request().query:
                obj_fields |= r_class.children_fields

        (ok, result) = r_class.pre_dbe_read(id, fq_name, db_conn)
        if not ok:
            (code, msg) = result
            raise cfgm_common.exceptions.HttpError(code, msg)

        try:
            (ok, result) = db_conn.dbe_read(obj_type, id,
                list(obj_fields), ret_readonly=True)
            if not ok:
                self.config_object_error(id, None, obj_type, 'http_get', result)
        except NoIdError as e:
            # Not present in DB
            raise cfgm_common.exceptions.HttpError(404, str(e))
        if not ok:
            raise cfgm_common.exceptions.HttpError(500, result)

        # check visibility
        if (not result['id_perms'].get('user_visible', True) and
            not self.is_admin_request()):
            result = 'This object is not visible by users: %s' % id
            self.config_object_error(id, None, obj_type, 'http_get', result)
            raise cfgm_common.exceptions.HttpError(404, result)

        if not self.is_admin_request():
            self.obj_view(resource_type, result)

        (ok, err_msg) = r_class.post_dbe_read(result, db_conn)
        if not ok:
            (code, msg) = err_msg
            raise cfgm_common.exceptions.HttpError(code, msg)

        rsp_body = {}
        rsp_body['uuid'] = id
        rsp_body['name'] = result['fq_name'][-1]
        if 'exclude_hrefs' not in get_request().query:
            self.generate_hrefs(resource_type, result)
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

    # filter object references based on permissions
    def obj_view(self, resource_type, obj_dict, ref_perms=None):
        r_class = self.get_resource_class(resource_type)
        obj_links = r_class.obj_links & set(obj_dict.keys())

        if not ref_perms:
            if self.is_rbac_enabled():
                fields = ['perms2']
            else:
                fields = ['id_perms']
            ref_uuids = {ref['uuid'] for link in obj_links
                         for ref in obj_dict[link]}
            ref_perms = {obj_dict['uuid']: obj_dict for obj_dict in
                         self._db_conn._object_db.object_raw_read(
                             resource_type, ref_uuids,fields)}

        for link_field in obj_links:
            links = obj_dict[link_field][:]

            # build new links in returned dict based on permissions on linked
            # object
            for link in links:
                if (link['uuid'] not in ref_perms or
                        not self._permissions.check_perms_read(
                            get_request(),
                            link['uuid'],
                            ref_perms[link['uuid']])[0]):
                    obj_dict[link_field].remove(link)
    # end obj_view

    @log_api_stats
    def http_resource_update(self, obj_type, id):
        resource_type, r_class = self._validate_resource_type(obj_type)

        # Early return if there is no body or an empty body
        request = get_request()
        req_json = request.json

        if not req_json or not req_json[resource_type]:
            return

        obj_dict = get_request().json[resource_type]

        if 'perms2' in obj_dict:
            if 'owner' not in obj_dict['perms2']:
                raise cfgm_common.exceptions.HttpError(400,
                                    'owner in perms2 must be present')

        fields = r_class.prop_fields | r_class.ref_fields
        try:
            ok, result = self._db_conn.dbe_read(obj_type, id, fields)
        except NoIdError as e:
            raise cfgm_common.exceptions.HttpError(404, str(e))
        if not ok:
            self.config_object_error(id, None, obj_type, 'http_resource_update',
                                     result[1])
            raise cfgm_common.exceptions.HttpError(result[0], result[1])
        db_obj_dict = result

        # Look if the resource have a pending version, if yes use it as resource
        # to update
        if hasattr(r_class, 'get_pending_resource'):
            ok, result = r_class.get_pending_resource(db_obj_dict, fields)
            if ok and isinstance(result, dict):
                db_obj_dict = result
                id = obj_dict['uuid'] = db_obj_dict['uuid']
            if not ok and result[0] != 404:
                self.config_object_error(
                    id, None, obj_type, 'http_resource_update', result[1])
                raise cfgm_common.exceptions.HttpError(result[0], result[1])

        self._put_common('http_put', obj_type, id, db_obj_dict,
                         req_obj_dict=obj_dict)

        rsp_body = {}
        rsp_body['uuid'] = id
        rsp_body['href'] = self.generate_url(resource_type, id)

        return {resource_type: rsp_body}
    # end http_resource_update

    @log_api_stats
    def http_resource_delete(self, obj_type, id):
        resource_type, r_class = self._validate_resource_type(obj_type)

        db_conn = self._db_conn
        # if obj doesn't exist return early
        try:
            req_obj_type = db_conn.uuid_to_obj_type(id)
            if req_obj_type != obj_type:
                raise cfgm_common.exceptions.HttpError(
                    404, 'No %s object found for id %s' %(resource_type, id))
            _ = db_conn.uuid_to_fq_name(id)
        except NoIdError:
            raise cfgm_common.exceptions.HttpError(
                404, 'ID %s does not exist' %(id))

        try:
            self._extension_mgrs['resourceApi'].map_method(
                'pre_%s_delete' %(obj_type), id)
        except RuntimeError:
            # lack of registered extension leads to RuntimeError
            pass
        except Exception as e:
            err_msg = 'In pre_%s_delete an extension had error for %s' \
                      %(obj_type, id)
            err_msg += cfgm_common.utils.detailed_traceback()
            self.config_log(err_msg, level=SandeshLevel.SYS_NOTICE)

        # read in obj from db (accepting error) to get details of it
        try:
            (read_ok, read_result) = db_conn.dbe_read(obj_type, id)
        except NoIdError as e:
            raise cfgm_common.exceptions.HttpError(404, str(e))
        if not read_ok:
            self.config_object_error(
                id, None, obj_type, 'http_delete', read_result)
            # proceed down to delete the resource

        # check visibility
        if (not read_result['id_perms'].get('user_visible', True) and
            not self.is_admin_request()):
            result = 'This object is not visible by users: %s' % id
            self.config_object_error(id, None, obj_type, 'http_delete', result)
            raise cfgm_common.exceptions.HttpError(404, result)

        # common handling for all resource delete
        parent_uuid = read_result.get('parent_uuid')
        (ok, del_result) = self._delete_common(
            get_request(), obj_type, id, parent_uuid)
        if not ok:
            (code, msg) = del_result
            self.config_object_error(id, None, obj_type, 'http_delete', msg)
            raise cfgm_common.exceptions.HttpError(code, msg)

        # Permit abort resource deletion and retrun 202 status code
        get_context().set_state('PENDING_DBE_DELETE')
        ok, result = r_class.pending_dbe_delete(read_result)
        if (not ok and isinstance(result, tuple) and result[0] == 409 and
                isinstance(result[1], set)):
            # Found back reference to existing enforced or draft resource
            exist_hrefs = [self.generate_url(type, uuid)
                           for type, uuid in result[1]]
            msg = "Delete when resource still referred: %s" % exist_hrefs
            self.config_object_error(id, None, obj_type, 'http_delete', msg)
            raise cfgm_common.exceptions.HttpError(409, msg)
        elif ok and isinstance(result, tuple) and result[0] == 202:
            # Deletion accepted but not applied, pending delete
            # return 202 HTTP OK code to aware clients
            bottle.response.status = 202
            return
        elif not ok:
            code, msg = result
            raise cfgm_common.exceptions.HttpError(code, msg)

        # fail if non-default children or non-derived backrefs exist
        for child_field in r_class.children_fields:
            child_type, is_derived = r_class.children_field_types[child_field]
            if is_derived:
                continue
            child_cls = self.get_resource_class(child_type)
            default_child_name = 'default-%s' %(
                child_cls(parent_type=obj_type).get_type())
            exist_hrefs = []
            for child in read_result.get(child_field, []):
                if child['to'][-1] in [default_child_name,
                        POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT]:
                    continue
                exist_hrefs.append(
                    self.generate_url(child_type, child['uuid']))
            if exist_hrefs:
                err_msg = 'Delete when children still present: %s' %(
                    exist_hrefs)
                self.config_object_error(
                    id, None, obj_type, 'http_delete', err_msg)
                raise cfgm_common.exceptions.HttpError(409, err_msg)

        relaxed_refs = set(db_conn.dbe_get_relaxed_refs(id))
        for backref_field in r_class.backref_fields:
            backref_type, _, is_derived = \
                r_class.backref_field_types[backref_field]
            if is_derived:
                continue
            exist_hrefs = [self.generate_url(backref_type, backref['uuid'])
                           for backref in read_result.get(backref_field, [])
                               if backref['uuid'] not in relaxed_refs]
            if exist_hrefs:
                err_msg = 'Delete when resource still referred: %s' %(
                    exist_hrefs)
                self.config_object_error(
                    id, None, obj_type, 'http_delete', err_msg)
                raise cfgm_common.exceptions.HttpError(409, err_msg)

        # State modification starts from here. Ensure that cleanup is done for all state changes
        cleanup_on_failure = []
        quota_counter = []
        def stateful_delete():
            get_context().set_state('PRE_DBE_DELETE')

            proj_id = r_class.get_project_id_for_resource(read_result, obj_type,
                                                          db_conn)
            (ok, del_result, zk_del_kwargs) = r_class.pre_dbe_delete(id, read_result, db_conn)
            if not ok:
                if del_result:
                    return (ok, del_result)
                else:
                    return (ok, zk_del_kwargs)
            zk_del_kwargs = zk_del_kwargs or {}

            # Delete default children first
            for child_field in r_class.children_fields:
                child_type, is_derived = r_class.children_field_types[child_field]
                if is_derived:
                    continue
                if child_field in self._GENERATE_DEFAULT_INSTANCE:
                    self.delete_default_children(child_type, read_result)

            callable = getattr(r_class, 'http_delete_fail', None)
            if callable:
                cleanup_on_failure.append((callable, [id, read_result, db_conn]))

            get_context().set_state('DBE_DELETE')
            (ok, del_result) = db_conn.dbe_delete(obj_type, id, read_result)
            if not ok:
                return (ok, del_result)

            if proj_id:
                (ok, proj_dict) = QuotaHelper.get_project_dict_for_quota(
                                      proj_id, db_conn)
                if not ok:
                    return ok, proj_dict
                quota_limit = QuotaHelper.get_quota_limit(proj_dict, obj_type)
                path = self._path_prefix + proj_id + "/" + obj_type
                if quota_limit > 0:
                    if self.quota_counter.get(path):
                        self.quota_counter[path] -= 1
                    else:
                        # quota counter obj not initialized
                        # in this api-server, Init counter
                        path_prefix = self._path_prefix + proj_id
                        QuotaHelper._zk_quota_counter_init(
                            path_prefix, {obj_type : quota_limit},
                            proj_id, db_conn, self.quota_counter)
                        if db_conn._zk_db.quota_counter_exists(path):
                            self.quota_counter[path] -= 1
                    quota_counter.append(self.quota_counter.get(path))
                elif self.quota_counter.get(path):
                    # quota limit is modified to unlimited
                    # delete counter object
                    del self.quota_counter[path]

            # type-specific hook
            get_context().set_state('POST_DBE_DELETE')
            try:
                ok, result = r_class.post_dbe_delete(
                        id, read_result, db_conn, **zk_del_kwargs)
            except Exception as e:
                ok = False
                msg = ("%s:%s post_dbe_delete had an exception: %s\n%s" %
                       (obj_type, id, str(e),
                        cfgm_common.utils.detailed_traceback()))
                result = (None, msg)

            if not ok:
                # Delete is done, log to system, no point in informing user
                self.config_log(result[1], level=SandeshLevel.SYS_ERR)

            return (True, '')
        # end stateful_delete

        try:
            ok, result = stateful_delete()
        except NoIdError as e:
            raise cfgm_common.exceptions.HttpError(
                404, 'No %s object found for id %s' %(resource_type, id))
        except Exception as e:
            ok = False
            err_msg = cfgm_common.utils.detailed_traceback()
            result = (500, traceback_err_msg(err_msg))
            self.config_log(err_msg, level=SandeshLevel.SYS_ERR)
        if not ok:
            self.undo(result, obj_type, id=id, counter=quota_counter, value=1)
            code, msg = result
            raise cfgm_common.exceptions.HttpError(code, msg)

        try:
            self._extension_mgrs['resourceApi'].map_method(
                'post_%s_delete' %(obj_type), id, read_result)
        except RuntimeError:
            # lack of registered extension leads to RuntimeError
            pass
        except Exception as e:
            err_msg = 'In pre_%s_delete an extension had error for %s' \
                      %(obj_type, id)
            err_msg += cfgm_common.utils.detailed_traceback()
            self.config_log(err_msg, level=SandeshLevel.SYS_NOTICE)
    # end http_resource_delete

    @log_api_stats
    def http_resource_list(self, obj_type):
        resource_type, r_class = self._validate_resource_type(obj_type)

        db_conn = self._db_conn
        env = get_request().headers.environ
        parent_uuids = None
        back_ref_uuids = None
        obj_uuids = None
        pagination = {}
        if 'parent_fq_name_str' in get_request().query:
            parent_uuids = []
            parent_fq_name = get_request().query.parent_fq_name_str.split(':')
            parent_types = r_class.parent_types
            if 'parent_type' in get_request().query:
                parent_types = [get_request().query.parent_type]
            for parent_type in parent_types:
                _, p_class = self._validate_resource_type(parent_type)
                try:
                    parent_uuids.append(
                        self._db_conn.fq_name_to_uuid(p_class.object_type,
                                                      parent_fq_name),
                    )
                except cfgm_common.exceptions.NoIdError:
                    pass
        elif 'parent_id' in get_request().query:
            parent_uuids = get_request().query.parent_id.split(',')
        if 'back_ref_id' in get_request().query:
            back_ref_uuids = get_request().query.back_ref_id.split(',')
        if 'obj_uuids' in get_request().query:
            obj_uuids = get_request().query.obj_uuids.split(',')

        if 'fq_names' in get_request().query:
            obj_fqn_strs = get_request().query.fq_names.split(',')
            obj_uuid = None
            for obj_fqn_str in obj_fqn_strs:
                try:
                    obj_fqn = obj_fqn_str.split(':')
                    obj_uuid = self._db_conn.fq_name_to_uuid(obj_type, obj_fqn)
                    if obj_uuids is None:
                        obj_uuids = []
                    obj_uuids.append(obj_uuid)
                except cfgm_common.exceptions.NoIdError as e:
                    pass
            if obj_uuids is None:
                return {'%ss' %(resource_type): []}

        if 'page_marker' in get_request().query:
            pagination['marker'] = self._validate_page_marker(
                                       get_request().query['page_marker'])

        if 'page_limit' in get_request().query:
            pagination['limit'] = self._validate_page_limit(
                                       get_request().query['page_limit'])

        # common handling for all resource get
        for parent_uuid in list(parent_uuids or []):
            (ok, result) = self._get_common(get_request(), parent_uuid)
            if not ok:
                parent_uuids.remove(parent_uuid)

        if obj_uuids is None and back_ref_uuids is None and parent_uuids == []:
            return {'%ss' %(resource_type): []}

        if 'count' in get_request().query:
            is_count = 'true' in get_request().query.count.lower()
        else:
            is_count = False

        if 'detail' in get_request().query:
            is_detail = 'true' in get_request().query.detail.lower()
        else:
            is_detail = False

        if 'fields' in get_request().query:
            req_fields = get_request().query.fields.split(',')
        else:
            req_fields = []

        if 'shared' in get_request().query:
            include_shared = 'true' in get_request().query.shared.lower()
        else:
            include_shared = False

        try:
            filters = utils.get_filters(get_request().query.filters)
        except Exception as e:
            raise cfgm_common.exceptions.HttpError(
                400, 'Invalid filter ' + get_request().query.filters)

        if 'exclude_hrefs' in get_request().query:
            exclude_hrefs = True
        else:
            exclude_hrefs = False

        if 'tags' in get_request().query:
            req_tags = get_request().query.tags.split(',')
        else:
            req_tags = []

        return self._list_collection(obj_type, parent_uuids, back_ref_uuids,
                                     obj_uuids, is_count, is_detail, filters,
                                     req_fields, req_tags, include_shared,
                                     exclude_hrefs, pagination)
    # end http_resource_list

    # internal_request_<oper> - handlers of internally generated requests
    # that save-ctx, generate-ctx and restore-ctx
    def internal_request_create(self, resource_type, obj_json):
        object_type = self.get_resource_class(resource_type).object_type
        try:
            orig_context = get_context()
            orig_request = get_request()
            b_req = bottle.BaseRequest(
                {'PATH_INFO': '/%ss' %(resource_type),
                 'bottle.app': orig_request.environ['bottle.app'],
                 'HTTP_X_USER': 'contrail-api',
                 'HTTP_X_ROLE': self.cloud_admin_role})
            json_as_dict = {'%s' %(resource_type): obj_json}
            i_req = context.ApiInternalRequest(
                b_req.url, b_req.urlparts, b_req.environ, b_req.headers,
                json_as_dict, None)
            set_context(context.ApiContext(internal_req=i_req))
            resp = self.http_resource_create(object_type)
            return True, resp
        finally:
            set_context(orig_context)
    # end internal_request_create

    def internal_request_update(self, resource_type, obj_uuid, obj_json):
        r_class = self.get_resource_class(resource_type)
        try:
            orig_context = get_context()
            orig_request = get_request()
            b_req = bottle.BaseRequest(
                {'PATH_INFO': '/%ss' %(r_class.resource_type),
                 'bottle.app': orig_request.environ['bottle.app'],
                 'HTTP_X_USER': 'contrail-api',
                 'HTTP_X_ROLE': self.cloud_admin_role})
            json_as_dict = {'%s' %(r_class.resource_type): obj_json}
            i_req = context.ApiInternalRequest(
                b_req.url, b_req.urlparts, b_req.environ, b_req.headers,
                json_as_dict, None)
            set_context(context.ApiContext(internal_req=i_req))
            self.http_resource_update(r_class.object_type, obj_uuid)
            return True, ""
        finally:
            set_context(orig_context)
    # end internal_request_update

    def internal_request_delete(self, resource_type, obj_uuid):
        object_type = self.get_resource_class(resource_type).object_type
        try:
            orig_context = get_context()
            orig_request = get_request()
            b_req = bottle.BaseRequest(
                {'PATH_INFO': '/%s/%s' %(resource_type, obj_uuid),
                 'bottle.app': orig_request.environ['bottle.app'],
                 'HTTP_X_USER': 'contrail-api',
                 'HTTP_X_ROLE': self.cloud_admin_role})
            i_req = context.ApiInternalRequest(
                b_req.url, b_req.urlparts, b_req.environ, b_req.headers,
                None, None)
            set_context(context.ApiContext(internal_req=i_req))
            self.http_resource_delete(object_type, obj_uuid)
            return True, ""
        finally:
            set_context(orig_context)
    # end internal_request_delete

    def internal_request_ref_update(self, res_type, obj_uuid, operation,
                                    ref_res_type, ref_uuid=None,
                                    ref_fq_name=None, attr=None,
                                    relax_ref_for_delete=False):
        req_dict = {'type': res_type,
                    'uuid': obj_uuid,
                    'operation': operation,
                    'ref-type': ref_res_type,
                    'ref-uuid': ref_uuid,
                    'ref-fq-name': ref_fq_name,
                    'attr': attr,
                    'relax_ref_for_delete': relax_ref_for_delete}
        try:
            orig_context = get_context()
            orig_request = get_request()
            b_req = bottle.BaseRequest(
                {'PATH_INFO': '/ref-update',
                 'bottle.app': orig_request.environ['bottle.app'],
                 'HTTP_X_USER': 'contrail-api',
                 'HTTP_X_ROLE': self.cloud_admin_role})
            i_req = context.ApiInternalRequest(
                b_req.url, b_req.urlparts, b_req.environ, b_req.headers,
                req_dict, None)
            set_context(context.ApiContext(internal_req=i_req))
            self.ref_update_http_post()
            return True, ""
        finally:
            set_context(orig_context)
    # end internal_request_ref_update

    def internal_request_prop_collection(self, obj_uuid, updates=None):
        req_dict = {
            'uuid': obj_uuid,
            'updates': updates or [],
        }
        try:
            orig_context = get_context()
            orig_request = get_request()
            b_req = bottle.BaseRequest(
                {'PATH_INFO': '/ref-update',
                 'bottle.app': orig_request.environ['bottle.app'],
                 'HTTP_X_USER': 'contrail-api',
                 'HTTP_X_ROLE': self.cloud_admin_role})
            i_req = context.ApiInternalRequest(
                b_req.url, b_req.urlparts, b_req.environ, b_req.headers,
                req_dict, None)
            set_context(context.ApiContext(internal_req=i_req))
            self.prop_collection_http_post()
            return True, ''
        finally:
            set_context(orig_context)

    def alloc_vn_id(self, fq_name_str):
        return self._db_conn._zk_db.alloc_vn_id(fq_name_str)

    def alloc_security_group_id(self, fq_name_str):
        return self._db_conn._zk_db.alloc_sg_id(fq_name_str)

    def alloc_tag_value_id(self, tag_type, name):
        return self._db_conn._zk_db.alloc_tag_value_id(tag_type, name)

    def create_default_children(self, object_type, parent_obj):
        childs = self.get_resource_class(object_type).children_field_types
        # Create a default child only if provisioned for
        child_types = {type for _, (type, derivate) in list(childs.items())
                       if (not derivate and
                           type in self._GENERATE_DEFAULT_INSTANCE)}
        if not child_types:
            return True, ''
        for child_type in child_types:
            child_cls = self.get_resource_class(child_type)
            child_obj_type = child_cls.object_type
            child_obj = child_cls(parent_obj=parent_obj)
            child_dict = child_obj.__dict__
            child_dict['id_perms'] = self._get_default_id_perms()
            child_dict['perms2'] = self._get_default_perms2()
            (ok, result) = self._db_conn.dbe_alloc(child_obj_type, child_dict)
            if not ok:
                return (ok, result)
            obj_id = result

            # For virtual networks, allocate an ID
            if child_obj_type == 'virtual_network':
                child_dict['virtual_network_network_id'] = self.alloc_vn_id(
                    child_obj.get_fq_name_str())

            (ok, result) = self._db_conn.dbe_create(child_obj_type, obj_id,
                                                    child_dict)
            if not ok:
                # DB Create failed, log and stop further child creation.
                err_msg = "DB Create failed creating %s" % child_type
                self.config_log(err_msg, level=SandeshLevel.SYS_ERR)
                return (ok, result)

            # recurse down type hierarchy
            ok, result = self.create_default_children(child_obj_type,
                                                      child_obj)
            if not ok:
                return False, result
        return True, ''
    # end create_default_children

    def delete_default_children(self, resource_type, parent_dict):
        r_class = self.get_resource_class(resource_type)
        for child_field in r_class.children_fields:
            # Delete a default child only if provisioned for
            child_type, is_derived = r_class.children_field_types[child_field]
            if child_type not in self._GENERATE_DEFAULT_INSTANCE:
               continue
            child_cls = self.get_resource_class(child_type)
            # first locate default child then delete it")
            default_child_name = 'default-%s' %(child_type)
            child_infos = parent_dict.get(child_field, [])
            for child_info in child_infos:
                if child_info['to'][-1] == default_child_name:
                    default_child_id = child_info['uuid']
                    self.http_resource_delete(child_type, default_child_id)
                    break
    # end delete_default_children

    @classmethod
    def _generate_resource_crud_methods(cls, obj):
        for object_type, _ in all_resource_type_tuples:
            create_method = functools.partial(obj.http_resource_create,
                                              object_type)
            functools.update_wrapper(create_method, obj.http_resource_create)
            setattr(obj, '%ss_http_post' %(object_type), create_method)

            read_method = functools.partial(obj.http_resource_read,
                                            object_type)
            functools.update_wrapper(read_method, obj.http_resource_read)
            setattr(obj, '%s_http_get' %(object_type), read_method)

            update_method = functools.partial(obj.http_resource_update,
                                              object_type)
            functools.update_wrapper(update_method, obj.http_resource_update)
            setattr(obj, '%s_http_put' %(object_type), update_method)

            delete_method = functools.partial(obj.http_resource_delete,
                                              object_type)
            functools.update_wrapper(delete_method, obj.http_resource_delete)
            setattr(obj, '%s_http_delete' %(object_type), delete_method)

            list_method = functools.partial(obj.http_resource_list,
                                            object_type)
            functools.update_wrapper(list_method, obj.http_resource_list)
            setattr(obj, '%ss_http_get' %(object_type), list_method)
    # end _generate_resource_crud_methods

    @classmethod
    def _generate_resource_crud_uri(cls, obj):
        for object_type, resource_type in all_resource_type_tuples:
            # CRUD + list URIs of the form
            # obj.route('/virtual-network/<id>', 'GET', obj.virtual_network_http_get)
            # obj.route('/virtual-network/<id>', 'PUT', obj.virtual_network_http_put)
            # obj.route('/virtual-network/<id>', 'DELETE', obj.virtual_network_http_delete)
            # obj.route('/virtual-networks', 'POST', obj.virtual_networks_http_post)
            # obj.route('/virtual-networks', 'GET', obj.virtual_networks_http_get)

            # leaf resource
            obj.route('/%s/<id>' %(resource_type),
                      'GET',
                      getattr(obj, '%s_http_get' %(object_type)))
            obj.route('/%s/<id>' %(resource_type),
                      'PUT',
                      getattr(obj, '%s_http_put' %(object_type)))
            obj.route('/%s/<id>' %(resource_type),
                      'DELETE',
                      getattr(obj, '%s_http_delete' %(object_type)))
            # collection of leaf
            obj.route('/%ss' %(resource_type),
                      'POST',
                      getattr(obj, '%ss_http_post' %(object_type)))
            obj.route('/%ss' %(resource_type),
                      'GET',
                      getattr(obj, '%ss_http_get' %(object_type)))
    # end _generate_resource_crud_uri

    def __init__(self, args_str=None):
        self._db_conn = None
        self._resource_classes = initialize_all_server_resource_classes(self)
        self._args = None
        self._path_prefix = _DEFAULT_ZK_COUNTER_PATH_PREFIX
        self.quota_counter = {}
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)
        self.lock_path_prefix = '%s/%s' % (self._args.cluster_id,
                                           _DEFAULT_ZK_LOCK_PATH_PREFIX)
        self.security_lock_prefix = '%s/security' % self.lock_path_prefix

        # set the max size of the api requests
        bottle.BaseRequest.MEMFILE_MAX = self._args.max_request_size

        # multi_tenancy is ignored if aaa_mode is configured by user
        if self._args.aaa_mode is not None:
            if self.aaa_mode not in AAA_MODE_VALID_VALUES:
                self.aaa_mode = AAA_MODE_DEFAULT_VALUE
        elif self._args.multi_tenancy is not None:
            # MT configured by user - determine from aaa-mode
            self.aaa_mode = "cloud-admin" if self._args.multi_tenancy else "no-auth"
        else:
            self.aaa_mode = "cloud-admin"

        api_proto = 'https' if self._args.config_api_ssl_enable else 'http'
        api_host_name = socket.getfqdn(self._args.listen_ip_addr)
        self._base_url = "%s://%s:%s" % (api_proto, api_host_name,
                                        self._args.listen_port)

        # Generate LinkObjects for all entities
        links = []
        # Link for root
        links.append(LinkObject('root', self._base_url , '/config-root',
                                'config-root'))

        for _, resource_type in all_resource_type_tuples:
            link = LinkObject('collection',
                              self._base_url , '/%ss' %(resource_type),
                              '%s' %(resource_type))
            links.append(link)

        for _, resource_type in all_resource_type_tuples:
            link = LinkObject('resource-base',
                              self._base_url , '/%s' %(resource_type),
                              '%s' %(resource_type))
            links.append(link)

        self._homepage_links = links

        self._pipe_start_app = None

        #GreenletProfiler.set_clock_type('wall')
        self._profile_info = None

        for act_res in _ACTION_RESOURCES:
            link = LinkObject('action', self._base_url, act_res['uri'],
                              act_res['link_name'], act_res['method'])
            self._homepage_links.append(link)

        # Register for VN delete request. Disallow delete of system default VN
        self.route('/virtual-network/<id>', 'DELETE', self.virtual_network_http_delete)

        self.route('/documentation/<filename:path>',
                     'GET', self.documentation_http_get)
        self._homepage_links.insert(
            0, LinkObject('documentation', self._base_url,
                          '/documentation/index.html',
                          'documentation', 'GET'))

        # APIs to reserve/free block of IP address from a VN/Subnet
        self.route('/virtual-network/<id>/ip-alloc',
                     'POST', self.vn_ip_alloc_http_post)
        self._homepage_links.append(
            LinkObject('action', self._base_url,
                       '/virtual-network/%s/ip-alloc',
                       'virtual-network-ip-alloc', 'POST'))

        self.route('/virtual-network/<id>/ip-free',
                     'POST', self.vn_ip_free_http_post)
        self._homepage_links.append(
            LinkObject('action', self._base_url,
                       '/virtual-network/%s/ip-free',
                       'virtual-network-ip-free', 'POST'))

        # APIs to find out number of ip instances from given VN subnet
        self.route('/virtual-network/<id>/subnet-ip-count',
                     'POST', self.vn_subnet_ip_count_http_post)
        self._homepage_links.append(
            LinkObject('action', self._base_url,
                       '/virtual-network/%s/subnet-ip-count',
                       'virtual-network-subnet-ip-count', 'POST'))

        # Enable/Disable aaa mode
        self.route('/aaa-mode',      'GET', self.aaa_mode_http_get)
        self.route('/aaa-mode',      'PUT', self.aaa_mode_http_put)

        # Set Tag actions
        self.route('/set-tag', 'POST', self.set_tag)
        self._homepage_links.append(
            LinkObject('action', self._base_url,  '/set-tag', 'set-tag',
                       'POST'))

         # Commit or discard draft security policy
        self.route('/security-policy-draft', 'POST',
                   self.security_policy_draft)
        self._homepage_links.append(
            LinkObject('action', self._base_url,  '/security-policy-draft',
                       'security-policy-draft', 'POST'))

        # randomize the collector list
        self._random_collectors = self._args.collectors
        self._chksum = "";
        if self._args.collectors:
            self._chksum = hashlib.md5(''.join(self._args.collectors)).hexdigest()
            self._random_collectors = random.sample(self._args.collectors, \
                                                    len(self._args.collectors))

        # sandesh init
        self._sandesh = Sandesh()
        # Reset the sandesh send rate limit  value
        if self._args.sandesh_send_rate_limit is not None:
            SandeshSystem.set_sandesh_send_rate_limit(
                self._args.sandesh_send_rate_limit)
        module = Module.API_SERVER
        module_name = ModuleNames[Module.API_SERVER]
        node_type = Module2NodeType[module]
        node_type_name = NodeTypeNames[node_type]
        self.table = "ObjectConfigNode"
        if self._args.worker_id:
            instance_id = self._args.worker_id
        else:
            instance_id = INSTANCE_ID_DEFAULT
        hostname = socket.getfqdn(self._args.listen_ip_addr)
        self._sandesh.init_generator(module_name, hostname,
                                     node_type_name, instance_id,
                                     self._random_collectors,
                                     'vnc_api_server_context',
                                     int(self._args.http_server_port),
                                     ['cfgm_common', 'vnc_cfg_api_server.sandesh'],
                                     logger_class=self._args.logger_class,
                                     logger_config_file=self._args.logging_conf,
                                     config=self._args.sandesh_config)
        self._sandesh.trace_buffer_create(name="VncCfgTraceBuf", size=1000)
        self._sandesh.trace_buffer_create(name="RestApiTraceBuf", size=1000)
        self._sandesh.trace_buffer_create(name="DBRequestTraceBuf", size=1000)
        self._sandesh.trace_buffer_create(name="DBUVERequestTraceBuf", size=1000)
        self._sandesh.trace_buffer_create(name="MessageBusNotifyTraceBuf",
                                          size=1000)
        VncGreenlet.register_sandesh_handler()

        self._sandesh.set_logging_params(
            enable_local_log=self._args.log_local,
            category=self._args.log_category,
            level=self._args.log_level,
            file=self._args.log_file,
            enable_syslog=self._args.use_syslog,
            syslog_facility=self._args.syslog_facility)

        ConnectionState.init(self._sandesh, hostname, module_name,
                instance_id,
                staticmethod(ConnectionState.get_conn_state_cb),
                NodeStatusUVE, NodeStatus, self.table)

        # Address Management interface
        addr_mgmt = vnc_addr_mgmt.AddrMgmt(self)
        self._addr_mgmt = addr_mgmt

        self._default_domain = None
        self._default_project = None

        # DB interface initialization
        if self._args.wipe_config:
            self._db_connect(True)
        else:
            self._db_connect(self._args.reset_config)
            self._db_init_entries()

        self._initialize_quota_counters()

        # API/Permissions check
        # after db init (uses db_conn)
        self._rbac = vnc_rbac.VncRbac(self, self._db_conn)
        self._permissions = vnc_perms.VncPermissions(self, self._args)
        if self.is_rbac_enabled():
            self._create_default_rbac_rule()
        if self.is_auth_needed():
            self._generate_obj_view_links()

        if os.path.exists('/usr/bin/contrail-version'):
            cfgm_cpu_uve = ModuleCpuState()
            cfgm_cpu_uve.name = socket.getfqdn(self._args.listen_ip_addr)
            cfgm_cpu_uve.config_node_ip = self.get_server_ip()

            command = "contrail-version contrail-config | grep 'contrail-config'"
            version = os.popen(command).read()
            if version:
                version = version.split()
            if not version or len(version) != 3:
                # In case of setup from source there is no RPM and version is available in env
                version_str = os.environ.get('CONTRAIL_VERSION', 'tf-master-latest')
                version = re.split('\.|-', version_str)[-3:]
                if len(version) != 3:
                    # in CI version is like changedid-changeset
                    version = ['tf', version_str, version_str]
            _, build_id, build_num = version[:3]

            cfgm_cpu_uve.build_info = build_info + '"build-id" : "' + \
                                      build_id + '", "build-number" : "' + \
                                      build_num + '"}]}'

            cpu_info_trace = ModuleCpuStateTrace(data=cfgm_cpu_uve, sandesh=self._sandesh)
            cpu_info_trace.send(sandesh=self._sandesh)

        self.re_uuid = re.compile('^[0-9A-F]{8}-?[0-9A-F]{4}-?4[0-9A-F]{3}-?[89AB][0-9A-F]{3}-?[0-9A-F]{12}$',
                                  re.IGNORECASE)

        # Load extensions
        self._extension_mgrs = {}
        self._load_extensions()

        # Authn/z interface
        if self._args.auth == 'keystone':
            auth_svc = vnc_auth_keystone.AuthServiceKeystone(self, self._args)
        else:
            auth_svc = vnc_auth.AuthService(self, self._args)

        self._pipe_start_app = auth_svc.get_middleware_app()
        self._auth_svc = auth_svc

        if int(self._args.worker_id) == 0:
            try:
                self._extension_mgrs['resync'].map(
                    self._resync_domains_projects)
            except RuntimeError:
                # lack of registered extension leads to RuntimeError
                pass
            except Exception as e:
                err_msg = cfgm_common.utils.detailed_traceback()
                self.config_log(err_msg, level=SandeshLevel.SYS_ERR)

        self._global_asn = None
        self._enable_4byte_as = None

        # api server list info
        self._config_node_list = []

        # create amqp handle
        self._amqp_client = self.initialize_amqp_client()
    # end __init__

    def _initialize_quota_counters(self):
        ok, result, _ = self._db_conn.dbe_list(
            'project', field_names=['quota'])
        if not ok:
            raise cfgm_common.exceptions.HttpError(result[0], result[1])
        projects = result

        for project in projects or []:
            if project.get('quota'):
                path_prefix = self._path_prefix + project['uuid']
                try:
                    QuotaHelper._zk_quota_counter_init(
                        path_prefix,
                        QuotaHelper.get_quota_limits(project),
                        project['uuid'],
                        self._db_conn,
                        self.quota_counter)
                except NoIdError as e:
                    msg = ("Error in initializing quota Internal error: %s" %
                           str(e))
                    self.config_log(msg, level=SandeshLevel.SYS_ERR)

    def initialize_amqp_client(self):
        amqp_client = None
        use_ssl = None
        try:
            if self._args.rabbit_use_ssl is not None:
                use_ssl = str(self._args.rabbit_use_ssl).lower() == 'true'

            # prepare rabbitMQ params
            rabbitmq_cfg = AttrDict(
                servers=self._args.rabbit_server,
                port=self._args.rabbit_port,
                user=self._args.rabbit_user,
                password=self._args.rabbit_password,
                vhost=self._args.rabbit_vhost,
                ha_mode=self._args.rabbit_ha_mode,
                use_ssl=use_ssl,
                ssl_version=self._args.kombu_ssl_version,
                ssl_keyfile=self._args.kombu_ssl_keyfile,
                ssl_certfile=self._args.kombu_ssl_certfile,
                ssl_ca_certs=self._args.kombu_ssl_ca_certs
            )
            amqp_client = KombuAmqpClient(self.config_log, rabbitmq_cfg,
                heartbeat=self.get_rabbit_health_check_interval())
            amqp_client.add_exchange(self.JOB_REQUEST_EXCHANGE, type="direct")
            amqp_client.run()
            # add dummy consumer to initialize the consumer greenlet
            amqp_client.add_exchange('amqp_request_exchange', type='direct')
            consumer = 'amqp_request.%s.dummy' % socket.getfqdn()
            amqp_client.add_consumer(consumer, 'amqp_request_exchange',
                routing_key='amqp.request.dummy', auto_delete=True)
        except Exception as e:
            err_msg = "Error while initializing the AMQP client %s " % repr(e)
            self.config_log(err_msg, level=SandeshLevel.SYS_ERR)
            if amqp_client is not None:
                amqp_client.stop()
            return None
        return amqp_client

    @property
    def global_autonomous_system(self):
        if not self._global_asn:
            gsc_class = self.get_resource_class(GlobalSystemConfig.object_type)
            ok, result = gsc_class.locate(uuid=self._gsc_uuid, create_it=False,
                                          fields=['autonomous_system'])
            if not ok:
                msg = ("Cannot fetch Global System Config to obtain "
                       "autonomous system")
                raise cfgm_common.exceptions.VncError(msg)
            self._global_asn = result['autonomous_system']
        return self._global_asn

    @global_autonomous_system.setter
    def global_autonomous_system(self, asn):
        self._global_asn = asn

    @property
    def enable_4byte_as(self):
        if not self._enable_4byte_as:
            gsc_class = self.get_resource_class(GlobalSystemConfig.object_type)
            ok, result = gsc_class.locate(uuid=self._gsc_uuid, create_it=False,
                                          fields=['enable_4byte_as'])
            if not ok:
                msg = ("Cannot fetch Global System Config to obtain "
                       "enable_4byte_as flag")
                raise cfgm_common.exceptions.VncError(msg)
            self._enable_4byte_as = result.get('enable_4byte_as', False)
        return self._enable_4byte_as

    @enable_4byte_as.setter
    def enable_4byte_as(self, enable_4byte_as):
        self._enable_4byte_as = enable_4byte_as

    @property
    def default_domain(self):
        if not self._default_domain:
            domain_class = self.get_resource_class(Domain.object_type)
            ok, result = domain_class.locate(
                fq_name=Domain().fq_name, create_it=False)
            if not ok:
                msg = ("Cannot fetch default domain")
                raise cfgm_common.exceptions.VncError(msg)
            self._default_domain = result
        return self._default_domain

    @default_domain.setter
    def default_domain(self, default_domain):
        self._default_domain = default_domain

    @property
    def default_project(self):
        if not self._default_project:
            project_class = self.get_resource_class(Project.object_type)
            ok, result = project_class.locate(
                fq_name=Project().fq_name, create_it=False)
            if not ok:
                msg = ("Cannot fetch default project")
                raise cfgm_common.exceptions.VncError(msg)
            self._default_project = result
        return self._default_project

    @default_project.setter
    def default_project(self, default_project):
        self._default_project = default_project

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
        method = get_request().method.upper()
        if method == 'GET':
            return None

        req_id = get_request().headers.get('X-Request-Id',
                                            'req-%s' %(str(uuid.uuid4())))
        gevent.getcurrent().trace_request_id = req_id
        url = get_request().url
        if method == 'DELETE':
            req_data = ''
        else:
            try:
                req_data = json.dumps(get_request().json)
            except Exception as e:
                req_data = '%s: Invalid request body' %(e)
        rest_trace = RestApiTrace(request_id=req_id)
        rest_trace.url = url
        rest_trace.method = method
        rest_trace.request_data = req_data

        # Also log keystone response time against this request id,
        # before returning the trace message.
        if ((get_context().get_keystone_response_time()) is not None):
            response_time = get_context().get_keystone_response_time()
            response_time_in_usec = ((response_time.days*24*60*60) +
                                      (response_time.seconds*1000000) +
                                      response_time.microseconds)
            stats = VncApiLatencyStats(
                operation_type='VALIDATE',
                application='KEYSTONE',
                response_time_in_usec=response_time_in_usec,
                response_size=0,
                identifier=req_id,
            )
            stats_log = VncApiLatencyStatsLog(node_name="issu-vm6", api_latency_stats=stats, sandesh=self._sandesh)
            x=stats_log.send(sandesh=self._sandesh)
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
        @use_context
        def handler_trap_exception(*args, **kwargs):
            try:
                trace = None
                self._extensions_transform_request(get_request())
                self._extensions_validate_request(get_request())

                trace = self._generate_rest_api_request_trace()
                (ok, status) = self._rbac.validate_request(get_request())
                if not ok:
                    (code, err_msg) = status
                    raise cfgm_common.exceptions.HttpError(code, err_msg)
                response = handler(*args, **kwargs)
                self._generate_rest_api_response_trace(trace, response)

                self._extensions_transform_response(get_request(), response)

                return response
            except Exception as e:
                if trace:
                    trace.trace_msg(name='RestApiTraceBuf',
                        sandesh=self._sandesh)
                # don't log details of cfgm_common.exceptions.HttpError i.e handled error cases
                if isinstance(e, cfgm_common.exceptions.HttpError):
                    bottle.abort(e.status_code, e.content)
                else:
                    string_buf = StringIO()
                    cgitb_hook(file=string_buf, format="text")
                    err_msg = string_buf.getvalue()
                    self.config_log(err_msg, level=SandeshLevel.SYS_ERR)
                    raise

        self.api_bottle.route(uri, method, handler_trap_exception)
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
            except ValueError as e:
                self.config_log("Skipping interface %s: %s" % (i, str(e)),
                                level=SandeshLevel.SYS_DEBUG)
        return ip_list
    # end get_server_ip

    def get_listen_ip(self):
        return self._args.listen_ip_addr
    # end get_listen_ip

    def get_server_port(self):
        return self._args.listen_port
    # end get_server_port

    def get_enable_ssl(self):
        return self._args.config_api_ssl_enable
    # end get_enable_ssl

    def get_keyfile(self):
        return self._args.config_api_ssl_keyfile
    # end get_keyfile

    def get_certfile(self):
        return self._args.config_api_ssl_certfile
    # end get_certfile

    def get_ca_cert(self):
        return self._args.config_api_ssl_ca_cert
    # end get_ca_cert

    def get_worker_id(self):
        return int(self._args.worker_id)
    # end get_worker_id

    def get_pipe_start_app(self):
        return self._pipe_start_app
    # end get_pipe_start_app

    def get_rabbit_health_check_interval(self):
        return float(self._args.rabbit_health_check_interval)
    # end get_rabbit_health_check_interval

    @staticmethod
    def path_in_white_list(path=None):
        if not path:
            path = get_context().path
        return _WHITE_LIST_URI_REGEX.match(path) is not None

    def is_auth_disabled(self):
        return self._args.auth is None or self._args.auth.lower() != 'keystone'

    def is_admin_request(self):
        if not self.is_auth_needed():
            return True

        if is_internal_request():
            return True

        env = bottle.request.headers.environ
        roles = []
        for field in ('HTTP_X_API_ROLE', 'HTTP_X_ROLE'):
            if field in env:
                roles.extend(env[field].split(','))
        return has_role(self.cloud_admin_role, roles)

    def get_auth_headers_from_token(self, request, token):
        if self.is_auth_disabled() or not self.is_auth_needed():
            return {}

        return self._auth_svc.get_auth_headers_from_token(request, token)
    # end get_auth_headers_from_token

    def _generate_obj_view_links(self):
        for object_type, resource_type in all_resource_type_tuples:
            r_class = self.get_resource_class(resource_type)
            r_class.obj_links = (r_class.ref_fields | r_class.backref_fields | r_class.children_fields)

    # Check for the system created VN. Disallow such VN delete
    def virtual_network_http_delete(self, id):
        db_conn = self._db_conn
        # if obj doesn't exist return early
        try:
            obj_type = db_conn.uuid_to_obj_type(id)
            if obj_type != 'virtual_network':
                raise cfgm_common.exceptions.HttpError(
                    404, 'No virtual-network object found for id %s' %(id))
            vn_name = db_conn.uuid_to_fq_name(id)
        except NoIdError:
            raise cfgm_common.exceptions.HttpError(
                404, 'ID %s does not exist' %(id))
        if (vn_name == cfgm_common.IP_FABRIC_VN_FQ_NAME or
            vn_name == cfgm_common.LINK_LOCAL_VN_FQ_NAME):
            raise cfgm_common.exceptions.HttpError(
                409,
                'Can not delete system created default virtual-network '+id)
        super(VncApiServer, self).virtual_network_http_delete(id)
   # end

    @use_context
    def homepage_http_get(self):
        json_body = {}
        json_links = []
        # strip trailing '/' in url
        url = get_request().url[:-1]
        url = url.replace('<script>', '<!--script>')
        url = url.replace('</script>', '</script-->')
        for link in self._homepage_links:
            # strip trailing '/' in url
            json_links.append(
                {'link': link.to_dict(with_url=url)}
            )

        json_body = {"href": url, "links": json_links}

        return json_body
    # end homepage_http_get

    def documentation_http_get(self, filename):
        # ubuntu packaged path
        doc_root = '/usr/share/doc/contrail-config/doc/source/contrail-config/html/'
        if not os.path.exists(doc_root):
            # centos packaged path
            doc_root='/usr/share/doc/python-vnc_cfg_api_server/contrial-config/html/'

        return bottle.static_file(
                filename,
                root=doc_root)
    # end documentation_http_get

    def obj_perms_http_get(self):
        if self.is_auth_disabled() or not self.is_auth_needed():
            result = {
                'token_info': None,
                'is_cloud_admin_role': False,
                'is_global_read_only_role': False,
                'permissions': 'RWX'
            }
            return result

        obj_uuid = None
        if 'uuid' in get_request().query:
            obj_uuid = get_request().query.uuid

        ok, result = self._auth_svc.validate_user_token()
        if not ok:
            code, msg = result
            self.config_object_error(obj_uuid, None, None,
                                     'obj_perms_http_get', msg)
            raise cfgm_common.exceptions.HttpError(code, msg)
        token_info = result

        # roles in result['token_info']['access']['user']['roles']
        result = {'token_info': token_info}
        # Handle v2 and v3 responses
        roles_list = []
        if 'access' in token_info:
            roles_list = [roles['name'] for roles in
                          token_info['access']['user']['roles']]
        elif 'token' in token_info:
            roles_list = [roles['name'] for roles in
                          token_info['token']['roles']]
        result['is_cloud_admin_role'] = has_role(self.cloud_admin_role,
                                                 roles_list)
        result['is_global_read_only_role'] = has_role(
            self.global_read_only_role, roles_list)
        if obj_uuid:
            result['permissions'] = self._permissions.obj_perms(get_request(),
                                                                obj_uuid)
        if 'token' in list(token_info.keys()):
            if 'project' in list(token_info['token'].keys()):
                domain = None
                try:
                    domain = token_info['token']['project']['domain']['id']
                    domain = str(uuid.UUID(domain))
                except ValueError as TypeError:
                    if domain == self._args.default_domain_id:
                        domain = 'default-domain'
                    domain = self._db_conn.fq_name_to_uuid('domain', [domain])
                if domain:
                    domain = domain.replace('-', '')
                    token_info['token']['project']['domain']['id'] = domain
        return result
    # end obj_perms_http_get

    def invalid_uuid(self, uuid):
        return self.re_uuid.match(uuid) is None

    def invalid_access(self, access):
        return type(access) is not int or access not in list(range(0, 8))

    def invalid_share_type(self, share_type):
        return share_type not in cfgm_common.PERMS2_VALID_SHARE_TYPES

    # change ownership of an object
    def obj_chown_http_post(self):
        obj_uuid = get_request().json.get('uuid')
        owner = get_request().json.get('owner')
        if obj_uuid is None:
            msg = "Bad Request, no resource UUID provided to chown"
            raise cfgm_common.exceptions.HttpError(400, msg)
        if owner is None:
            msg = "Bad Request, no owner UUID provided to chown"
            raise cfgm_common.exceptions.HttpError(400, msg)
        if self.invalid_uuid(obj_uuid):
            msg = "Bad Request, invalid resource UUID"
            raise cfgm_common.exceptions.HttpError(400, msg)
        if self.invalid_uuid(owner):
            msg = "Bad Request, invalid owner UUID"
            raise cfgm_common.exceptions.HttpError(400, msg)

        try:
            obj_type = self._db_conn.uuid_to_obj_type(obj_uuid)
        except NoIdError as e:
            # Not present in DB
            raise cfgm_common.exceptions.HttpError(404, str(e))

        self._ensure_services_conn('chown', obj_type, obj_uuid=obj_uuid)

        # ensure user has RW permissions to object
        perms = self._permissions.obj_perms(get_request(), obj_uuid)
        if not 'RW' in perms:
            raise cfgm_common.exceptions.HttpError(403, " Permission denied")

        try:
            (ok, obj_dict) = self._db_conn.dbe_read(obj_type, obj_uuid,
                                                    obj_fields=['perms2'])
        except NoIdError as e:
            raise cfgm_common.exceptions.HttpError(404, str(e))
        obj_dict['perms2']['owner'] = owner
        self._db_conn.dbe_update(obj_type, obj_uuid, obj_dict)

        msg = "chown: %s owner set to %s" % (obj_uuid, owner)
        self.config_log(msg, level=SandeshLevel.SYS_NOTICE)

        return {}
    #end obj_chown_http_post

    def dump_cache(self):
        self._post_common(None, {})

        req_dict = get_request().json or {}
        obj_uuids = req_dict.get('uuids', [])
        count = req_dict.get('count', 10)

        return self._db_conn._object_db._obj_cache_mgr.dump_cache(
            obj_uuids=obj_uuids, count=count)

    # chmod for an object
    def obj_chmod_http_post(self):
        try:
            obj_uuid = get_request().json['uuid']
        except Exception as e:
            raise cfgm_common.exceptions.HttpError(400, str(e))
        if self.invalid_uuid(obj_uuid):
            raise cfgm_common.exceptions.HttpError(
                400, "Bad Request, invalid object id")

        try:
            obj_type = self._db_conn.uuid_to_obj_type(obj_uuid)
        except NoIdError as e:
            # Not present in DB
            raise cfgm_common.exceptions.HttpError(404, str(e))

        self._ensure_services_conn('chmod', obj_type, obj_uuid=obj_uuid)

        # ensure user has RW permissions to object
        perms = self._permissions.obj_perms(get_request(), obj_uuid)
        if not 'RW' in perms:
            raise cfgm_common.exceptions.HttpError(403, " Permission denied")

        request_params = get_request().json
        owner         = request_params.get('owner')
        share         = request_params.get('share')
        owner_access  = request_params.get('owner_access')
        global_access = request_params.get('global_access')

        (ok, obj_dict) = self._db_conn.dbe_read(obj_type, obj_uuid,
                             obj_fields=['perms2', 'is_shared'])
        obj_perms = obj_dict['perms2']
        old_perms = '%s/%d %d %s' % (obj_perms['owner'],
            obj_perms['owner_access'], obj_perms.get('global_access', 0),
            ['%s:%d' % (item['tenant'], item['tenant_access']) for item in
             obj_perms.get('share', [])])

        if owner:
            if self.invalid_uuid(owner):
                raise cfgm_common.exceptions.HttpError(
                    400, "Bad Request, invalid owner")
            obj_perms['owner'] = owner.replace('-','')
        if owner_access is not None:
            if self.invalid_access(owner_access):
                raise cfgm_common.exceptions.HttpError(
                    400, "Bad Request, invalid owner_access value")
            obj_perms['owner_access'] = owner_access
        if share is not None:
            try:
                for item in share:
                    """
                    item['tenant'] := [<share_type>:] <uuid>
                    share_type := ['domain' | 'tenant']
                    """
                    (share_type, share_id) = cfgm_common.utils.shareinfo_from_perms2_tenant(item['tenant'])
                    if self.invalid_share_type(share_type) or self.invalid_uuid(share_id) or self.invalid_access(item['tenant_access']):
                        raise cfgm_common.exceptions.HttpError(
                            400, "Bad Request, invalid share list")
            except Exception as e:
                raise cfgm_common.exceptions.HttpError(400, str(e))
            obj_perms['share'] = share
        if global_access is not None:
            if self.invalid_access(global_access):
                raise cfgm_common.exceptions.HttpError(
                    400, "Bad Request, invalid global_access value")
            obj_perms['global_access'] = global_access
            obj_dict['is_shared'] = (global_access != 0)

        new_perms = '%s/%d %d %s' % (obj_perms['owner'],
            obj_perms['owner_access'], obj_perms.get('global_access', 0),
            ['%s:%d' % (item['tenant'], item['tenant_access']) for item in
             obj_perms.get('share', [])])

        self._db_conn.dbe_update(obj_type, obj_uuid, obj_dict)
        msg = "chmod: %s perms old=%s, new=%s" % (obj_uuid, old_perms, new_perms)
        self.config_log(msg, level=SandeshLevel.SYS_NOTICE)

        return {}
    # end obj_chmod_http_post

    def prop_collection_http_get(self):
        if 'uuid' not in get_request().query:
            raise cfgm_common.exceptions.HttpError(
                400, 'Object uuid needed for property collection get')
        obj_uuid = get_request().query.uuid

        if 'fields' not in get_request().query:
            raise cfgm_common.exceptions.HttpError(
                400, 'Object fields needed for property collection get')
        obj_fields = get_request().query.fields.split(',')

        if 'position' in get_request().query:
            fields_position = get_request().query.position
        else:
            fields_position = None

        try:
            obj_type = self._db_conn.uuid_to_obj_type(obj_uuid)
        except NoIdError:
            raise cfgm_common.exceptions.HttpError(
                404, 'Object Not Found: ' + obj_uuid)
        resource_class = self.get_resource_class(obj_type)

        for obj_field in obj_fields:
            if ((obj_field not in resource_class.prop_list_fields) and
                (obj_field not in resource_class.prop_map_fields)):
                err_msg = '%s neither "ListProperty" nor "MapProperty"' %(
                    obj_field)
                raise cfgm_common.exceptions.HttpError(400, err_msg)
        # request validations over

        # common handling for all resource get
        (ok, result) = self._get_common(get_request(), obj_uuid)
        if not ok:
            (code, msg) = result
            self.config_object_error(
                obj_uuid, None, None, 'prop_collection_http_get', msg)
            raise cfgm_common.exceptions.HttpError(code, msg)

        try:
            ok, result = self._db_conn.prop_collection_get(
                obj_type, obj_uuid, obj_fields, fields_position)
            if not ok:
                self.config_object_error(
                    obj_uuid, None, None, 'prop_collection_http_get', result)
        except NoIdError as e:
            # Not present in DB
            raise cfgm_common.exceptions.HttpError(404, str(e))
        if not ok:
            raise cfgm_common.exceptions.HttpError(500, result)

        # check visibility
        if (not result['id_perms'].get('user_visible', True) and
            not self.is_admin_request()):
            result = 'This object is not visible by users: %s' % id
            self.config_object_error(
                id, None, None, 'prop_collection_http_get', result)
            raise cfgm_common.exceptions.HttpError(404, result)

        # Prepare response
        del result['id_perms']

        return result
    # end prop_collection_http_get

    def prop_collection_http_post(self):
        request_params = get_request().json
        # validate each requested operation
        obj_uuid = request_params.get('uuid')
        if not obj_uuid:
            err_msg = 'Error: prop_collection_update needs obj_uuid'
            raise cfgm_common.exceptions.HttpError(400, err_msg)

        try:
            obj_type = self._db_conn.uuid_to_obj_type(obj_uuid)
        except NoIdError:
            raise cfgm_common.exceptions.HttpError(
                404, 'Object Not Found: ' + obj_uuid)
        r_class = self.get_resource_class(obj_type)

        for req_param in request_params.get('updates') or []:
            obj_field = req_param.get('field')
            if obj_field in r_class.prop_list_fields:
                prop_coll_type = 'list'
            elif obj_field in r_class.prop_map_fields:
                prop_coll_type = 'map'
            else:
                err_msg = '%s neither "ListProperty" nor "MapProperty"' %(
                    obj_field)
                raise cfgm_common.exceptions.HttpError(400, err_msg)

            req_oper = req_param.get('operation').lower()
            field_val = req_param.get('value')
            field_pos = str(req_param.get('position'))
            prop_type = r_class.prop_field_types[obj_field]['xsd_type']
            prop_cls = cfgm_common.utils.str_to_class(prop_type, __name__)
            prop_val_type = prop_cls.attr_field_type_vals[prop_cls.attr_fields[0]]['attr_type']
            prop_val_cls = cfgm_common.utils.str_to_class(prop_val_type, __name__)
            try:
                self._validate_complex_type(prop_val_cls, field_val)
            except Exception as e:
                raise cfgm_common.exceptions.HttpError(400, str(e))
            if prop_coll_type == 'list':
                if req_oper not in ('add', 'modify', 'delete'):
                    err_msg = 'Unsupported operation %s in request %s' %(
                        req_oper, json.dumps(req_param))
                    raise cfgm_common.exceptions.HttpError(400, err_msg)
                if ((req_oper == 'add') and field_val is None):
                    err_msg = 'Add needs field value in request %s' %(
                        req_oper, json.dumps(req_param))
                    raise cfgm_common.exceptions.HttpError(400, err_msg)
                elif ((req_oper == 'modify') and
                    None in (field_val, field_pos)):
                    err_msg = 'Modify needs field value and position in request %s' %(
                        req_oper, json.dumps(req_param))
                    raise cfgm_common.exceptions.HttpError(400, err_msg)
                elif ((req_oper == 'delete') and field_pos is None):
                    err_msg = 'Delete needs field position in request %s' %(
                        req_oper, json.dumps(req_param))
                    raise cfgm_common.exceptions.HttpError(400, err_msg)
            elif prop_coll_type == 'map':
                if req_oper not in ('set', 'delete'):
                    err_msg = 'Unsupported operation %s in request %s' %(
                        req_oper, json.dumps(req_param))
                    raise cfgm_common.exceptions.HttpError(400, err_msg)
                if ((req_oper == 'set') and field_val is None):
                    err_msg = 'Set needs field value in request %s' %(
                        req_oper, json.dumps(req_param))
                elif ((req_oper == 'delete') and field_pos is None):
                    err_msg = 'Delete needs field position in request %s' %(
                        req_oper, json.dumps(req_param))
                    raise cfgm_common.exceptions.HttpError(400, err_msg)

        # Get actual resource from DB
        fields = r_class.prop_fields | r_class.ref_fields
        try:
            ok, result = self._db_conn.dbe_read(obj_type, obj_uuid,
                                                obj_fields=fields)
        except NoIdError as e:
            raise cfgm_common.exceptions.HttpError(404, str(e))
        except Exception:
            ok = False
            result = cfgm_common.utils.detailed_traceback()
        if not ok:
            self.config_object_error(
                obj_uuid, None, obj_type, 'prop_collection_update', result[1])
            raise cfgm_common.exceptions.HttpError(result[0], result[1])
        db_obj_dict = result

        # Look if the resource have a pending version, if yes use it as resource
        # to update
        if hasattr(r_class, 'get_pending_resource'):
            ok, result = r_class.get_pending_resource(db_obj_dict, fields)
            if ok and isinstance(result, dict):
                db_obj_dict = result
                obj_uuid = db_obj_dict['uuid']
            if not ok and result[0] != 404:
                self.config_object_error(obj_uuid, None, obj_type,
                                         'prop_collection_update', result[1])
                raise cfgm_common.exceptions.HttpError(result[0], result[1])

        self._put_common('prop-collection-update', obj_type, obj_uuid,
                         db_obj_dict,
                         req_prop_coll_updates=request_params.get('updates'))
    # end prop_collection_http_post

    def ref_update_http_post(self):
        # grab fields
        type = get_request().json.get('type')
        res_type, res_class = self._validate_resource_type(type)
        obj_uuid = get_request().json.get('uuid')
        ref_type = get_request().json.get('ref-type')
        ref_field = '%s_refs' %(ref_type.replace('-', '_'))
        ref_res_type, ref_class = self._validate_resource_type(ref_type)
        operation = get_request().json.get('operation')
        ref_uuid = get_request().json.get('ref-uuid')
        ref_fq_name = get_request().json.get('ref-fq-name')
        attr = get_request().json.get('attr')
        relax_ref_for_delete = get_request().json.get('relax_ref_for_delete', False)

        # validate fields
        if None in (res_type, obj_uuid, ref_res_type, operation):
            err_msg = 'Bad Request: type/uuid/ref-type/operation is null: '
            err_msg += '%s, %s, %s, %s.' \
                        %(res_type, obj_uuid, ref_res_type, operation)
            raise cfgm_common.exceptions.HttpError(400, err_msg)

        operation = operation.upper()
        if operation not in ['ADD', 'DELETE']:
            err_msg = 'Bad Request: operation should be add or delete: %s' \
                      %(operation)
            raise cfgm_common.exceptions.HttpError(400, err_msg)

        if not ref_uuid and not ref_fq_name:
            err_msg = 'Bad Request: ref-uuid or ref-fq-name must be specified'
            raise cfgm_common.exceptions.HttpError(400, err_msg)

        obj_type = res_class.object_type
        ref_obj_type = ref_class.object_type
        if not ref_uuid:
            try:
                ref_uuid = self._db_conn.fq_name_to_uuid(ref_obj_type, ref_fq_name)
            except NoIdError:
                raise cfgm_common.exceptions.HttpError(
                    404, 'Name ' + pformat(ref_fq_name) + ' not found')

        elif operation == 'ADD':
            # if UUID provided verify existence of the reference being added
            try:
                ref_fq_name = self._db_conn.uuid_to_fq_name(ref_uuid)
            except NoIdError as e:
                raise cfgm_common.exceptions.HttpError(404, str(e))

        # To invoke type specific hook and extension manager
        fields = res_class.prop_fields | res_class.ref_fields
        try:
            ok, result = self._db_conn.dbe_read(obj_type, obj_uuid, fields)
        except NoIdError as e:
            raise cfgm_common.exceptions.HttpError(404, str(e))
        except Exception:
            ok = False
            result = cfgm_common.utils.detailed_traceback()
        if not ok:
            self.config_object_error(obj_uuid, None, obj_type, 'ref_update',
                                     result[1])
            raise cfgm_common.exceptions.HttpError(result[0], result[1])
        db_obj_dict = result

        # Look if the resource have a pending version, if yes use it as resource
        # to update
        if hasattr(res_class, 'get_pending_resource'):
            ok, result = res_class.get_pending_resource(db_obj_dict, fields)
            if ok and isinstance(result, dict):
                db_obj_dict = result
                obj_uuid = db_obj_dict['uuid']
            if not ok and result[0] != 404:
                self.config_object_error(
                    obj_uuid, None, obj_type, 'ref_update', result[1])
                raise cfgm_common.exceptions.HttpError(result[0], result[1])

        obj_dict = {'uuid': obj_uuid}
        if ref_field in db_obj_dict:
            obj_dict[ref_field] = copy.deepcopy(db_obj_dict[ref_field])

        if operation == 'ADD':
            if ref_obj_type+'_refs' not in obj_dict:
                obj_dict[ref_obj_type + '_refs'] = []
            for ref in obj_dict.get(ref_obj_type + '_refs', []):
                if ref['uuid'] == ref_uuid:
                    ref['attr'] = attr
                    break
            else:
                obj_dict[ref_obj_type+'_refs'].append(
                    {'to':ref_fq_name, 'uuid': ref_uuid, 'attr':attr})
        elif operation == 'DELETE':
            for old_ref in obj_dict.get(ref_obj_type+'_refs', []):
                if old_ref['to'] == ref_fq_name or old_ref['uuid'] == ref_uuid:
                    obj_dict[ref_obj_type+'_refs'].remove(old_ref)
                    break

        ref_args = {'ref_obj_type':ref_obj_type, 'ref_uuid': ref_uuid,
                    'operation': operation, 'data': {'attr': attr},
                    'relax_ref_for_delete': relax_ref_for_delete}
        self._put_common('ref-update', obj_type, obj_uuid, db_obj_dict,
                         req_obj_dict=obj_dict, ref_args=ref_args)

        return {'uuid': obj_uuid}
    # end ref_update_http_post

    def ref_relax_for_delete_http_post(self):
        self._post_common(None, {})
        # grab fields
        obj_uuid = get_request().json.get('uuid')
        ref_uuid = get_request().json.get('ref-uuid')

        # validate fields
        if None in (obj_uuid, ref_uuid):
            err_msg = 'Bad Request: Both uuid and ref-uuid should be specified: '
            err_msg += '%s, %s.' %(obj_uuid, ref_uuid)
            raise cfgm_common.exceptions.HttpError(400, err_msg)

        try:
            obj_type = self._db_conn.uuid_to_obj_type(obj_uuid)
            self._db_conn.ref_relax_for_delete(obj_uuid, ref_uuid)
        except NoIdError:
            raise cfgm_common.exceptions.HttpError(
                404, 'uuid ' + obj_uuid + ' not found')

        apiConfig = VncApiCommon()
        apiConfig.object_type = obj_type
        fq_name = self._db_conn.uuid_to_fq_name(obj_uuid)
        apiConfig.identifier_name=':'.join(fq_name)
        apiConfig.identifier_uuid = obj_uuid
        apiConfig.operation = 'ref-relax-for-delete'
        try:
            body = json.dumps(get_request().json)
        except:
            body = str(get_request().json)
        apiConfig.body = body

        self._set_api_audit_info(apiConfig)
        log = VncApiConfigLog(api_log=apiConfig, sandesh=self._sandesh)
        log.send(sandesh=self._sandesh)

        return {'uuid': obj_uuid}
    # end ref_relax_for_delete_http_post

    def fq_name_to_id_http_post(self):
        self._post_common(None, {})
        type = get_request().json.get('type')
        res_type, r_class = self._validate_resource_type(type)
        obj_type = r_class.object_type
        fq_name = get_request().json['fq_name']

        try:
            id = self._db_conn.fq_name_to_uuid(obj_type, fq_name)
        except NoIdError:
            if obj_type == 'project':
                resource_type, r_class = self._validate_resource_type(obj_type)
                try:
                    self._extension_mgrs['resourceApi'].map_method(
                        'pre_%s_read_fqname' %(obj_type), fq_name)
                    id = self._db_conn.fq_name_to_uuid(obj_type, fq_name)
                except Exception as e:
                    self.config_log("fq_name_to_id_http_post error: " + str(e),
                                    level=SandeshLevel.SYS_DEBUG)
                    raise cfgm_common.exceptions.HttpError(
                        404, 'Name ' + pformat(fq_name) + ' not found')
            else:
                raise cfgm_common.exceptions.HttpError(
                    404, 'Name ' + pformat(fq_name) + ' not found')

        # ensure user has access to this id
        ok, result = self._permissions.check_perms_read(bottle.request, id)
        if not ok:
            err_code, err_msg = result
            raise cfgm_common.exceptions.HttpError(err_code, err_msg)

        return {'uuid': id}
    # end fq_name_to_id_http_post

    def id_to_fq_name_http_post(self):
        self._post_common(None, {})
        obj_uuid = get_request().json['uuid']

        # ensure user has access to this id
        ok, result = self._permissions.check_perms_read(get_request(), obj_uuid)
        if not ok:
            err_code, err_msg = result
            raise cfgm_common.exceptions.HttpError(err_code, err_msg)

        try:
            fq_name = self._db_conn.uuid_to_fq_name(obj_uuid)
        except NoIdError:
            raise cfgm_common.exceptions.HttpError(
               404, 'UUID ' + obj_uuid + ' not found')

        obj_type = self._db_conn.uuid_to_obj_type(obj_uuid)
        res_type = self.get_resource_class(obj_type).resource_type
        return {'fq_name': fq_name, 'type': res_type}
    # end id_to_fq_name_http_post

    # Enables a user-agent to store and retrieve key-val pair
    # TODO this should be done only for special/quantum plugin
    def useragent_kv_http_post(self):
        self._post_common(None, {})

        request_params = get_request().json
        oper = request_params.get('operation')
        if oper is None:
            err_msg = ("Error: Key/value store API needs 'operation' "
                       "parameter")
            raise cfgm_common.exceptions.HttpError(400, err_msg)
        if 'key' not in request_params:
            err_msg = ("Error: Key/value store API needs 'key' parameter")
            raise cfgm_common.exceptions.HttpError(400, err_msg)
        key = request_params.get('key')
        val = request_params.get('value', '')


        # TODO move values to common
        if oper == 'STORE':
            self._db_conn.useragent_kv_store(key, val)
        elif oper == 'RETRIEVE':
            try:
                result = self._db_conn.useragent_kv_retrieve(key)
                return {'value': result}
            except NoUserAgentKey:
                raise cfgm_common.exceptions.HttpError(
                    404, "Unknown User-Agent key " + key)
        elif oper == 'DELETE':
            result = self._db_conn.useragent_kv_delete(key)
        else:
            raise cfgm_common.exceptions.HttpError(
                404, "Invalid Operation " + oper)

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

    def get_resource_class(self, type_str):
        try:
            return self._resource_classes[type_str]
        except KeyError:
            raise TypeError('Invalid Contrail resource type: %s' % type_str)

    def list_bulk_collection_http_post(self):
        """ List collection when requested ids don't fit in query params."""

        type = get_request().json.get('type') # e.g. virtual-network
        resource_type, r_class = self._validate_resource_type(type)

        try:
            parent_uuids = get_request().json['parent_id'].split(',')
        except KeyError:
            parent_uuids = None

        try:
            back_ref_uuids = get_request().json['back_ref_id'].split(',')
        except KeyError:
            back_ref_uuids = None

        try:
            obj_uuids = get_request().json['obj_uuids'].split(',')
        except KeyError:
            obj_uuids = None

        is_count = get_request().json.get('count', False)
        is_detail = get_request().json.get('detail', False)
        include_shared = get_request().json.get('shared', False)

        try:
            filters = utils.get_filters(get_request().json.get('filters'))
        except Exception as e:
            raise cfgm_common.exceptions.HttpError(
                400, 'Invalid filter ' + get_request().json.get('filters'))

        req_fields = get_request().json.get('fields', [])
        if req_fields:
            req_fields = req_fields.split(',')

        exclude_hrefs = get_request().json.get('exclude_hrefs', False)

        pagination = {}
        if 'page_marker' in get_request().json:
            pagination['marker'] = self._validate_page_marker(
                                       get_request().json['page_marker'])

        if 'page_limit' in get_request().json:
            pagination['limit'] = self._validate_page_limit(
                                       get_request().json['page_limit'])

        req_tags = get_request().json.get('tags', [])
        if req_tags:
            req_tags = req_tags.split(',')

        return self._list_collection(r_class.object_type, parent_uuids,
                                     back_ref_uuids, obj_uuids, is_count,
                                     is_detail, filters, req_fields, req_tags,
                                     include_shared, exclude_hrefs,
                                     pagination)
    # end list_bulk_collection_http_post


    # Get hbs
    def hbs_get(self):
        self._post_common(None, {})
        # Get hbs fq_name from request
        req_json = get_request().json

        # valid json data required
        if not req_json:
            raise cfgm_common.exceptions.HttpError(
                400, 'HBS fqname or uuid (hbs_fqname/hbs_uuid) required for hbs daemonset get')

        hbs_fq_name = req_json.get('hbs_fq_name', '')
        hbs_uuid = req_json.get('hbs_uuid', '')

        # hbs fq_name or uuid required
        if not hbs_fq_name and not hbs_uuid:
            raise cfgm_common.exceptions.HttpError(
                400, 'HBS fqname or uuid (hbs_fqname/hbs_uuid) required for hbs daemonset get')

        # get hbs uuid, if not given for rbac checks
        if not hbs_uuid:
            try:
                hbs_uuid = self._db_conn.fq_name_to_uuid('host_based_service', hbs_fq_name)
            except NoIdError:
                raise cfgm_common.exceptions.HttpError(
                    404, 'Name ' + pformat(hbs_fq_name) + ' not found')

        # rbac check for hbs object operations
        ok, result = self._permissions.check_perms_read(get_request(), hbs_uuid)
        if not ok:
            raise cfgm_common.exceptions.HttpError(result[0], result[1])

        hbs_class = self.get_resource_class('host-based-service')
        # get hbs info
        hbs_info = hbs_class.get_hbs_info(hbs_fq_name, hbs_uuid)
        namespace_template = hbs_class.get_hbs_namespace(hbs_info['namespace'])
        ds_template = hbs_class.get_hbs_ds(hbs_info)
        left_vn_template = hbs_class.get_hbs_network(hbs_info['vnleft']['uuid'],
                                                 'left',
                                                 hbs_info['namespace'])
        right_vn_template = hbs_class.get_hbs_network(hbs_info['vnright']['uuid'],
                                                  'right',
                                                  hbs_info['namespace'])
        hbs = {'hbs': [left_vn_template, right_vn_template, ds_template]}
        return hbs

    # end hbs_get

    # Private Methods
    def _parse_args(self, args_str):
        self._args, _ = utils.parse_args(args_str)
    # end _parse_args

    # sigchld handler is currently not engaged. See comment @sigchld
    def sigchld_handler(self):
        # DB interface initialization
        self._db_connect(reset_config=False)
        self._db_init_entries()
    # end sigchld_handler

    def sigterm_handler(self, exit_arg=None):
        if exit_arg:
            self.config_log(exit_arg, level=SandeshLevel.SYS_ERR)
        exit(exit_arg)

    # sighup handler for applying new configs
    def sighup_handler(self):
        if self._args.conf_file:
            config = SafeConfigParser()
            config.read(self._args.conf_file)
            if 'DEFAULTS' in config.sections():
                try:
                    collectors = config.get('DEFAULTS', 'collectors')
                    if isinstance(collectors, string_types):
                        collectors = collectors.split()
                        new_chksum = hashlib.md5("".join(collectors)).hexdigest()
                        if new_chksum != self._chksum:
                            self._chksum = new_chksum
                            self._random_collectors = random.sample(collectors, len(collectors))
                        # Reconnect to achieve load-balance irrespective of list
                        self._sandesh.reconfig_collectors(self._random_collectors)
                except NoOptionError as e:
                    pass
    # end sighup_handler

    def _load_extensions(self):
        try:
            conf_sections = self._args.config_sections
            hostname = socket.getfqdn(self._args.listen_ip_addr)
            self._extension_mgrs['resourceApi'] = ExtensionManager(
                'vnc_cfg_api.resourceApi',
                propagate_map_exceptions=True,
                api_server_ip=hostname,
                api_server_port=self._args.listen_port,
                conf_sections=conf_sections, sandesh=self._sandesh)
            if (self._args.auth != 'no-auth' or
                self._args.auth != 'noauth'):
                self._extension_mgrs['resync'] = ExtensionManager(
                    'vnc_cfg_api.resync', api_server_ip=hostname,
                    api_server_port=self._args.listen_port,
                    conf_sections=conf_sections, sandesh=self._sandesh)
                self._extension_mgrs['resourceApi'].map_method(
                    'set_resync_extension_manager', self._extension_mgrs['resync'])
            self._extension_mgrs['neutronApi'] = ExtensionManager(
                'vnc_cfg_api.neutronApi',
                api_server_ip=hostname,
                api_server_port=self._args.listen_port,
                conf_sections=conf_sections, sandesh=self._sandesh,
                api_server_obj=self)
        except Exception as e:
            err_msg = cfgm_common.utils.detailed_traceback()
            self.config_log("Exception in extension load: %s" %(err_msg),
                level=SandeshLevel.SYS_ERR)
    # end _load_extensions

    def _db_connect(self, reset_config):
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
        cassandra_user = self._args.cassandra_user
        cassandra_password = self._args.cassandra_password
        cassandra_use_ssl = self._args.cassandra_use_ssl
        cassandra_ca_certs = self._args.cassandra_ca_certs
        obj_cache_entries = int(self._args.object_cache_entries)
        obj_cache_exclude_types = \
            [t.replace('-', '_').strip() for t in
             self._args.object_cache_exclude_types.split(',')]
        debug_obj_cache_types = \
            [t.replace('-', '_').strip() for t in
             self._args.debug_object_cache_types.split(',')]

        db_engine = self._args.db_engine
        self._db_engine = db_engine
        cred = None

        db_server_list = None
        if db_engine == 'cassandra':
            if cassandra_user is not None and cassandra_password is not None:
                cred = {'username':cassandra_user,'password':cassandra_password}
            db_server_list = cass_server_list

        self._db_conn = VncDbClient(
            self, db_server_list, rabbit_servers, rabbit_port, rabbit_user,
            rabbit_password, rabbit_vhost, rabbit_ha_mode, self._args.listen_ip_addr,
            reset_config, zk_server, self._args.cluster_id, db_credential=cred,
            db_engine=db_engine, rabbit_use_ssl=self._args.rabbit_use_ssl,
            kombu_ssl_version=self._args.kombu_ssl_version,
            kombu_ssl_keyfile= self._args.kombu_ssl_keyfile,
            kombu_ssl_certfile=self._args.kombu_ssl_certfile,
            kombu_ssl_ca_certs=self._args.kombu_ssl_ca_certs,
            obj_cache_entries=obj_cache_entries,
            obj_cache_exclude_types=obj_cache_exclude_types,
            debug_obj_cache_types=debug_obj_cache_types,
            cassandra_use_ssl=self._args.cassandra_use_ssl,
            cassandra_ca_certs=self._args.cassandra_ca_certs)

        #TODO refacter db connection management.
        self._addr_mgmt._get_db_conn()
    # end _db_connect

    def _ensure_id_perms_present(self, obj_uuid, obj_dict):
        """
        Called at resource creation to ensure that id_perms is present in obj
        """
        # retrieve object and permissions
        id_perms = self._get_default_id_perms()

        if (('id_perms' not in obj_dict) or
                (obj_dict['id_perms'] is None)):
            # Resource creation
            if obj_uuid is None:
                obj_dict['id_perms'] = id_perms
                return

            return

        # retrieve the previous version of the id_perms
        # from the database and update the id_perms with
        # them.
        if obj_uuid is not None:
            try:
                old_id_perms = self._db_conn.uuid_to_obj_perms(obj_uuid)
                for field, value in list(old_id_perms.items()):
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

    def _get_default_id_perms(self, **kwargs):
        id_perms = copy.deepcopy(Provision.defaults.perms)
        id_perms_json = json.dumps(id_perms, default=lambda o: dict((k, v)
                                   for k, v in o.__dict__.items()))
        id_perms_dict = json.loads(id_perms_json)
        id_perms_dict.update(kwargs)
        return id_perms_dict
    # end _get_default_id_perms

    def _ensure_perms2_present(self, obj_type, obj_uuid, obj_dict,
                               project_id=None):
        """
        Called at resource creation to ensure that id_perms is present in obj
        """
        # retrieve object and permissions
        perms2 = self._get_default_perms2()

        # set ownership of object to creator tenant
        if obj_type == 'project' and 'uuid' in obj_dict:
            perms2['owner'] = str(obj_dict['uuid']).replace('-', '')

        elif obj_dict.get('perms2') and obj_dict['perms2'].get('owner'):
            perms2['owner'] = obj_dict['perms2']['owner']

        elif 'fq_name' in obj_dict and obj_dict['fq_name'][:-1]:
            if 'parent_type' in obj_dict:
                parent_type = obj_dict['parent_type'].replace('-', '_')
            else:
                r_class = self.get_resource_class(obj_type)
                if (len(r_class.parent_types) != 1):
                    msg = ("Ambiguous parent to ensure permissiosn of %s, "
                           "please choose one parent type: %s" %
                           (obj_type, pformat(r_class.parent_types)))
                    raise cfgm_common.exceptions.HttpError(400, msg)
                parent_type = r_class.parent_types[0].replace('-', '_')

            if parent_type == 'domain':
                if project_id:
                    perms2['owner'] = project_id
                else:
                    perms2['owner'] = 'cloud-admin'
            else:
                parent_fq_name = obj_dict['fq_name'][:-1]
                parent_uuid = obj_dict.get('parent_uuid')
                try:
                    if parent_uuid is None:
                        try:
                            parent_uuid = self._db_conn.fq_name_to_uuid(
                                parent_type, parent_fq_name)
                        except NoIdError:
                            raise cfgm_common.exceptions.HttpError(
                                404, 'Name' + pformat(parent_fq_name) + ' not found')
                    ok, parent_obj_dict = self._db_conn.dbe_read(
                        parent_type, parent_uuid, obj_fields=['perms2'])
                except NoIdError as e:
                    msg = "Parent %s cannot be found: %s" % (parent_type, str(e))
                    raise cfgm_common.exceptions.HttpError(404, msg)
                perms2['owner'] = parent_obj_dict['perms2']['owner']

        elif project_id:
            perms2['owner'] = project_id
        else:
            perms2['owner'] = 'cloud-admin'

        if obj_dict.get('perms2') is None:
            # Resource creation
            if obj_uuid is None:
                obj_dict['perms2'] = perms2
                return
            # Resource already exists
            try:
                obj_dict['perms2'] = self._db_conn.uuid_to_obj_perms2(obj_uuid)
            except NoIdError:
                obj_dict['perms2'] = perms2
            return

        # retrieve the previous version of the perms2
        # from the database and update the perms2 with
        # them.
        if obj_uuid is not None:
            try:
                old_perms2 = self._db_conn.uuid_to_obj_perms2(obj_uuid)
                for field, value in list(old_perms2.items()):
                    if value is not None:
                        perms2[field] = value
            except NoIdError:
                pass

        # Start from default and update from obj_dict
        req_perms2 = obj_dict['perms2']
        for key in req_perms2:
            perms2[key] = req_perms2[key]
        # TODO handle perms2 present in req_perms2

        obj_dict['perms2'] = perms2

        # ensure is_shared and global_access are consistent
        shared = obj_dict.get('is_shared', None)
        gaccess = obj_dict['perms2'].get('global_access', None)
        if (gaccess is not None and shared is not None and
                shared != (gaccess != 0)):
            msg = ("Inconsistent is_shared (%s a) and global_access (%s)" %
                   (shared, gaccess))
            # NOTE(ethuleau): ignore exception for the moment as it breaks the
            # Neutron use case where external network have global access but
            # is property 'is_shared' is False https://review.opencontrail.org/#/q/Id6a0c1a509d7663da8e5bc86f2c7c91c73d420a2
            # Before patch https://review.opencontrail.org/#q,I9f53c0f21983bf191b4c51318745eb348d48dd86,n,z
            # error was also ignored as all retruned errors of that method were
            # not took in account
            # raise cfgm_common.exceptions.HttpError(400, msg)

    def _get_default_perms2(self):
        perms2 = copy.deepcopy(Provision.defaults.perms2)
        perms2_json = json.dumps(perms2, default=lambda o: dict((k, v)
                                   for k, v in o.__dict__.items()))
        perms2_dict = json.loads(perms2_json)
        return perms2_dict
    # end _get_default_perms2

    def _db_init_entries(self):
        # create singleton defaults if they don't exist already in db
        gsc = self.create_singleton_entry(GlobalSystemConfig(
            autonomous_system=64512, config_version=CONFIG_VERSION))
        self._gsc_uuid = gsc.uuid
        gvc = self.create_singleton_entry(GlobalVrouterConfig(
            parent_obj=gsc))
        domain = self.create_singleton_entry(Domain())
        self._default_domain = domain.serialize_to_json()
        project = self.create_singleton_entry(Project(parent_obj=domain))
        self._default_project = project.serialize_to_json()
        self.create_singleton_entry(Fabric())

        # Global and default policy resources
        pm = self.create_singleton_entry(PolicyManagement())
        self._global_pm_uuid = pm.uuid
        aps = self.create_singleton_entry(ApplicationPolicySet(
            parent_obj=pm, all_applications=True))
        ok, result = self._db_conn.ref_update(
            ApplicationPolicySet.object_type,
            aps.uuid,
            GlobalVrouterConfig.object_type,
            gvc.uuid,
            {'attr': None},
            'ADD',
            None,
        )
        if not ok:
            msg = ("Error while referencing global vrouter config %s with the "
                   "default global application policy set %s: %s" %
                   (gvc.uuid, aps.uuid, result[1]))
            self.config_log(msg, level=SandeshLevel.SYS_ERR)

        ip_fab_vn = self.create_singleton_entry(
            VirtualNetwork(cfgm_common.IP_FABRIC_VN_FQ_NAME[-1],
                           is_provider_network=True))
        self.create_singleton_entry(
            RoutingInstance(cfgm_common.IP_FABRIC_VN_FQ_NAME[-1], ip_fab_vn,
                            routing_instance_is_default=True))
        self.create_singleton_entry(
            RoutingInstance('__default__', ip_fab_vn))
        link_local_vn = self.create_singleton_entry(
            VirtualNetwork(cfgm_common.LINK_LOCAL_VN_FQ_NAME[-1]))
        self.create_singleton_entry(
            RoutingInstance('__link_local__', link_local_vn,
                            routing_instance_is_default=True))

        # dc network
        dci_vn = self.create_singleton_entry(
            VirtualNetwork(cfgm_common.DCI_VN_FQ_NAME[-1]))
        self.create_singleton_entry(
            RoutingInstance(cfgm_common.DCI_VN_FQ_NAME[-1], dci_vn,
                            routing_instance_is_default=True))
        self.create_singleton_entry(
            RoutingInstance('__default__', dci_vn))

        # specifying alarm kwargs like contrail_alarm.py
        alarm_kwargs = {"alarm_rules":
                        {"or_list" : [
                         {"and_list": [
                           { "operand1": "UveConfigReq.err_info.*.",
                            "operation": "==",
                            "operand2": {"json_value": "True"}
                           }          ]
                         }           ]
                        },
                        "alarm_severity": 1,
                        "fq_name": [
                            "default-global-system-config",
                            "system-defined-bottle-request-size-limit"
                        ],
                        "id_perms": {
                            "description": "Bottle request size limit exceeded."
                        },
                        "parent_type": "global-system-config",
                        "uve_keys": {
                            "uve_key": [
                                "config-node"
                            ]
                        }
                       }
        self.create_singleton_entry(Alarm(**alarm_kwargs))
        try:
            self.create_singleton_entry(
                RoutingInstance('default-virtual-network',
                                routing_instance_is_default=True))
        except Exception as e:
            self.config_log('error while creating primary routing instance for'
                            'default-virtual-network: ' + str(e),
                            level=SandeshLevel.SYS_NOTICE)

        # Create singleton SG __no_rule__ object for openstack
        sg_rules = PolicyEntriesType()
        id_perms = IdPermsType(enable=True,
                               description="Security group with no rules",
                               user_visible=True)
        perms2 = PermType2(owner='cloud-admin')
        perms2.set_global_access(PERMS_RX)
        # Creating SG without SG-ID which will then
        # be populated and attached to this SG during
        # singleton create
        sg_obj = SecurityGroup(
            name=SG_NO_RULE_FQ_NAME[-1],
            parent_obj=project,
            security_group_entries=sg_rules.exportDict(''),
            id_perms=id_perms.exportDict(''),
            perms2=perms2.exportDict(''))
        self.create_singleton_entry(sg_obj)

        self.create_singleton_entry(DiscoveryServiceAssignment())
        self.create_singleton_entry(GlobalQosConfig())

        sc_ipam_subnet_v4 = IpamSubnetType(subnet=SubnetType('0.0.0.0', 8))
        sc_ipam_subnet_v6 = IpamSubnetType(subnet=SubnetType('::ffff', 104))
        sc_ipam_subnets = IpamSubnets([sc_ipam_subnet_v4, sc_ipam_subnet_v6])
        sc_ipam_obj = NetworkIpam('service-chain-flat-ipam',
                ipam_subnet_method="flat-subnet", ipam_subnets=sc_ipam_subnets)
        self.create_singleton_entry(sc_ipam_obj)

        # Create pre-defined tag-type
        for type_str, type_id in list(TagTypeNameToId.items()):
            type_id_hex = "0x{:04x}".format(type_id)
            tag = TagType(name=type_str, tag_type_id=type_id_hex)
            tag.display_name = type_str
            self.create_singleton_entry(tag, user_visible=False)

        if int(self._args.worker_id) == 0:
            self._db_conn.db_resync()

        # make default ipam available across tenants for backward compatability
        obj_type = 'network_ipam'
        fq_name = ['default-domain', 'default-project', 'default-network-ipam']
        obj_uuid = self._db_conn.fq_name_to_uuid(obj_type, fq_name)
        (ok, obj_dict) = self._db_conn.dbe_read(obj_type, obj_uuid,
                                                obj_fields=['perms2'])
        obj_dict['perms2']['global_access'] = PERMS_RX
        self._db_conn.dbe_update(obj_type, obj_uuid, obj_dict)

    # end _db_init_entries

    # generate default rbac group rule
    def _create_default_rbac_rule(self):
        # allow full access to cloud admin
        rbac_rules = [
            {
                'rule_object':'fqname-to-id',
                'rule_field': '',
                'rule_perms': [{'role_name':'*', 'role_crud':'CRUD'}]
            },
            {
                'rule_object':'id-to-fqname',
                'rule_field': '',
                'rule_perms': [{'role_name':'*', 'role_crud':'CRUD'}]
            },
            {
                'rule_object':'useragent-kv',
                'rule_field': '',
                'rule_perms': [{'role_name':'*', 'role_crud':'CRUD'}]
            },
            {
                'rule_object':'documentation',
                'rule_field': '',
                'rule_perms': [{'role_name':'*', 'role_crud':'R'}]
            },
            {
                'rule_object':'/',
                'rule_field': '',
                'rule_perms': [{'role_name':'*', 'role_crud':'R'}]
            },
        ]

        obj_type = 'api_access_list'
        fq_name = ['default-global-system-config', 'default-api-access-list']
        try:
            # ensure global list is not missing any default rules (bug 1642464)
            id = self._db_conn.fq_name_to_uuid(obj_type, fq_name)
            (ok, obj_dict) = self._db_conn.dbe_read(obj_type, id)
            update_obj = False
            cur_rbac_rules = copy.deepcopy(obj_dict['api_access_list_entries']['rbac_rule'])
            for rule in rbac_rules:
                present = False
                for existing_rule in cur_rbac_rules:
                    if rule == existing_rule:
                        present = True
                        cur_rbac_rules.remove(existing_rule)
                        break
                if not present:
                    obj_dict['api_access_list_entries']['rbac_rule'].append(rule)
                    update_obj = True
            if update_obj:
                self._db_conn.dbe_update(obj_type, id, obj_dict)
            return
        except NoIdError:
            pass

        rge = RbacRuleEntriesType([])
        for rule in rbac_rules:
            rule_perms = [RbacPermType(role_name=p['role_name'], role_crud=p['role_crud']) for p in rule['rule_perms']]
            rbac_rule = RbacRuleType(rule_object=rule['rule_object'],
                rule_field=rule['rule_field'], rule_perms=rule_perms)
            rge.add_rbac_rule(rbac_rule)

        rge_dict = rge.exportDict('')
        glb_rbac_cfg = ApiAccessList(parent_type='global-system-config',
            fq_name=fq_name, api_access_list_entries = rge_dict)

        try:
            self.create_singleton_entry(glb_rbac_cfg)
        except Exception as e:
            err_msg = 'Error creating default api access list object'
            err_msg += cfgm_common.utils.detailed_traceback()
            self.config_log(err_msg, level=SandeshLevel.SYS_ERR)
    # end _create_default_rbac_rule

    def _resync_domains_projects(self, ext):
        if hasattr(ext.obj, 'resync_domains_projects'):
            ext.obj.resync_domains_projects()
    # end _resync_domains_projects

    def create_singleton_entry(self, singleton_obj, user_visible=True):

        s_obj = singleton_obj
        obj_type = s_obj.object_type
        fq_name = s_obj.get_fq_name()

        # TODO remove backward compat create mapping in zk
        # for singleton START
        try:
            cass_uuid = self._db_conn._object_db.fq_name_to_uuid(obj_type, fq_name)
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
            s_obj.uuid = self._db_conn.fq_name_to_uuid(obj_type, fq_name)
        except NoIdError:
            obj_json = json.dumps(s_obj, default=_obj_serializer_all)
            obj_dict = json.loads(obj_json)
            if s_obj.get_id_perms():
                obj_dict['id_perms'] = s_obj.get_id_perms()
            else:
                obj_dict['id_perms'] = self._get_default_id_perms(
                                                user_visible=user_visible)
            if s_obj.get_perms2():
                obj_dict['perms2'] = s_obj.get_perms2()
            else:
                obj_dict['perms2'] = self._get_default_perms2()

            (ok, result) = self._db_conn.dbe_alloc(obj_type, obj_dict)
            if not ok:
                (code, msg) = result
                raise cfgm_common.exceptions.HttpError(code, msg)
            obj_id = result
            s_obj.uuid = obj_id
            # For virtual networks, allocate an ID
            if obj_type == 'virtual_network':
                vn_id = self.alloc_vn_id(s_obj.get_fq_name_str())
                obj_dict['virtual_network_network_id'] = vn_id
            if obj_type == 'tag':
                obj_dict = self._allocate_tag_id(obj_dict)
            if obj_type == 'security_group':
                sg_id = self.alloc_security_group_id(s_obj.get_fq_name_str())
                obj_dict['security_group_id'] = sg_id
            self._db_conn.dbe_create(obj_type, obj_id, obj_dict)
            self.create_default_children(obj_type, s_obj)
        return s_obj
    # end create_singleton_entry

    # allocate tag id for tag object
    def _allocate_tag_id(self, obj_dict):
        type_str = obj_dict['tag_type_name']
        value_str = obj_dict['tag_value']

        ok, result = self.get_resource_class('tag_type').locate(
            [type_str], id_perms=IdPermsType(user_visible=False))
        tag_type = result
        obj_dict['tag_type_refs'] = [
            {
                'uuid': tag_type['uuid'],
                'to': tag_type['fq_name'],
            },
        ]

        # Allocate ID for tag value. Use the all fq_name to distinguish same
        # tag values between global and scoped
        value_id = self.get_resource_class(
            'tag').vnc_zk_client.alloc_tag_value_id(
                type_str, ':'.join(obj_dict['fq_name']))

        # Compose Tag ID with the type ID and value ID
        obj_dict['tag_id'] = "{}{:04x}".format(tag_type['tag_type_id'],
                                               value_id)
        return obj_dict
    # end allocate tag id

    def _validate_page_marker(self, req_page_marker):
        # query params always appears as string
        if req_page_marker and req_page_marker.lower() != 'none':
            try:
                req_page_marker_uuid = req_page_marker.split(':')[-1]
                _ = str(uuid.UUID(req_page_marker_uuid))
            except Exception as e:
                raise cfgm_common.exceptions.HttpError(
                    400, 'Invalid page_marker %s: %s' %(
                         req_page_marker, e))
        else:
            req_page_marker = None

        return req_page_marker
    # end _validate_page_marker

    def _validate_page_limit(self, req_page_limit):
        try:
            val = int(req_page_limit)
            if val <= 0:
                raise Exception("page_limit has to be greater than zero")
        except Exception as e:
            raise cfgm_common.exceptions.HttpError(
                400, 'Invalid page_limit %s: %s' %(
                     req_page_limit, e))
        return int(req_page_limit)
    # end _validate_page_limit

    def _list_collection(self, obj_type, parent_uuids=None,
                         back_ref_uuids=None, obj_uuids=None,
                         is_count=False, is_detail=False, filters=None,
                         req_fields=None, req_tags=None, include_shared=False,
                         exclude_hrefs=False, pagination=None):
        resource_type, r_class = self._validate_resource_type(obj_type)

        is_admin = self.is_admin_request()

        if is_admin:
            field_names = req_fields
        else:
            field_names = [u'id_perms'] + (req_fields or [])

        if is_count and is_admin:
            ret_result = 0
        else:
            ret_result = []

        page_filled = False
        if 'marker' in pagination:
            # if marker is None, start scanning from uuid 0
            page_start = pagination['marker'] or '0'
            if 'limit' in pagination:
                page_count = pagination['limit']
            else:
                page_count = self._args.paginate_count
        else:
            page_start = None # cookie to start next search
            page_count = None # remainder count to finish page

        (ok, result) = r_class.pre_dbe_list(obj_uuids, self._db_conn)
        if not ok:
            (code, msg) = result
            raise cfgm_common.exceptions.HttpError(code, msg)

        while not page_filled:
            (ok, result, ret_marker) = self._db_conn.dbe_list(obj_type,
                                 parent_uuids, back_ref_uuids, obj_uuids, is_count and is_admin,
                                 filters, is_detail=is_detail, field_names=field_names,
                                 tag_values=req_tags,
                                 include_shared=include_shared,
                                 paginate_start=page_start,
                                 paginate_count=page_count)
            if not ok:
                self.config_object_error(None, None, '%ss' %(obj_type),
                                         'dbe_list', result)
                raise cfgm_common.exceptions.HttpError(404, result)

            # If only counting, return early
            if is_count and is_admin:
                ret_result += result
                return {'%ss' %(resource_type): {'count': ret_result}}

            allowed_fields = set(['uuid', 'href', 'fq_name'])
            allowed_fields |= set(req_fields or [])
            obj_dicts = []
            if is_admin:
                for obj_result in result:
                    if not exclude_hrefs:
                        obj_result['href'] = self.generate_url(
                            resource_type, obj_result['uuid'])
                    if is_detail:
                        obj_result['name'] = obj_result['fq_name'][-1]
                        obj_dicts.append({resource_type: obj_result})
                    else:
                        obj_dicts.append(obj_result)
            else:
                # fetch all perms of child/ref/back_ref of listed resources in
                # one DB call for performance reason
                if is_detail:
                    ref_uuids = {ref['uuid'] for link in r_class.obj_links
                                 for o in result for ref in o.get(link, [])}
                    if self.is_rbac_enabled():
                        fields = ['perms2']
                    else:
                        fields = ['id_perms']
                    ref_dicts = self._db_conn._object_db.object_raw_read(
                        resource_type, list(ref_uuids), fields)
                    ref_perms = {obj_dict['uuid']: obj_dict
                                for obj_dict in ref_dicts}

                for obj_result in result:
                    id_perms = obj_result.get('id_perms')

                    if not id_perms:
                        # It is possible that the object was deleted, but received
                        # an update after that. We need to ignore it for now. In
                        # future, we should clean up such stale objects
                        continue

                    if not id_perms.get('user_visible', True):
                        # skip items not authorized
                        continue
                    ok, status = self._permissions.check_perms_read(
                        get_request(), obj_result['uuid'], obj_result)
                    if not ok and status[0] == 403:
                        continue

                    obj_result['name'] = obj_result['fq_name'][-1]
                    if is_detail:
                        self.obj_view(resource_type, obj_result, ref_perms)
                        obj_dicts.append({resource_type: obj_result})
                    else:
                        for field in set(obj_result.keys()) - allowed_fields:
                            del obj_result[field]
                        obj_dicts.append(obj_result)

                    if not exclude_hrefs:
                        obj_result['href'] = self.generate_url(
                            resource_type, obj_result['uuid'])
                # end obj_result in result
            # end not admin req

            ret_result.extend(obj_dicts)
            if 'marker' not in pagination:
                page_filled = True
            elif ret_marker is None: # pagination request and done
                page_filled = True
            else: # pagination request and partially filled
                page_start = ret_marker
                page_count -= len(result)
                if page_count <= 0:
                    page_filled = True
        # end while not page_filled

        (ok, err_msg) = r_class.post_dbe_list(ret_result, self._db_conn)
        if not ok:
            (code, msg) = err_msg
            raise cfgm_common.exceptions.HttpError(code, msg)

        if 'marker' in pagination: # send next marker along with results
            if is_count:
                return {'%ss' %(resource_type): {'count': len(ret_result)},
                        'marker': ret_marker}
            else:
                return {'%ss' %(resource_type): ret_result,
                        'marker': ret_marker}

        if is_count:
            return {'%ss' %(resource_type): {'count': len(ret_result)}}
        else:
            return {'%ss' %(resource_type): ret_result}
    # end _list_collection

    def get_db_connection(self):
        return self._db_conn
    # end get_db_connection

    def generate_url(self, resource_type, obj_uuid):
        try:
            url_parts = get_request().urlparts
            netloc = url_parts.netloc.replace('<script>', '<!--script>')
            netloc = netloc.replace('</script>', '</script-->')
            return '%s://%s/%s/%s'\
                % (url_parts.scheme, netloc, resource_type, obj_uuid)
        except Exception as e:
            return '%s/%s/%s' % (self._base_url, resource_type, obj_uuid)
    # end generate_url

    def generate_hrefs(self, resource_type, obj_dict):
        # return a copy of obj_dict with href keys for:
        # self, parent, children, refs, backrefs
        r_class = self.get_resource_class(resource_type)

        obj_dict['href'] = self.generate_url(resource_type, obj_dict['uuid'])

        try:
            obj_dict['parent_href'] = self.generate_url(
                obj_dict['parent_type'], obj_dict['parent_uuid'])
        except KeyError:
            # No parent
            pass

        for field, field_info in itertools.chain(
                    list(r_class.children_field_types.items()),
                    list(r_class.ref_field_types.items()),
                    list(r_class.backref_field_types.items()),
                ):
            try:
                type = field_info[0]
                for link in obj_dict[field]:
                    link['href'] = self.generate_url(type, link['uuid'])
            except KeyError:
                pass

    def config_object_error(self, id, fq_name_str, obj_type,
                            operation, err_str):
        apiConfig = VncApiCommon()
        if obj_type is not None:
            apiConfig.object_type = obj_type
        apiConfig.identifier_name = fq_name_str
        apiConfig.identifier_uuid = id
        apiConfig.operation = operation
        if err_str:
            apiConfig.error = "%s:%s" % (obj_type, err_str)
        self._set_api_audit_info(apiConfig)

        log = VncApiConfigLog(api_log=apiConfig, sandesh=self._sandesh)
        log.send(sandesh=self._sandesh)
    # end config_object_error

    def config_log(self, msg_str, level=SandeshLevel.SYS_INFO):
        errcls = {
            SandeshLevel.SYS_DEBUG: VncApiDebug,
            SandeshLevel.SYS_INFO: VncApiInfo,
            SandeshLevel.SYS_NOTICE: VncApiNotice,
            SandeshLevel.SYS_ERR: VncApiError,
        }
        errcls.get(level, VncApiError)(
            api_msg=msg_str, level=level, sandesh=self._sandesh).send(
                sandesh=self._sandesh)
    # end config_log

    def _set_api_audit_info(self, apiConfig):
        apiConfig.url = get_request().url
        apiConfig.remote_ip = get_request().headers.get('X-Requestor-IP')
        if not apiConfig.remote_ip:
            # If the X-Requestor-IP was not sent, it's likely that the request
            # did not come from node.js. In this case, try to get the remote IP as:
            # 1. If present, the first IP address of HTTP_X_FORWARDED_FOR.
            # 2. Else, If present, from the REMOTE_ADDR.
            # 3. HTTP_X_Host

            if 'HTTP_X_FORWARDED_FOR' in get_request().environ:
                addr = get_request().environ.get('HTTP_X_FORWARDED_FOR').split(',')
                apiConfig.remote_ip = addr[0]
            elif 'REMOTE_ADDR' in get_request().environ:
                apiConfig.remote_ip = get_request().environ.get('REMOTE_ADDR')
            else:
                apiConfig.remote_ip = get_request().headers.get('Host')

        useragent = get_request().headers.get('X-Contrail-Useragent')
        if not useragent:
            useragent = get_request().headers.get('User-Agent')
        apiConfig.useragent = useragent
        apiConfig.user = get_request().headers.get('X-User-Name')
        apiConfig.project = get_request().headers.get('X-Project-Name')
        apiConfig.domain = get_request().headers.get('X-Domain-Name', 'None')
        if apiConfig.domain.lower() == 'none':
            apiConfig.domain = 'default-domain'
        if int(get_request().headers.get('Content-Length', 0)) > 0:
            try:
                body = json.dumps(get_request().json)
            except:
                body = str(get_request().json)
            apiConfig.body = body
    # end _set_api_audit_info

    # uuid is parent's for collections
    def _get_common(self, request, uuid=None):
        # TODO check api + resource perms etc.
        if self.is_auth_needed() and uuid:
            if isinstance(uuid, list):
                for u_id in uuid:
                    ok, result = self._permissions.check_perms_read(request,
                                                                    u_id)
                    if not ok:
                        return ok, result
            else:
                return self._permissions.check_perms_read(request, uuid)

        return (True, '')
    # end _get_common

    def _put_common(
            self, api_name, obj_type, obj_uuid, db_obj_dict, req_obj_dict=None,
            req_prop_coll_updates=None, ref_args=None):

        obj_fq_name = db_obj_dict.get('fq_name', 'missing-fq-name')
        # ZK and rabbitmq should be functional
        self._ensure_services_conn(
            api_name, obj_type, obj_uuid, obj_fq_name)

        resource_type, r_class = self._validate_resource_type(obj_type)
        try:
            self._extension_mgrs['resourceApi'].map_method(
                'pre_%s_update' %(obj_type), obj_uuid, req_obj_dict)
        except RuntimeError:
            # lack of registered extension leads to RuntimeError
            pass
        except Exception as e:
            err_msg = 'In pre_%s_update an extension had error for %s' \
                      %(obj_type, req_obj_dict)
            err_msg += cfgm_common.utils.detailed_traceback()
            self.config_log(err_msg, level=SandeshLevel.SYS_NOTICE)

        db_conn = self._db_conn

        # check visibility
        if (not db_obj_dict['id_perms'].get('user_visible', True) and
            not self.is_admin_request()):
            result = 'This object is not visible by users: %s' % obj_uuid
            self.config_object_error(obj_uuid, None, obj_type, api_name, result)
            raise cfgm_common.exceptions.HttpError(404, result)

        # properties validator (for collections validation in caller)
        if req_obj_dict is not None:
            ok, result = self._validate_props_in_request(r_class,
                         req_obj_dict, operation='UPDATE')
            if not ok:
                result = 'Bad property in %s: %s' %(api_name, result)
                raise cfgm_common.exceptions.HttpError(400, result)

        # references validator
        if req_obj_dict is not None:
            ok, result = self._validate_refs_in_request(r_class, req_obj_dict)
            if not ok:
                result = 'Bad reference in %s: %s' %(api_name, result)
                raise cfgm_common.exceptions.HttpError(400, result)

        # common handling for all resource put
        request = get_request()
        fq_name_str = ":".join(obj_fq_name or [])
        if req_obj_dict:
            if ('id_perms' in req_obj_dict and
                    req_obj_dict['id_perms'].get('uuid')):
                if not self._db_conn.match_uuid(req_obj_dict, obj_uuid):
                    msg = (
                        "UUID mismatch from %s:%s" %
                        (request.environ.get('REMOTE_ADDR',
                                             "Remote address not found"),
                         request.environ.get('HTTP_USER_AGENT',
                                             "User agent not found"))
                    )
                    self.config_object_error(
                        obj_uuid, fq_name_str, obj_type, 'put', msg)
                    self._db_conn.set_uuid(obj_type, req_obj_dict,
                                           uuid.UUID(obj_uuid),
                                           do_lock=False)

            # Ensure object has at least default permissions set
            self._ensure_id_perms_present(obj_uuid, req_obj_dict)

        apiConfig = VncApiCommon()
        apiConfig.object_type = obj_type
        apiConfig.identifier_name = fq_name_str
        apiConfig.identifier_uuid = obj_uuid
        apiConfig.operation = api_name
        self._set_api_audit_info(apiConfig)
        log = VncApiConfigLog(api_log=apiConfig,
                sandesh=self._sandesh)
        log.send(sandesh=self._sandesh)

        if self.is_auth_needed():
            ok, result = self._permissions.check_perms_write(request, obj_uuid)
            if not ok:
                (code, msg) = result
                self.config_object_error(
                    obj_uuid, fq_name_str, obj_type, api_name, msg)
                raise cfgm_common.exceptions.HttpError(code, msg)

        # no ref to a pending deleted resource
        ok, result = r_class.no_pending_deleted_resource_in_refs(req_obj_dict)
        if not ok:
            code, msg = result
            raise cfgm_common.exceptions.HttpError(code, msg)

        # Validate perms on references
        if req_obj_dict is not None:
            try:
                self._validate_perms_in_request(
                    r_class, obj_type, req_obj_dict)
            except NoIdError:
                raise cfgm_common.exceptions.HttpError(400,
                    'Unknown reference in resource update %s %s.'
                    %(obj_type, req_obj_dict))

        # State modification starts from here. Ensure that cleanup is done for all state changes
        cleanup_on_failure = []
        if req_obj_dict is not None:
            req_obj_dict['uuid'] = obj_uuid

        # Permit abort resource update and retrun 202 status code
        get_context().set_state('PENDING_DBE_UPDATE')
        ok, result = r_class.pending_dbe_update(db_obj_dict, req_obj_dict,
                                                req_prop_coll_updates)
        if not ok:
            code, msg = result
            raise cfgm_common.exceptions.HttpError(code, msg)
        if ok and isinstance(result, tuple) and result[0] == 202:
            # Modifications accepted but not applied, pending update
            # returns 202 HTTP OK code to aware clients
            bottle.response.status = 202
            return True, ''

        def stateful_update():
            get_context().set_state('PRE_DBE_UPDATE')
            # type-specific hook
            _req_obj_dict = {}
            if req_obj_dict:
                _req_obj_dict = req_obj_dict
            (ok, result) = r_class.pre_dbe_update(
                obj_uuid, obj_fq_name, _req_obj_dict, self._db_conn,
                prop_collection_updates=req_prop_coll_updates)
            if not ok:
                return (ok, result)
            attr_to_publish = None
            if isinstance(result, dict):
                attr_to_publish = result

            # All resource type can have tag refs but there is some constraints
            # Done after PRE_DBE_UPDATE as tag refs can be modifed in that hook
            ok, result = self._validate_tag_refs(obj_type, req_obj_dict)
            if not ok:
                return False, result

            get_context().set_state('DBE_UPDATE')
            if api_name == 'ref-update':
                # read ref_update args
                ref_obj_type = ref_args.get('ref_obj_type')
                ref_uuid = ref_args.get('ref_uuid')
                ref_data = ref_args.get('data')
                operation = ref_args.get('operation')
                relax_ref_for_delete = ref_args.get('relax_ref_for_delete', False)

                (ok, result) = db_conn.ref_update(
                    obj_type,
                    obj_uuid,
                    ref_obj_type,
                    ref_uuid,
                    ref_data,
                    operation,
                    db_obj_dict['id_perms'],
                    attr_to_publish=attr_to_publish,
                    relax_ref_for_delete=relax_ref_for_delete
                )
            elif req_obj_dict:
                (ok, result) = db_conn.dbe_update(
                    obj_type,
                    obj_uuid,
                    req_obj_dict,
                    attr_to_publish=attr_to_publish,
                )
                # Update quota counter
                if resource_type == 'project' and 'quota' in req_obj_dict:
                    path_prefix = self._path_prefix + obj_uuid
                    try:
                        QuotaHelper._zk_quota_counter_update(
                            path_prefix,
                            req_obj_dict,
                            obj_uuid,
                            db_conn,
                            self.quota_counter)
                    except NoIdError:
                        msg = "Error in initializing quota "\
                              "Internal error : Failed to read resource count"
                        self.config_log(msg, level=SandeshLevel.SYS_ERR)
            elif req_prop_coll_updates:
                (ok, result) = db_conn.prop_collection_update(
                    obj_type,
                    obj_uuid,
                    req_prop_coll_updates,
                    attr_to_publish=attr_to_publish,
                )
            if not ok:
                return (ok, result)

            get_context().set_state('POST_DBE_UPDATE')
            # type-specific hook
            (ok, result) = r_class.post_dbe_update(
                obj_uuid, obj_fq_name, _req_obj_dict, self._db_conn,
                prop_collection_updates=req_prop_coll_updates)
            if not ok:
                return (ok, result)

            return (ok, result)
        # end stateful_update

        try:
            ok, result = stateful_update()
        except Exception as e:
            ok = False
            err_msg = cfgm_common.utils.detailed_traceback()
            result = (500, traceback_err_msg(err_msg))
            self.config_log(err_msg, level=SandeshLevel.SYS_ERR)
        if not ok:
            self.undo(result, obj_type, id=obj_uuid)
            # Revert changes made to quota counter by using DB quota dict
            if resource_type == 'project' and 'quota' in req_obj_dict:
                path_prefix = self._path_prefix + obj_uuid
                try:
                    QuotaHelper._zk_quota_counter_update(
                        path_prefix,
                        db_obj_dict,
                        obj_uuid,
                        self._db_conn,
                        self.quota_counter)
                except NoIdError:
                    err_msg = "Error in rolling back quota count on undo "\
                              "Internal error : Failed to read resource count"
                    self.config_log(err_msg, level=SandeshLevel.SYS_ERR)
            code, msg = result
            raise cfgm_common.exceptions.HttpError(code, msg)

        try:
            self._extension_mgrs['resourceApi'].map_method(
                'post_%s_update' %(obj_type), obj_uuid,
                 req_obj_dict, db_obj_dict)
        except RuntimeError:
            # lack of registered extension leads to RuntimeError
            pass
        except Exception as e:
            err_msg = 'In post_%s_update an extension had error for %s' \
                      %(obj_type, req_obj_dict)
            err_msg += cfgm_common.utils.detailed_traceback()
            self.config_log(err_msg, level=SandeshLevel.SYS_NOTICE)
    # end _put_common

    # parent_type needed for perms check. None for derived objects (eg.
    # routing-instance)
    def _delete_common(self, request, obj_type, uuid, parent_uuid):
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

        fq_name = self._db_conn.uuid_to_fq_name(uuid)
        apiConfig = VncApiCommon()
        apiConfig.object_type = obj_type
        apiConfig.identifier_name=':'.join(fq_name)
        apiConfig.identifier_uuid = uuid
        apiConfig.operation = 'delete'
        self._set_api_audit_info(apiConfig)
        log = VncApiConfigLog(api_log=apiConfig, sandesh=self._sandesh)
        log.send(sandesh=self._sandesh)

        # TODO check api + resource perms etc.
        if not self.is_auth_needed() or not parent_uuid:
            return (True, '')

        """
        Validate parent allows write access. Implicitly trust
        parent info in the object since coming from our DB.
        """
        return self._permissions.check_perms_delete(request, obj_type, uuid,
                                                    parent_uuid)
    # end _http_delete_common

    def _post_validate(self, obj_type=None, obj_dict=None):
        if not obj_dict:
            return

        def _check_field_present(fname):
            fval = obj_dict.get(fname)
            if not fval:
                raise cfgm_common.exceptions.HttpError(
                    400, "Bad Request, no %s in POST body" %(fname))
            return fval
        fq_name = _check_field_present('fq_name')

        # well-formed name checks
        if illegal_xml_chars_RE.search(fq_name[-1]):
            raise cfgm_common.exceptions.HttpError(400,
                "Bad Request, name has illegal xml characters")
        if obj_type == 'route_target':
            invalid_chars = self._INVALID_NAME_CHARS - set(':')
        else:
            invalid_chars = self._INVALID_NAME_CHARS
        if any((c in invalid_chars) for c in fq_name[-1]):
            raise cfgm_common.exceptions.HttpError(400,
                "Bad Request, name has one of invalid chars %s"
                %(invalid_chars))
    # end _post_validate

    def validate_parent_type(self, obj_type, obj_dict):
        parent_type = obj_dict.get('parent_type')
        r_class = self.get_resource_class(obj_type)
        allowed_parent_types = r_class.parent_types
        if parent_type:
            if  parent_type not in allowed_parent_types:
                raise cfgm_common.exceptions.HttpError(
                    400, 'Invalid parent type: %s. Allowed types: %s' % (
                        parent_type, allowed_parent_types))
        elif (len(allowed_parent_types) > 1 and
              'config-root' not in allowed_parent_types):
            raise cfgm_common.exceptions.HttpError(
                400, 'Missing parent type: %s. Allowed types: %s' % (
                    parent_type, allowed_parent_types))
        elif len(allowed_parent_types) == 1:
            parent_type = allowed_parent_types[0]
        if parent_type in ('config-root', None):
            if len(obj_dict['fq_name']) != 1:
                raise cfgm_common.exceptions.HttpError(
                    400, 'Invalid fq-name of an object with no parent: %s' % (
                    obj_dict['fq_name']))
        elif len(obj_dict['fq_name']) < 2:
            raise cfgm_common.exceptions.HttpError(
                400, 'Invalid fq-name for object with parent_type %s: %s' % (
                parent_type, obj_dict['fq_name']))
    # end validate_parent_type


    def _post_common(self, obj_type, obj_dict):
        self._ensure_services_conn(
            'http_post', obj_type, obj_fq_name=obj_dict.get('fq_name'))

        if not obj_dict:
            # TODO check api + resource perms etc.
            return (True, None)

        # Fail if object exists already
        try:
            obj_uuid = self._db_conn.fq_name_to_uuid(
                obj_type, obj_dict['fq_name'])
            raise cfgm_common.exceptions.HttpError(
                409, '' + pformat(obj_dict['fq_name']) +
                ' already exists with uuid: ' + obj_uuid)
        except NoIdError:
            pass

        self.validate_parent_type(obj_type, obj_dict)
        # Ensure object has at least default permissions set
        self._ensure_id_perms_present(None, obj_dict)
        self._ensure_perms2_present(obj_type, None, obj_dict,
            get_request().headers.environ.get('HTTP_X_PROJECT_ID', None))

        # TODO check api + resource perms etc.

        uuid_in_req = obj_dict.get('uuid', None)

        # Set the display name
        if (('display_name' not in obj_dict) or
            (obj_dict['display_name'] is None)):
            obj_dict['display_name'] = obj_dict['fq_name'][-1]

        fq_name_str = ":".join(obj_dict['fq_name'])
        apiConfig = VncApiCommon()
        apiConfig.object_type = obj_type
        apiConfig.identifier_name=fq_name_str
        apiConfig.identifier_uuid = uuid_in_req
        apiConfig.operation = 'post'
        try:
            body = json.dumps(get_request().json)
        except:
            body = str(get_request().json)
        apiConfig.body = body
        if uuid_in_req:
            if uuid_in_req != str(uuid.UUID(uuid_in_req)):
                bottle.abort(400, 'Invalid UUID format: ' + uuid_in_req)
            try:
                fq_name = self._db_conn.uuid_to_fq_name(uuid_in_req)
                raise cfgm_common.exceptions.HttpError(
                    409, uuid_in_req + ' already exists with fq_name: ' +
                    pformat(fq_name))
            except NoIdError:
                pass
            apiConfig.identifier_uuid = uuid_in_req

        self._set_api_audit_info(apiConfig)
        log = VncApiConfigLog(api_log=apiConfig, sandesh=self._sandesh)
        log.send(sandesh=self._sandesh)

        return (True, uuid_in_req)
    # end _post_common

    def reset(self):
        # cleanup internal state/in-flight operations
        if self._amqp_client is not None:
            self._amqp_client.stop()
        if self._db_conn:
            self._db_conn.reset()
    # end reset

    # allocate block of IP addresses from VN. Subnet info expected in request
    # body
    def vn_ip_alloc_http_post(self, id):
        try:
            vn_fq_name = self._db_conn.uuid_to_fq_name(id)
        except NoIdError:
            raise cfgm_common.exceptions.HttpError(
                404, 'Virtual Network ' + id + ' not found!')

        # expected format {"subnet_list" : "2.1.1.0/24", "count" : 4}
        req_dict = get_request().json
        count = req_dict.get('count', 1)
        subnet = req_dict.get('subnet')
        family = req_dict.get('family')
        try:
            result = self.get_resource_class('virtual_network').ip_alloc(
                vn_fq_name, subnet, count, family)
        except vnc_addr_mgmt.AddrMgmtSubnetUndefined as e:
            raise cfgm_common.exceptions.HttpError(404, str(e))
        except vnc_addr_mgmt.AddrMgmtSubnetExhausted as e:
            raise cfgm_common.exceptions.HttpError(409, str(e))

        return result
    # end vn_ip_alloc_http_post

    # free block of ip addresses to subnet
    def vn_ip_free_http_post(self, id):
        try:
            vn_fq_name = self._db_conn.uuid_to_fq_name(id)
        except NoIdError:
            raise cfgm_common.exceptions.HttpError(
                404, 'Virtual Network ' + id + ' not found!')

        """
          {
            "subnet" : "2.1.1.0/24",
            "ip_addr": [ "2.1.1.239", "2.1.1.238", "2.1.1.237", "2.1.1.236" ]
          }
        """

        req_dict = get_request().json
        ip_list = req_dict['ip_addr'] if 'ip_addr' in req_dict else []
        result = self.get_resource_class('virtual_network').ip_free(
            vn_fq_name, ip_list)
        return result
    # end vn_ip_free_http_post

    # return no. of  IP addresses from VN/Subnet
    def vn_subnet_ip_count_http_post(self, id):
        try:
            vn_fq_name = self._db_conn.uuid_to_fq_name(id)
        except NoIdError:
            raise cfgm_common.exceptions.HttpError(
                404, 'Virtual Network ' + id + ' not found!')

        # expected format {"subnet_list" : ["2.1.1.0/24", "1.1.1.0/24"]
        req_dict = get_request().json
        try:
            (ok, result) = self._db_conn.dbe_read('virtual_network', id)
        except NoIdError as e:
            raise cfgm_common.exceptions.HttpError(404, str(e))
        except Exception as e:
            ok = False
            result = cfgm_common.utils.detailed_traceback()
            self.config_log(result, level=SandeshLevel.SYS_ERR)
        if not ok:
            raise cfgm_common.exceptions.HttpError(
                500, traceback_err_msg(result))

        obj_dict = result
        subnet_list = req_dict[
            'subnet_list'] if 'subnet_list' in req_dict else []
        result = self.get_resource_class('virtual_network').subnet_ip_count(
            vn_fq_name, subnet_list)
        return result
    # end vn_subnet_ip_count_http_post

    # check if token validatation needed
    def is_auth_needed(self):
        return self.aaa_mode != 'no-auth'

    def is_rbac_enabled(self):
        return self.aaa_mode == 'rbac'

    @property
    def aaa_mode(self):
        return self._args.aaa_mode

    @aaa_mode.setter
    def aaa_mode(self, mode):
        self._args.aaa_mode = mode

    # indication if multi tenancy with rbac is enabled or disabled
    def aaa_mode_http_get(self):
        return {'aaa-mode': self.aaa_mode}

    def aaa_mode_http_put(self):
        aaa_mode = get_request().json['aaa-mode']
        if aaa_mode not in AAA_MODE_VALID_VALUES:
            raise ValueError('Invalid aaa-mode %s' % aaa_mode)

        ok, result = self._auth_svc.validate_user_token()
        if not ok:
            code, msg = result
            self.config_object_error(None, None, None, 'aaa_mode_http_put',
                                     msg)
            raise cfgm_common.exceptions.HttpError(code, msg)
        if not self.is_admin_request():
            raise cfgm_common.exceptions.HttpError(403, " Permission denied")

        self.aaa_mode = aaa_mode
        if self.is_rbac_enabled():
            self._create_default_rbac_rule()
        return {'aaa-mode': self.aaa_mode}
    # end

    @property
    def cloud_admin_role(self):
        return self._args.cloud_admin_role

    @property
    def global_read_only_role(self):
        return self._args.global_read_only_role

    def set_tag(self):
        self._post_common(None, {})

        req_dict = get_request().json
        obj_type = req_dict.pop('obj_type', None)
        obj_uuid = req_dict.pop('obj_uuid', None)
        need_update = False

        if obj_type is None or obj_uuid is None:
            msg = "Object type and UUID must be specified"
            raise cfgm_common.exceptions.HttpError(400, msg)

        try:
            r_class = self.get_resource_class(obj_type)
        except TypeError as e:
            raise cfgm_common.exceptions.HttpError(400, str(e))

        try:
            ok, result = self._db_conn.dbe_read(
                r_class.object_type,
                obj_uuid,
                obj_fields=['parent_type', 'perms2', 'tag_refs'],
            )
        except NoIdError as e:
            raise cfgm_common.exceptions.HttpError(404, str(e))
        if not ok:
            raise cfgm_common.exceptions.HttpError(*result)
        obj_dict = result

        def _locate_tag(type, value, is_global=False):
            name = type + "=" + value
            # unless global, inherit project id from caller
            if is_global:
                fq_name = [name]
            else:
                fq_name = copy.deepcopy(obj_dict['fq_name'])
                if obj_type == 'project':
                    fq_name.append(name)
                elif ('parent_type' in obj_dict and
                        obj_dict['parent_type'] == 'project'):
                    fq_name[-1] = name
                elif ('perms2' in obj_dict and
                        is_uuid_like(obj_dict['perms2']['owner'])):
                    parent_uuid = str(uuid.UUID(obj_dict['perms2']['owner']))
                    try:
                        fq_name = self._db_conn.uuid_to_fq_name(parent_uuid)
                    except NoIdError:
                        msg = ("Cannot find %s %s owner" %
                               (obj_type, obj_dict['uuid']))
                        raise cfgm_common.exceptions.HttpError(404, msg)
                    fq_name.append(name)
                else:
                    msg = ("Not able to determine the scope of the tag '%s'" %
                           name)
                    raise cfgm_common.exceptions.HttpError(404, msg)

            # lookup (validate) tag
            try:
                tag_uuid = self._db_conn.fq_name_to_uuid('tag', fq_name)
            except NoIdError:
                msg = "Tag with FQName %s not found" % pformat(fq_name)
                raise cfgm_common.exceptions.HttpError(404, msg)

            return fq_name, tag_uuid

        refs_per_type = {}
        for ref in obj_dict.get('tag_refs', []):
            ref_type = ref['to'][-1].partition('=')[0]
            refs_per_type.setdefault(ref_type, []).append(ref)

        for tag_type, attrs in list(req_dict.items()):
            tag_type = tag_type.lower()

            # If the body of a Tag type is None, all references to that Tag
            # type are remove on the resource
            if attrs is None:
                for ref in refs_per_type.get(tag_type, []):
                    need_update = True
                    obj_dict['tag_refs'].remove(ref)
                refs_per_type[tag_type] = []
                continue

            # Else get defined values and update Tag references on the resource
            is_global = attrs.get('is_global', False)
            value = attrs.get('value')
            add_values = set(attrs.get('add_values', []))
            delete_values = set(attrs.get('delete_values', []))

            # Tag type is unique per object, unless
            # TAG_TYPE_NOT_UNIQUE_PER_OBJECT type
            if tag_type not in TAG_TYPE_NOT_UNIQUE_PER_OBJECT:
                if add_values or delete_values:
                    msg = ("Tag type %s cannot be set multiple times on a "
                           "same object." % tag_type)
                    raise cfgm_common.exceptions.HttpError(400, msg)

            # address-group object can only be associated with label
            if (obj_type == 'address_group' and
                    tag_type not in TAG_TYPE_AUTHORIZED_ON_ADDRESS_GROUP):
                msg = ("Invalid tag type %s for object type %s" %
                       (tag_type, obj_type))
                raise cfgm_common.exceptions.HttpError(400, msg)

            refs_per_values = {}
            if tag_type in refs_per_type:
                refs_per_values = {ref['to'][-1].partition('=')[2]: ref for ref
                                   in refs_per_type[tag_type]}

            if tag_type not in TAG_TYPE_NOT_UNIQUE_PER_OBJECT:
                if value is None or isinstance(value, list):
                    msg = "No valid value provided for tag type %s" % tag_type
                    raise cfgm_common.exceptions.HttpError(400, msg)

                # don't need to update if tag type with same value already
                # referenced
                if value in refs_per_values:
                    continue

                for ref in list(refs_per_values.values()):
                    need_update = True
                    # object already have a reference to that tag type with a
                    # different value, remove it
                    obj_dict['tag_refs'].remove(ref)

                # finally, reference the tag type with the new value
                tag_fq_name, tag_uuid = _locate_tag(tag_type, value, is_global)
                obj_dict.setdefault('tag_refs', []).append({
                    'uuid': tag_uuid,
                    'to': tag_fq_name,
                    'attr': None,
                })
                need_update = True
            else:
                # Add 'value' attribut to 'add_values' list if not null
                if value is not None:
                    add_values.add(value)
                for add_value in add_values - set(refs_per_values.keys()):
                    need_update = True
                    tag_fq_name, tag_uuid = _locate_tag(tag_type, add_value,
                                                        is_global)
                    obj_dict.setdefault('tag_refs', []).append({
                        'uuid': tag_uuid,
                        'to': tag_fq_name,
                        'attr': None,
                    })
                for del_value in delete_values & set(refs_per_values.keys()):
                    need_update = True
                    obj_dict['tag_refs'].remove(refs_per_values[del_value])

        if need_update:
            self.internal_request_update(obj_type, obj_uuid, obj_dict)

        return {}

    def security_policy_draft(self):
        self._post_common(None, {})

        req_dict = get_request().json
        scope_uuid = req_dict.pop('scope_uuid')
        action = req_dict.pop('action')

        pm_class = self.get_resource_class('policy-management')
        try:
            scope_type = self._db_conn.uuid_to_obj_type(scope_uuid)
        except NoIdError as e:
            msg = ("Cannot find scope where pending security resource are "
                   "own: %s" % str(e))
        scope_class = self.get_resource_class(scope_type)
        scope_fq_name = self._db_conn.uuid_to_fq_name(scope_uuid)
        pm_fq_name = [POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT]
        if (scope_type == GlobalSystemConfig.object_type and
                scope_fq_name == GlobalSystemConfig().fq_name):
            parent_type = PolicyManagement.resource_type
            parent_fq_name = PolicyManagement().fq_name
            parent_uuid = self._global_pm_uuid
        else:
            pm_fq_name = scope_fq_name + pm_fq_name
            parent_type = scope_class.resource_type
            parent_fq_name = scope_fq_name
            parent_uuid = scope_uuid

        ok, result = pm_class.locate(
            fq_name=pm_fq_name,
            create_it=False,
            fields=['%ss' % type for type in SECURITY_OBJECT_TYPES],
        )
        if not ok and result[0] == 404:
            # Draft dedicated policy management does not exists, the draft mode
            # is not enabled on the scope
            msg = ("Security draft mode is not enabled on the %s %s (%s)" %
                   (scope_type.replace('_', ' ').title(), scope_fq_name,
                    scope_uuid))
            raise cfgm_common.exceptions.HttpError(400, msg)
        if not ok:
            raise cfgm_common.exceptions.HttpError(result[0], result[1])
        pm = result

        scope_lock = self._db_conn._zk_db._zk_client.write_lock(
            '%s/%s/%s' % (
                self.security_lock_prefix, scope_type,
                ':'.join(scope_fq_name)
            ),
            'api-server-%s %s' % (socket.getfqdn(self._args.listen_ip_addr), action),
        )
        try:
            acquired_lock = scope_lock.acquire(timeout=1)
        except LockTimeout:
            acquired_lock = False
        if acquired_lock:
            try:
                if action == 'commit':
                    self._security_commit_resources(scope_type, parent_type,
                                                    parent_fq_name,
                                                    parent_uuid, pm)
                elif action == 'discard':
                    self._security_discard_resources(pm)
                else:
                    msg = "Only 'commit' or 'discard' actions are supported"
                    raise cfgm_common.exceptions.HttpError(400, msg)
            finally:
                scope_lock.release()
        else:
            contenders = scope_lock.contenders()
            action_in_progress = '<unknown action>'
            if len(contenders) > 0 and contenders[0]:
                _, _, action_in_progress = contenders[0].partition(' ')
            msg = ("Security resource modifications or commit/discard action "
                   "on %s '%s' (%s) scope is under progress. Try again later."
                   % (scope_type.replace('_', ' ').title(),
                      ':'.join(scope_fq_name), scope_uuid))
            raise cfgm_common.exceptions.HttpError(400, msg)

        # TODO(ethuleau): we could return some stats or type/uuid resources
        # actions which were done during commit or discard?
        return {}

    def _security_commit_resources(self, scope_type, parent_type,
                                   parent_fq_name, parent_uuid, pm):
        updates = []
        deletes = []
        held_refs = []
        for type_name in SECURITY_OBJECT_TYPES:
            r_class = self.get_resource_class(type_name)
            for child in pm.get('%ss' % r_class.object_type, []):
                ok, result = r_class.locate(child['to'], child['uuid'],
                                            create_it=False)
                if not ok:
                    continue
                draft = result
                fq_name = parent_fq_name + [child['to'][-1]]
                try:
                    uuid = self._db_conn.fq_name_to_uuid(r_class.object_type,
                                                         fq_name)
                except NoIdError:
                    # No original version found, new resource created
                    uuid = None
                self._holding_backrefs(updates, held_refs, scope_type,
                                       r_class.object_type, fq_name, draft)
                # Purge pending resource as we re-use the same UUID
                self.internal_request_delete(r_class.object_type,
                                             child['uuid'])
                if uuid and draft['draft_mode_state'] == 'deleted':
                    # The resource is removed, we can purge original resource
                    deletes.append((r_class.object_type, uuid))
                elif uuid and draft['draft_mode_state'] == 'updated':
                    # Update orginal resource with pending resource
                    draft.pop('fq_name', None)
                    draft.pop('uuid', None)
                    draft.pop('draft_mode_state', None)
                    if 'id_perms' in draft:
                        draft['id_perms'].pop('uuid', None)
                    draft['parent_type'] = parent_type
                    draft['parent_uuid'] = parent_uuid
                    # if a ref type was purge when the draft mode is enabled,
                    # set the ref to an empty list to ensure all refs will be
                    # removed when resource will be updated/committed
                    for ref_type in r_class.ref_fields:
                        if ref_type not in draft:
                            draft[ref_type] = []
                    self._update_fq_name_security_refs(
                        parent_fq_name, pm['fq_name'], type_name, draft)
                    updates.append(('update', (r_class.resource_type, uuid,
                                            copy.deepcopy(draft))))
                elif not uuid and draft['draft_mode_state'] == 'created':
                    # Create new resource with pending values (re-use UUID)
                    draft.pop('id_perms', None)
                    draft.pop('perms2', None)
                    draft.pop('draft_mode_state', None)
                    draft['fq_name'] = fq_name
                    draft['parent_type'] = parent_type
                    draft['parent_uuid'] = parent_uuid
                    self._update_fq_name_security_refs(
                        parent_fq_name, pm['fq_name'], type_name, draft)
                    updates.append(('create', (r_class.resource_type,
                                            copy.deepcopy(draft))))
                else:
                    msg = (
                        "Try to commit a security resource %s (%s) with "
                        "invalid state '%s'. Ignore it." %
                        (':'.join(draft.get('fq_name', ['FQ name unknown'])),
                        draft.get('uuid', 'UUID unknown'),
                        draft.get('draft_mode_state', 'No draft mode state'))
                    )
                    self.config_log(msg, level=SandeshLevel.SYS_WARN)

        # Need to create/update leaf resources first as they could be
        # referenced by another create/updated resource (e.g.: FP -> FP)
        updates.reverse()  # order is: AG, SG, FR, FP and APS
        for action, args in updates:
            getattr(self, 'internal_request_%s' % action)(*args)

        # Postpone delete to be sure deleted resource not anymore
        # referenced and delete resource with ref before resource with backref
        for args in deletes:  # order is: APS, FP, FR, SG and AG
            self.internal_request_delete(*args)

        for args, kwargs in held_refs:
            self.internal_request_ref_update(*args, **kwargs)

    @staticmethod
    def _update_fq_name_security_refs(parent_fq_name, pm_fq_name, res_type,
                                      draft):
        for ref_type in SECURITY_OBJECT_TYPES:
            for ref in draft.get('%s_refs' % ref_type, []):
                if ref['to'][:-1] == pm_fq_name:
                    ref['to'] = parent_fq_name + [ref['to'][-1]]

        if res_type == 'firewall_rule':
            for ep in [draft.get('endpoint_1', {}),
                       draft.get('endpoint_2', {})]:
                ag_fq_name = ep.get('address_group', [])
                if ag_fq_name and ag_fq_name.split(':')[:-1] == pm_fq_name:
                    ep['address_group'] = ':'.join(parent_fq_name + [
                        ag_fq_name.split(':')[-1]])

    def _holding_backrefs(self, updates, held_refs, scope_type, obj_type,
                          fq_name, obj_dict):
        backref_fields = {'%s_back_refs' % t for t in SECURITY_OBJECT_TYPES}
        if (scope_type == GlobalSystemConfig().object_type and
                obj_dict['draft_mode_state'] != 'deleted'):
            for backref_field in set(obj_dict.keys()) & backref_fields:
                backref_type = backref_field[:-10]
                for backref in copy.deepcopy(obj_dict.get(backref_field, [])):
                    # if it's a backref to global resource let it
                    if backref['to'][0] in [PolicyManagement().name,
                            POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT]:
                        continue
                    self.internal_request_ref_update(
                        backref_type,
                        backref['uuid'],
                        'DELETE',
                        obj_type,
                        ref_uuid=obj_dict['uuid'],
                    )
                    if obj_type == AddressGroup.object_type:
                        # Is not allowed to directly create Address Group
                        # reference to a Firewall Rule, use its endpoints
                        # address-group property
                        backref_class = self.get_resource_class(backref_type)
                        ok, result = backref_class.locate(
                            backref['to'],
                            backref['uuid'],
                            create_it=False,
                            fields=['endpoint_1', 'endpoint_2'])
                        if not ok:
                            msg = ("Cannot read Firewall Rule %s (%s)" %
                                   (backref['to'], backref['uuid']))
                            raise cfgm_common.exceptions.HttpError(400, msg)
                        fr = result
                        for ep_type in ['endpoint_1', 'endpoint_2']:
                            if (ep_type in fr and
                                    fr[ep_type].get('address_group', '') ==\
                                    ':'.join(obj_dict['fq_name'])):
                                ept = FirewallRuleEndpointType(
                                    address_group=':'.join(fq_name))
                                updates.append(
                                    ('update',
                                     (FirewallRule.resource_type, fr['uuid'],
                                      {ep_type: vars(ept)})))
                    else:
                        held_refs.append(
                            ((backref_type, backref['uuid'], 'ADD', obj_type),
                            {
                                'ref_fq_name': fq_name,
                                'attr': backref.get('attr')
                            }
                            )
                        )
                    obj_dict[backref_field].remove(backref)

    def _security_discard_resources(self, pm):
        for type_name in SECURITY_OBJECT_TYPES:
            r_class = self.get_resource_class(type_name)
            for child in pm.get('%ss' % r_class.object_type, []):
                self.internal_request_delete(r_class.object_type,
                                             child['uuid'])

# end VncApiServer

def main(args_str=None, server=None):
    vnc_api_server = server

    pipe_start_app = vnc_api_server.get_pipe_start_app()
    server_ip = vnc_api_server.get_listen_ip()
    server_port = vnc_api_server.get_server_port()

    enable_ssl = vnc_api_server.get_enable_ssl()
    if enable_ssl:
        certfile=vnc_api_server.get_certfile()
        keyfile=vnc_api_server.get_keyfile()
        ca_cert=vnc_api_server.get_ca_cert()

    """ @sigchld
    Disable handling of SIG_CHLD for now as every keystone request to validate
    token sends SIG_CHLD signal to API server.
    """
    #hub.signal(signal.SIGCHLD, vnc_api_server.sigchld_handler)
    hub.signal(signal.SIGTERM, vnc_api_server.sigterm_handler)
    hub.signal(signal.SIGHUP, vnc_api_server.sighup_handler)
    if pipe_start_app is None:
        pipe_start_app = vnc_api_server.api_bottle
    try:
        vnc_args = vnc_api_server.get_args()
        if enable_ssl:
            if not (certfile and keyfile and ca_cert):
                msg = "SSL is enabled but one or more of these options " \
                      "config_api_ssl_keyfile, config_api_ssl_certfile, " \
                      "config_api_ssl_ca_cert not specified"
                raise cfgm_common.exceptions.VncError(msg)
            bottle.run(app=pipe_start_app, host=server_ip, port=server_port,
                       ca_certs=ca_cert, keyfile=keyfile, certfile=certfile,
                       server=get_bottle_server(server._args.max_requests,
                       tcp_keepalive_interval=vnc_args.tcp_keepalive_interval,
                       tcp_keepalive_idle_time=vnc_args.tcp_keepalive_idle_time,
                       tcp_keepalive_probes=vnc_args.tcp_keepalive_probes),
                       ssl_version=ssl.PROTOCOL_TLSv1_2)
        else:
            bottle.run(app=pipe_start_app, host=server_ip, port=server_port,
                       server=get_bottle_server(server._args.max_requests,
                       tcp_keepalive_interval=vnc_args.tcp_keepalive_interval,
                       tcp_keepalive_idle_time=vnc_args.tcp_keepalive_idle_time,
                       tcp_keepalive_probes=vnc_args.tcp_keepalive_probes))
    except KeyboardInterrupt:
        # quietly handle Ctrl-C
        pass
    finally:
        # always cleanup gracefully
        vnc_api_server.reset()

# end main

def server_main(args_str=None):
    vnc_cgitb.enable(format='text')

    main(args_str, VncApiServer(args_str))
#server_main

if __name__ == "__main__":
    server_main()

