#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# Opserver
#
# Operational State Server for VNC
#
 
from gevent import monkey
monkey.patch_all()
try:
    from collections import OrderedDict
except ImportError:
    # python 2.6 or earlier, use backport
    from ordereddict import OrderedDict
from collections import namedtuple
TableSchema = namedtuple("TableSchema", ("name", "datatype", "index", "suffixes"))
from uveserver import UVEServer
import math
import sys
import ConfigParser
import bottle
import json
import uuid
import argparse
import time
import redis
import base64
import socket
import struct
import signal
import random
import hashlib
import errno
import copy
import datetime
import platform
from analytics_db import AnalyticsDb

from pysandesh.util import UTCTimestampUsec
from pysandesh.sandesh_base import *
from pysandesh.sandesh_session import SandeshWriter
from pysandesh.gen_py.sandesh_trace.ttypes import SandeshTraceRequest
from pysandesh.connection_info import ConnectionState
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, CategoryNames,\
     ModuleCategoryMap, Module2NodeType, NodeTypeNames, ModuleIds,\
     INSTANCE_ID_DEFAULT, \
     ANALYTICS_API_SERVER_DISCOVERY_SERVICE_NAME, ALARM_GENERATOR_SERVICE_NAME, \
     OpServerAdminPort, CLOUD_ADMIN_ROLE, APIAAAModes, \
     AAA_MODE_CLOUD_ADMIN, AAA_MODE_NO_AUTH, AAA_MODE_RBAC, \
     ServicesDefaultConfigurationFiles, SERVICE_OPSERVER
from sandesh.viz.constants import _TABLES, _OBJECT_TABLES,\
    _OBJECT_TABLE_SCHEMA, _OBJECT_TABLE_COLUMN_VALUES, \
    _STAT_TABLES, STAT_OBJECTID_FIELD, STAT_VT_PREFIX, \
    STAT_TIME_FIELD, STAT_TIMEBIN_FIELD, STAT_UUID_FIELD, \
    STAT_SOURCE_FIELD, SOURCE, MODULE
from sandesh.viz.constants import *
from sandesh.analytics.ttypes import *
from sandesh.nodeinfo.ttypes import NodeStatusUVE, NodeStatus
from sandesh.nodeinfo.cpuinfo.ttypes import *
from sandesh.nodeinfo.process_info.ttypes import *
from opserver_util import OpServerUtils
from opserver_util import AnalyticsDiscovery
from sandesh_req_impl import OpserverSandeshReqImpl
from sandesh.analytics_database.ttypes import *
from sandesh.analytics_database.constants import PurgeStatusString
from sandesh.analytics.ttypes import DbInfoSetRequest, \
     DbInfoGetRequest, DbInfoResponse
from overlay_to_underlay_mapper import OverlayToUnderlayMapper, \
     OverlayToUnderlayMapperError
from generator_introspect_util import GeneratorIntrospectUtil
from stevedore import hook, extension
from partition_handler import PartInfo, UveStreamer, UveCacheProcessor
from functools import wraps
from vnc_cfg_api_client import VncCfgApiClient
from opserver_local import LocalApp
from opserver_util import AnalyticsDiscovery
from strict_redis_wrapper import StrictRedisWrapper
from sandesh.analytics_api_info.ttypes import AnalyticsApiInfoUVE, \
    AnalyticsApiInfo, UVEDbCacheTablesRequest, UVEDbCacheTable, \
    UVEDbCacheTablesResponse, UVEDbCacheTableKeysRequest, \
    UVEDbCacheTableKey, UVEDbCacheTableKeysResponse, \
    UVEDbCacheUveRequest, UVEDbCacheUveResponse
from cfgm_common.exceptions import BadRequest, HttpError, PermissionDenied, AuthFailed


_ERRORS = {
    errno.EBADMSG: 400,
    errno.ENOBUFS: 403,
    errno.EINVAL: 404,
    errno.ENOENT: 410,
    errno.EIO: 500,
    errno.EBUSY: 503
}

@bottle.error(400)
@bottle.error(403)
@bottle.error(404)
@bottle.error(410)
@bottle.error(500)
@bottle.error(503)
def opserver_error(err):
    return err.body
#end opserver_error

class LinkObject(object):

    def __init__(self, name, href):
        self.name = name
        self.href = href
    # end __init__
# end class LinkObject

class OpserverStdLog(object):
    def __init__(self, server_name, http_port):
        self._server_name = server_name
        self._port = http_port

    def write(self, text):
        sys.stderr.write('[' + self._server_name + ':' + str(self._port) + ']' + text)

class ContrailGeventServer(bottle.GeventServer):
    def run(self, handler):
        from gevent import wsgi as wsgi_fast, pywsgi, monkey, local
        if self.options.get('monkey', True):
            import threading
            if not threading.local is local.local: monkey.patch_all()
        wsgi = wsgi_fast if self.options.get('fast') else pywsgi
        self._std_log = OpserverStdLog("API", self.port)
        self.srv = wsgi.WSGIServer((self.host, self.port), handler, log = self._std_log)
        self.srv.serve_forever()
    def stop(self):
        if hasattr(self, 'srv'):
            self.srv.stop()
            gevent.sleep(0)

def obj_to_dict(obj):
    # Non-null fields in object get converted to json fields
    return dict((k, v) for k, v in obj.__dict__.iteritems())
# end obj_to_dict


def redis_query_start(host, port, redis_password, qid, inp, columns):
    redish = StrictRedisWrapper(db=0, host=host, port=port,
                               password=redis_password)
    for key, value in inp.items():
        redish.hset("QUERY:" + qid, key, json.dumps(value))
    col_list = []
    if columns is not None:
        for col in columns:
            m = TableSchema(name = col.name, datatype = col.datatype, index = col.index, suffixes = col.suffixes)
            col_list.append(m._asdict())
    query_metadata = {}
    query_metadata['enqueue_time'] = OpServerUtils.utc_timestamp_usec()
    redish.hset("QUERY:" + qid, 'query_metadata', json.dumps(query_metadata))
    redish.hset("QUERY:" + qid, 'enqueue_time',
                OpServerUtils.utc_timestamp_usec())
    redish.hset("QUERY:" + qid, 'table_schema', json.dumps(col_list))
    redish.lpush("QUERYQ", qid)

    res = redish.blpop("REPLY:" + qid, 10)
    if res is None:
        return None
    # Put the status back on the queue for the use of the status URI
    redish.lpush("REPLY:" + qid, res[1])

    resp = json.loads(res[1])
    return int(resp["progress"])
# end redis_query_start


def redis_query_status(host, port, redis_password, qid):
    redish = StrictRedisWrapper(db=0, host=host, port=port,
                               password=redis_password)
    resp = {"progress": 0}
    chunks = []
    # For now, the number of chunks will be always 1
    res = redish.lrange("REPLY:" + qid, -1, -1)
    if not res:
        return None
    chunk_resp = json.loads(res[0])
    ttl = redish.ttl("REPLY:" + qid)
    if int(ttl) != -1:
        chunk_resp["ttl"] = int(ttl)
    query_time = redish.hmget("QUERY:" + qid, ["start_time", "end_time"])
    chunk_resp["start_time"] = query_time[0]
    chunk_resp["end_time"] = query_time[1]
    if chunk_resp["progress"] == 100:
        chunk_resp["href"] = "/analytics/query/%s/chunk-final/%d" % (qid, 0)
    chunks.append(chunk_resp)
    resp["progress"] = chunk_resp["progress"]
    resp["chunks"] = chunks
    return resp
# end redis_query_status


def redis_query_chunk_iter(host, port, redis_password, qid, chunk_id):
    redish = StrictRedisWrapper(db=0, host=host, port=port,
                               password=redis_password)

    iters = 0
    fin = False

    while not fin:
        #import pdb; pdb.set_trace()
        # Keep the result line valid while it is being read
        redish.persist("RESULT:" + qid + ":" + str(iters))
        elems = redish.lrange("RESULT:" + qid + ":" + str(iters), 0, -1)
        yield elems
        if elems == []:
            fin = True
        else:
            redish.delete("RESULT:" + qid + ":" + str(iters), 0, -1)
        iters += 1

    return
# end redis_query_chunk_iter


def redis_query_chunk(host, port, redis_password, qid, chunk_id):
    res_iter = redis_query_chunk_iter(host, port, redis_password, qid, chunk_id)

    dli = u''
    starter = True
    fin = False
    yield u'{"value": ['
    outcount = 0
    while not fin:

        #import pdb; pdb.set_trace()
        # Keep the result line valid while it is being read
        elems = res_iter.next()

        fin = True
        for elem in elems:
            fin = False
            outcount += 1
            if starter:
                dli += '\n' + elem
                starter = False
            else:
                dli += ', ' + elem
        if not fin:
            yield dli + '\n'
            dli = u''

    if outcount == 0:
        yield '\n' + u']}'
    else:
        yield u']}'
    return
# end redis_query_chunk



def redis_query_result(host, port, redis_password, qid):
    try:
        status = redis_query_status(host, port, redis_password, qid)
    except redis.exceptions.ConnectionError:
        yield bottle.HTTPError(_ERRORS[errno.EIO],
                'Failure in connection to the query DB')
    except Exception as e:
        self._logger.error("Exception: %s" % e)
        yield bottle.HTTPError(_ERRORS[errno.EIO], 'Error: %s' % e)
    else:
        if status is None:
            yield bottle.HTTPError(_ERRORS[errno.ENOENT], 
                    'Invalid query id (or) query result purged from DB')
        if status['progress'] == 100:
            for chunk in status['chunks']:
                chunk_id = int(chunk['href'].rsplit('/', 1)[1])
                for gen in redis_query_chunk(host, port, redis_password, qid, 
                                             chunk_id):
                    yield gen
        else:
            yield {}
    return
# end redis_query_result

def redis_query_result_dict(host, port, redis_password, qid):

    stat = redis_query_status(host, port, redis_password, qid)
    prg = int(stat["progress"])
    res = []

    if (prg < 0) or (prg == 100):

        done = False
        gen = redis_query_result(host, port, redis_password, qid)
        result = u''
        while not done:
            try:
                result += gen.next()
                #import pdb; pdb.set_trace()
            except StopIteration:
                done = True
        res = (json.loads(result))['value']

    return prg, res
# end redis_query_result_dict


def redis_query_info(redish, qid):
    query_data = {}
    query_dict = redish.hgetall('QUERY:' + qid)
    query_metadata = json.loads(query_dict['query_metadata'])
    del query_dict['query_metadata']
    query_data['query_id'] = qid
    query_data['query'] = str(query_dict)
    query_data['enqueue_time'] = query_metadata['enqueue_time']
    return query_data
# end redis_query_info


class OpStateServer(object):

    def __init__(self, logger, redis_password=None):
        self._logger = logger
        self._redis_list = []
        self._redis_password= redis_password
    # end __init__

    def update_redis_list(self, redis_list):
        self._redis_list = redis_list
    # end update_redis_list

    def redis_publish(self, msg_type, destination, msg):
        # Get the sandesh encoded in XML format
        sandesh = SandeshWriter.encode_sandesh(msg)
        msg_encode = base64.b64encode(sandesh)
        redis_msg = '{"type":"%s","destination":"%s","message":"%s"}' \
            % (msg_type, destination, msg_encode)
        # Publish message in the Redis bus
        for redis_server in self._redis_list:
            redis_inst = StrictRedisWrapper(redis_server[0],
                                           redis_server[1], db=0,
                                           password=self._redis_password)
            try:
                redis_inst.publish('analytics', redis_msg)
            except redis.exceptions.ConnectionError:
                self._logger.error('No Connection to Redis [%s:%d].'
                                   'Failed to publish message.' \
                                   % (redis_server[0], redis_server[1]))
        return True
    # end redis_publish

# end class OpStateServer

class AnalyticsApiStatistics(object):
    def __init__(self, sandesh, obj_type):
        self.obj_type = obj_type
        self.time_start = UTCTimestampUsec()
        self.api_stats = None
        self.sandesh = sandesh

    def collect(self, resp_size, resp_size_bytes):
        time_finish = UTCTimestampUsec()

        useragent = bottle.request.headers.get('X-Contrail-Useragent')
        if not useragent:
            useragent = bottle.request.headers.get('User-Agent')

        # Create api stats object
        self.api_stats = AnalyticsApiSample(
            operation_type=bottle.request.method,
            remote_ip=bottle.request.environ.get('REMOTE_ADDR'),
            request_url=bottle.request.url,
            object_type=self.obj_type,
            response_time_in_usec=(time_finish - self.time_start),
            response_size_objects=resp_size,
            response_size_bytes=resp_size_bytes,
            resp_code='200',
            useragent=useragent,
            node=self.sandesh.source_id())

    def sendwith(self):
        stats_log = AnalyticsApiStats(api_stats=self.api_stats,
            sandesh=self.sandesh)
        stats_log.send(sandesh=self.sandesh)

class OpServer(object):

    """
    This class provides ReST API to get operational state of
    Contrail VNS system.

    The supported **GET** APIs are:
        * ``/analytics/virtual-network/<name>``
        * ``/analytics/virtual-machine/<name>``
        * ``/analytics/vrouter/<name>``:
        * ``/analytics/bgp-router/<name>``
        * ``/analytics/bgp-peer/<name>``
        * ``/analytics/xmpp-peer/<name>``
        * ``/analytics/collector/<name>``
        * ``/analytics/tables``:
        * ``/analytics/table/<table>``:
        * ``/analytics/table/<table>/schema``:
        * ``/analytics/query/<queryId>``
        * ``/analytics/query/<queryId>/chunk-final/<chunkId>``
        * ``/analytics/send-tracebuffer/<source>/<module>/<name>``
        * ``/analytics/operation/analytics-data-start-time``

    The supported **POST** APIs are:
        * ``/analytics/query``:
        * ``/analytics/operation/database-purge``:
    """

    def validate_user_token(func=None, only_cloud_admin=True,
            get_token_info=False):
        def _validate_user_token_impl(func):
            def _impl(self, *f_args, **f_kwargs):
                if self._args.auth_conf_info.get('aaa_auth_enabled') and \
                        bottle.request.app == bottle.app():
                    user_token = bottle.request.headers.get('X-Auth-Token')
                    if not user_token:
                        raise bottle.HTTPResponse(status = 401,
                            body = 'Authentication required',
                            headers = self._reject_auth_headers())
                    if get_token_info:
                        user_token_info = dict()
                    else:
                        user_token_info = None
                    try:
                        is_cloud_admin = \
                                self._vnc_api_client.is_role_cloud_admin(
                                    user_token,
                                    user_token_info=user_token_info)
                    except (BadRequest, HttpError) as vnce:
                        raise bottle.HTTPResponse(status = vnce.status_code,
                            body = vnce.content)
                    except AuthFailed as afe:
                        raise bottle.HTTPResponse(status = 401,
                            body = 'Authentication required',
                            headers = self._reject_auth_headers())
                    except PermissionDenied as pde:
                        raise bottle.HTTPResponse(status = 403,
                            body = pde.args[0],
                            headers = self._reject_auth_headers())
                    else:
                        bottle.request.is_role_cloud_admin = is_cloud_admin
                        bottle.request.user_token = user_token
                        bottle.request.user_token_info = user_token_info
                        if only_cloud_admin and not is_cloud_admin:
                            raise bottle.HTTPResponse(status = 401,
                                body = 'Authentication required',
                                headers = self._reject_auth_headers())
                return func(self, *f_args, **f_kwargs)
            return _impl
        if not func:
            def _impl_func(func):
                return _validate_user_token_impl(func)
            return _impl_func
        else:
            return _validate_user_token_impl(func)
    # end validate_user_token

    def is_role_cloud_admin(self):
        if self._args.auth_conf_info.get('aaa_auth_enabled') and \
                bottle.request.app == bottle.app():
            return bottle.request.is_role_cloud_admin
        return True
    # end is_role_cloud_admin

    def get_user_token_info(self, token_info):
        if self._args.auth_conf_info.get('aaa_auth_enabled') and \
                bottle.request.app == bottle.app():
            return bottle.request.user_token_info
        return None
    # end get_user_token_info

    def get_user_token(self):
        if self._args.auth_conf_info.get('aaa_auth_enabled') and \
                bottle.request.app == bottle.app():
            return bottle.request.user_token
    # end get_user_token

    """
    returns the list of resources for which user has permissions
    returns None if user is cloud-admin or if mode is no-auth
    """
    def get_resource_list_from_uve_type(self, uve_type, raise_exp=True):
        if self.is_role_cloud_admin():
            return None
        if uve_type in self.UveTypeToConfigObjectType and \
                self._args.aaa_mode == AAA_MODE_RBAC:
            cfg_type = self.UveTypeToConfigObjectType[uve_type]
            return self.get_resource_list(cfg_type)
        if raise_exp:
            raise bottle.HTTPResponse(status = 403,
                        body = 'Operation Forbidden',
                        headers = self._reject_auth_headers())
        else:
            return []
    # end get_resource_list_from_uve_type

    def get_resource_list(self, obj_type):
        if self._args.aaa_mode == AAA_MODE_RBAC and \
                bottle.request.app == bottle.app():
            res_list = self._vnc_api_client.get_resource_list(obj_type,\
                    self.get_user_token())
            if res_list is None:
                return None
            user_accessible_resources = set()
            for _, resources in res_list.iteritems():
                for res in resources:
                    user_accessible_resources.add(':'.join(res['fq_name']))
            return user_accessible_resources
        return None
    # end get_resource_list

    def check_perms_and_update_where_clause(self, request, tabl, tabn):
        """ update where clause if rbac mode is enabled and user is non admin"""
        if self._args.aaa_mode == AAA_MODE_RBAC and not \
                self.is_role_cloud_admin():
            if tabl in self.ObjectLogTypeToConfigObjectType:
                # update where clause for specific objectlog table
                cfg_type = self.ObjectLogTypeToConfigObjectType[tabl]
                cfg_obj_list = self.get_resource_list(cfg_type)
                if cfg_obj_list is not None:
                    self.update_where_clause(request, cfg_obj_list, 'ObjectId')
                return
            elif tabl == 'StatTable.FieldNames.fields':
                # update where clause for list of object tables
                where = request.json['where'][0]
                default_clause =  [{
                    'name' : 'name',
                    'value' : 'OBJECT',
                    'op' : OpServerUtils.MatchOp.PREFIX
                }]
                if where == default_clause:
                    cfg_obj_list = []
                    for tabl in self.ObjectLogTypeToConfigObjectType:
                        cfg_obj_list.append(tabl)
                    self.update_where_clause(request, cfg_obj_list, \
                                'name', 'OBJECT:')
                    return
                # update where clause for list of stat tables
                default_clause[0]['value'] = 'STAT'
                if where == default_clause:
                    cfg_obj_list = []
                    for table in self._VIRTUAL_TABLES:
                        if table.schema.type == 'STAT':
                            for column in table.schema.columns:
                                if column.uve_type in self.UveTypeToConfigObjectType:
                                    cfg_obj_list.append(table.name)
                                    continue
                    self.update_where_clause(request, cfg_obj_list, \
                                'name', 'STAT:')
                    return
            elif tabl is not None and tabl.startswith("StatTable."):
                # update where clause if stat table has any tag with uve_type
                found_uve_type = False
                for column in self._VIRTUAL_TABLES[tabn].schema.columns:
                    if column.index and column.uve_type:
                        if column.uve_type in self.UveTypeToConfigObjectType:
                            found_uve_type = True
                            cfg_type = self.UveTypeToConfigObjectType[column.uve_type]
                            cfg_obj_list = self.get_resource_list(cfg_type)
                            if cfg_obj_list is not None:
                                if len(cfg_obj_list) == 0:
                                    request.json['where'] = []
                                    break
                                self.update_where_clause(request, cfg_obj_list,\
                                    column.name)
                if found_uve_type:
                    return
        if not self.is_role_cloud_admin():
            raise bottle.HTTPResponse(status = 403,
                        body = 'Operation Forbidden',
                        headers = self._reject_auth_headers())
    #end check_perms_and_update_where_clause

    def update_where_clause(self, request, obj_list, name, prefix=None):
        where_clause_list = []
        if 'where' in request.json:
            where_clause_list = request.json['where']
            if len(where_clause_list) == 0:
                return
        else:
            request.json['where'] = []

        name_where_clause = []
        for where_clause in where_clause_list:
            for and_query in where_clause:
                if and_query['name'] == name:
                    name_where_clause.append((where_clause, and_query))
        new_clause_list = []
        if prefix:
            obj_list = [prefix + obj for obj in obj_list]
        for obj_value in obj_list:
            new_clause =  [{
                'name'  : name,
                'value' : obj_value,
                'op'    : OpServerUtils.MatchOp.EQUAL
            }]
            if len(name_where_clause) == 0:
                new_clause_list.append(new_clause)
            else:
                for (clause, and_query) in name_where_clause:
                    if and_query["name"] == name and and_query["op"] == \
                            OpServerUtils.MatchOp.PREFIX:
                        if obj_value.startswith(and_query["value"]):
                            if new_clause not in new_clause_list:
                                new_clause_list.append(new_clause)
                            if and_query in clause:
                                clause.remove(and_query)
                    elif and_query["name"] == name and and_query["op"] == \
                            OpServerUtils.MatchOp.EQUAL:
                        if and_query["value"] not in obj_list:
                            raise bottle.HTTPResponse(status=401,
                                    body='Authentication required',
                                    headers=self._reject_auth_headers())
                        if obj_value == and_query["value"]:
                            if new_clause not in new_clause_list:
                                new_clause_list.append(new_clause)
                            if and_query in clause:
                                clause.remove(and_query)
        if len(where_clause_list) == 0:
            request.json['where'] = new_clause_list
        elif len(new_clause_list) > 0:
            final_clause_list = []
            for existing_clause in where_clause_list:
                for new_clause in new_clause_list:
                    if existing_clause == []:
                        final_clause_list.append([new_clause[0]])
                    elif ([existing_clause[0], new_clause[0]]) not in \
                            final_clause_list:
                        final_clause_list.append([existing_clause[0], new_clause[0]])
            request.json['where'] = final_clause_list
    #end update_where_clause

    def _reject_auth_headers(self):
        header_val = 'Keystone uri=\'%s\'' % \
            self._args.auth_conf_info.get('auth_uri')
        return { "WWW-Authenticate" : header_val }

    def __init__(self, args_str=' '.join(sys.argv[1:])):
        self.gevs = []
        self._args = None
        self._usecache = True
        self._parse_args(args_str)
        print args_str
 
        self._homepage_links = []
        self._homepage_links.append(
            LinkObject('documentation', '/documentation/index.html'))
        self._homepage_links.append(
            LinkObject('Message documentation', '/documentation/messages/index.html'))
        self._homepage_links.append(LinkObject('analytics', '/analytics'))

        super(OpServer, self).__init__()
        self._webserver = None
        module = Module.OPSERVER
        self._moduleid = ModuleNames[module]
        node_type = Module2NodeType[module]
        self._node_type_name = NodeTypeNames[node_type]
        if self._args.worker_id:
            self._instance_id = self._args.worker_id
        else:
            self._instance_id = INSTANCE_ID_DEFAULT
        self.table = "ObjectCollectorInfo"
        self._hostname = socket.gethostname()
        if self._args.dup:
            self._hostname += 'dup'
        self._sandesh = Sandesh()
        self.disk_usage_percentage = 0
        self.pending_compaction_tasks = 0
        opserver_sandesh_req_impl = OpserverSandeshReqImpl(self)
        self.random_collectors = self._args.collectors
        if self._args.collectors:
            self._chksum = hashlib.md5("".join(self._args.collectors)).hexdigest()
            self.random_collectors = random.sample(self._args.collectors, \
                                                   len(self._args.collectors))
        self._api_server_checksum = ''
        if self._args.api_server:
            self._api_server_checksum = hashlib.md5(''.join(
                self._args.api_server)).hexdigest()
            self._args.api_server = random.sample(self._args.api_server,
                len(self._args.api_server))

        self._sandesh.init_generator(
            self._moduleid, self._hostname, self._node_type_name,
            self._instance_id, self.random_collectors, 'opserver_context',
            int(self._args.http_server_port), ['opserver.sandesh'],
            logger_class=self._args.logger_class,
            logger_config_file=self._args.logging_conf,
            config=self._args.sandesh_config)
        self._sandesh.set_logging_params(
            enable_local_log=self._args.log_local,
            category=self._args.log_category,
            level=self._args.log_level,
            file=self._args.log_file,
            enable_syslog=self._args.use_syslog,
            syslog_facility=self._args.syslog_facility)
        ConnectionState.init(self._sandesh, self._hostname, self._moduleid,
            self._instance_id,
            staticmethod(ConnectionState.get_conn_state_cb),
            NodeStatusUVE, NodeStatus, self.table)
        self._uvepartitions_state = None
        # Trace buffer list
        self.trace_buf = [
            {'name':'DiscoveryMsg', 'size':1000}
        ]
        # Create trace buffers 
        for buf in self.trace_buf:
            self._sandesh.trace_buffer_create(name=buf['name'], size=buf['size'])

        self._logger = self._sandesh._logger
        self._get_common = self._http_get_common
        self._put_common = self._http_put_common
        self._delete_common = self._http_delete_common
        self._post_common = self._http_post_common

        self._collector_pool = None
        self._state_server = OpStateServer(self._logger, self._args.redis_password)

        body = gevent.queue.Queue()

        self._vnc_api_client = None
        self._vnc_api_client_connect = None
        if self._args.auth_conf_info.get('aaa_auth_enabled'):
            self._vnc_api_client = VncCfgApiClient(self._args.auth_conf_info,
                self._sandesh, self._logger)
        self._uvedbstream = UveStreamer(self._logger, None, None,
                self.get_agp, self._args.redis_password)

        # On olders version of linux, kafka cannot be
        # relied upon. DO NOT use it to serve UVEs
        self._usecache = True
        (PLATFORM, VERSION, EXTRA) = platform.linux_distribution()
        if PLATFORM.lower() == 'ubuntu':
            if VERSION.find('12.') == 0:
                self._usecache = False
        if PLATFORM.lower() == 'centos':
            if VERSION.find('6.') == 0:
                self._usecache = False
        if self._args.partitions == 0:
            self._usecache = False

        if not self._usecache:
            self._logger.error("NOT using UVE Cache")
        else:
            self._logger.error("Initializing UVE Cache")

        self._LEVEL_LIST = []
        for k in SandeshLevel._VALUES_TO_NAMES:
            if (k < SandeshLevel.UT_START):
                d = {}
                d[k] = SandeshLevel._VALUES_TO_NAMES[k]
                self._LEVEL_LIST.append(d)
        self._CATEGORY_MAP =\
            dict((ModuleNames[k], [CategoryNames[ce] for ce in v])
                 for k, v in ModuleCategoryMap.iteritems())

        self.agp = {}
        if self._usecache:
            ConnectionState.update(conn_type = ConnectionType.UVEPARTITIONS,
                name = 'UVE-Aggregation', status = ConnectionStatus.INIT)
            self._uvepartitions_state = ConnectionStatus.INIT
        else:
            ConnectionState.update(conn_type = ConnectionType.UVEPARTITIONS,
                name = 'UVE-Aggregation', status = ConnectionStatus.UP)
            self._uvepartitions_state = ConnectionStatus.UP

        self.redis_uve_list = []
        if type(self._args.redis_uve_list) is str:
            self._args.redis_uve_list = self._args.redis_uve_list.split()
        ad_freq = 10
        us_freq = 5
        is_local = True 
        for redis_uve in self._args.redis_uve_list:
            redis_ip_port = redis_uve.split(':')
            if redis_ip_port[0] != "127.0.0.1":
                is_local = False
            redis_elem = (redis_ip_port[0], int(redis_ip_port[1]))
            self.redis_uve_list.append(redis_elem)
        if is_local:
            ad_freq = 2
            us_freq = 2 

        if self._args.zk_list:
            self._ad = AnalyticsDiscovery(self._logger,
                ','.join(self._args.zk_list),
                ANALYTICS_API_SERVER_DISCOVERY_SERVICE_NAME,
                self._hostname + "-" + self._instance_id,
                {ALARM_GENERATOR_SERVICE_NAME:self.disc_agp},
                self._args.zk_prefix,
                ad_freq)
        else:
            self._ad = None
            if self._args.partitions != 0:
                # Assume all partitions are on the 1st redis server
                # and there is only one redis server
                redis_ip_port = self._args.redis_uve_list[0].split(':')
                assert(len(self._args.redis_uve_list) == 1)
                for part in range(0,self._args.partitions):
                    pi = PartInfo(ip_address = redis_ip_port[0],
                                  acq_time = UTCTimestampUsec(),
                                  instance_id = "0",
                                  port = int(redis_ip_port[1]))
                    self.agp[part] = pi


        self._uve_server = UVEServer(self.redis_uve_list,
                                 self._logger,
                                 self._args.redis_password,
                                 self._uvedbstream, self._usecache,
                                 freq = us_freq)
        self._state_server.update_redis_list(self.redis_uve_list)
        self._uve_server.set_ad_handle(self._ad)

        self._analytics_links = ['uves', 'uve-types', 'tables',
            'queries', 'alarms', 'uve-stream', 'alarm-stream']

        self._VIRTUAL_TABLES = copy.deepcopy(_TABLES)

        listmgrs = extension.ExtensionManager('contrail.analytics.alarms')
        for elem in listmgrs:
            self._logger.info('Loaded extensions for %s: %s doc %s' % \
                (elem.name , elem.entry_point, elem.plugin.__doc__))

        self.UveTypeToConfigObjectType = {}
        self.ObjectLogTypeToConfigObjectType = {}
        self.ObjectLogTypeToUveType = {}
        for t in _OBJECT_TABLES:
            obj = query_table(
                name=t, display_name=_OBJECT_TABLES[t].objtable_display_name,
                schema=_OBJECT_TABLE_SCHEMA,
                columnvalues=_OBJECT_TABLE_COLUMN_VALUES)
            self._VIRTUAL_TABLES.append(obj)
            if _OBJECT_TABLES[t].config_object_type:
                self.UveTypeToConfigObjectType[_OBJECT_TABLES[t].\
                        log_query_name] = _OBJECT_TABLES[t].config_object_type
                self.ObjectLogTypeToConfigObjectType[t] = \
                        _OBJECT_TABLES[t].config_object_type
                self.ObjectLogTypeToUveType[t] = \
                        _OBJECT_TABLES[t].log_query_name

        stat_tables = []
        # read the stat table schemas from vizd first
        for t in _STAT_TABLES:
            attributes = []
            for attr in t.attributes:
                suffixes = []
                if attr.suffixes:
                    for suffix in attr.suffixes:
                        suffixes.append(suffix)
                attributes.append({"name":attr.name,"datatype":attr.datatype,"index":attr.index,"suffixes":suffixes})
            new_table = {"stat_type":t.stat_type,
                         "stat_attr":t.stat_attr,
                         "display_name":t.display_name,
                         "obj_table":t.obj_table,
                         "attributes":attributes}
            stat_tables.append(new_table)

        # read all the _stats_tables.json files for remaining stat table schema
        topdir = os.path.dirname(__file__) + "/stats_schema/"
        extn = '_stats_tables.json'
        stat_schema_files = []
        for dirpath, dirnames, files in os.walk(topdir):
            for name in files:
                if name.lower().endswith(extn):
                    stat_schema_files.append(os.path.join(dirpath, name))
        for schema_file in stat_schema_files:
            with open(schema_file) as data_file:
                data = json.load(data_file)
            for _, tables in data.iteritems():
                for table in tables:
                    if table not in stat_tables:
                        stat_tables.append(table)

        for table in stat_tables:
            stat_id = table["stat_type"] + "." + table["stat_attr"]
            scols = []

            keyln = stat_query_column(name=STAT_SOURCE_FIELD, datatype='string', index=True)
            scols.append(keyln)

            tln = stat_query_column(name=STAT_TIME_FIELD, datatype='int', index=False)
            scols.append(tln)

            tcln = stat_query_column(name="CLASS(" + STAT_TIME_FIELD + ")", 
                     datatype='int', index=False)
            scols.append(tcln)

            teln = stat_query_column(name=STAT_TIMEBIN_FIELD, datatype='int', index=False)
            scols.append(teln)

            tecln = stat_query_column(name="CLASS(" + STAT_TIMEBIN_FIELD+ ")", 
                     datatype='int', index=False)
            scols.append(tecln)

            uln = stat_query_column(name=STAT_UUID_FIELD, datatype='uuid', index=False)
            scols.append(uln)

            cln = stat_query_column(name="COUNT(" + table["stat_attr"] + ")",
                    datatype='int', index=False)
            scols.append(cln)

            isname = False
            for aln in table["attributes"]:
                if aln["name"]==STAT_OBJECTID_FIELD:
                    isname = True
                if "suffixes" in aln.keys():
                    if "uve_type" in aln.keys():
                        aln_col = stat_query_column(name=aln["name"], \
                        datatype=aln["datatype"], index=aln["index"], \
                        suffixes=aln["suffixes"], uve_type=aln["uve_type"])
                    else:
                        aln_col = stat_query_column(name=aln["name"], \
                        datatype=aln["datatype"], index=aln["index"], \
                        suffixes=aln["suffixes"])
                else:
                    if "uve_type" in aln.keys():
                        aln_col = stat_query_column(name=aln["name"], \
                        datatype=aln["datatype"], index=aln["index"], \
                        uve_type=aln["uve_type"])
                    else:
                        aln_col = stat_query_column(name=aln["name"], \
                        datatype=aln["datatype"], index=aln["index"])
                scols.append(aln_col)

                if aln["datatype"] in ['int','double']:
                    sln = stat_query_column(name= "SUM(" + aln["name"] + ")",
                            datatype=aln["datatype"], index=False)
                    scols.append(sln)
                    scln = stat_query_column(name= "CLASS(" + aln["name"] + ")",
                            datatype=aln["datatype"], index=False)
                    scols.append(scln)
                    sln = stat_query_column(name= "MAX(" + aln["name"] + ")",
                            datatype=aln["datatype"], index=False)
                    scols.append(sln)
                    scln = stat_query_column(name= "MIN(" + aln["name"] + ")",
                            datatype=aln["datatype"], index=False)
                    scols.append(scln)
                    scln = stat_query_column(name= "PERCENTILES(" + aln["name"] + ")",
                            datatype='percentiles', index=False)
                    scols.append(scln)
                    scln = stat_query_column(name= "AVG(" + aln["name"] + ")",
                            datatype='avg', index=False)
                    scols.append(scln)
            if not isname: 
                if "obj_table" in table and table["obj_table"] in \
                        self.ObjectLogTypeToUveType:
                    uve_type = self.ObjectLogTypeToUveType[table["obj_table"]]
                    keyln = stat_query_column(name=STAT_OBJECTID_FIELD, \
                            datatype='string', index=True, uve_type=uve_type)
                else:
                    keyln = stat_query_column(name=STAT_OBJECTID_FIELD, \
                            datatype='string', index=True)
                scols.append(keyln)

            sch = query_schema_type(type='STAT', columns=scols)

            stt = query_table(
                name = STAT_VT_PREFIX + "." + stat_id,
                display_name = table["display_name"],
                schema = sch,
                columnvalues = [STAT_OBJECTID_FIELD, SOURCE])
            self._VIRTUAL_TABLES.append(stt)

        self._analytics_db = AnalyticsDb(self._logger,
            self._args.auth_conf_info['admin_port'],
            self._args.auth_conf_info['admin_user'],
            self._args.auth_conf_info['admin_password'])

        # Register introspect request handlers
        UVEDbCacheTablesRequest.handle_request = \
            self.handle_UVEDbCacheTablesRequest
        UVEDbCacheTableKeysRequest.handle_request = \
            self.handle_UVEDbCacheTableKeysRequest
        UVEDbCacheUveRequest.handle_request = self.handle_UVEDbCacheUveRequest

        bottle.route('/', 'GET', self.homepage_http_get)
        bottle.route('/analytics', 'GET', self.analytics_http_get)
        bottle.route('/analytics/uves', 'GET', self.uves_http_get)
        bottle.route('/analytics/uve-types', 'GET', self.uve_types_http_get)
        bottle.route('/analytics/alarms/acknowledge', 'POST',
            self.alarms_ack_http_post)
        bottle.route('/analytics/query', 'POST', self.query_process)
        bottle.route(
            '/analytics/query/<queryId>', 'GET', self.query_status_get)
        bottle.route('/analytics/query/<queryId>/chunk-final/<chunkId>',
                     'GET', self.query_chunk_get)
        bottle.route('/analytics/queries', 'GET', self.show_queries)
        bottle.route('/analytics/tables', 'GET', self.tables_process)
        bottle.route('/analytics/operation/analytics-data-start-time',
                     'GET', self._get_analytics_data_start_time)
        bottle.route('/analytics/table/<table>', 'GET', self.table_process)
        bottle.route('/analytics/table/<table>/schema',
                     'GET', self.table_schema_process)
        bottle.route('/analytics/send-tracebuffer/<source>/<module>/<instance_id>/<name>',
                     'GET', self.send_trace_buffer)
        bottle.route('/doc-style.css', 'GET',
                     self.documentation_messages_css_get)
        bottle.route('/documentation/messages/<module>',
                     'GET', self.documentation_messages_http_get)
        bottle.route('/documentation/messages/<module>/<sfilename>',
                     'GET', self.documentation_messages_http_get)
        bottle.route('/documentation/<filename:path>',
                     'GET', self.documentation_http_get)
        bottle.route('/analytics/uve-stream', 'GET', self.uve_stream)
        bottle.route('/analytics/alarm-stream', 'GET', self.alarm_stream)

        bottle.route('/analytics/uves/<tables>', 'GET', self.dyn_list_http_get)
        bottle.route('/analytics/uves/<table>/<name:path>', 'GET', self.dyn_http_get)
        bottle.route('/analytics/uves/<tables>', 'POST', self.dyn_http_post)
        bottle.route('/analytics/alarms', 'GET', self.alarms_http_get)

    # end __init__

    def _parse_args(self, args_str=' '.join(sys.argv[1:])):
        '''
        Eg. python opserver.py --host_ip 127.0.0.1
                               --redis_query_port 6379
                               --redis_password
                               --collectors 127.0.0.1:8086
                               --http_server_port 8090
                               --rest_api_port 8081
                               --rest_api_ip 0.0.0.0
                               --log_local
                               --log_level SYS_DEBUG
                               --log_category test
                               --log_file <stdout>
                               --use_syslog
                               --syslog_facility LOG_USER
                               --worker_id 0
                               --partitions 15
                               --zk_list 127.0.0.1:2181
                               --redis_uve_list 127.0.0.1:6379
                               --zk_list 127.0.0.1:2181
        '''
        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file", action='append',
                                 help="Specify config file", metavar="FILE",
                                 default=ServicesDefaultConfigurationFiles.get(
                                     SERVICE_OPSERVER, None))
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'host_ip'            : "127.0.0.1",
            'collectors'         : None,
            'http_server_port'   : 8090,
            'rest_api_port'      : 8081,
            'rest_api_ip'        : '0.0.0.0',
            'log_local'          : False,
            'log_level'          : 'SYS_DEBUG',
            'log_category'       : '',
            'log_file'           : Sandesh._DEFAULT_LOG_FILE,
            'use_syslog'         : False,
            'syslog_facility'    : Sandesh._DEFAULT_SYSLOG_FACILITY,
            'dup'                : False,
            'analytics_data_ttl' : 48,
            'analytics_config_audit_ttl' : -1,
            'analytics_statistics_ttl' : -1,
            'analytics_flow_ttl' : -1,
            'logging_conf': '',
            'logger_class': None,
            'partitions'        : 15,
            'zk_list'           : None,
            'zk_prefix'         : '',
            'aaa_mode'          : AAA_MODE_CLOUD_ADMIN,
            'api_server'        : ['127.0.0.1:8082'],
            'admin_port'        : OpServerAdminPort,
            'cloud_admin_role'  : CLOUD_ADMIN_ROLE,
            'api_server_use_ssl': False,
            'use_aggregated_uve_db' : 'true',
        }
        defaults.update(SandeshConfig.get_default_options(['DEFAULTS']))
        redis_opts = {
            'redis_query_port'   : 6379,
            'redis_password'       : None,
            'redis_uve_list'     : ['127.0.0.1:6379'],
        }
        keystone_opts = {
            'auth_host': '127.0.0.1',
            'auth_protocol': 'http',
            'auth_port': 35357,
            'admin_user': 'admin',
            'admin_password': 'contrail123',
            'admin_tenant_name': 'default-domain'
        }
        sandesh_opts = SandeshConfig.get_default_options()

        # read contrail-analytics-api own conf file
        config = None
        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read(args.conf_file)
            if 'DEFAULTS' in config.sections():
                defaults.update(dict(config.items("DEFAULTS")))
            if 'REDIS' in config.sections():
                redis_opts.update(dict(config.items('REDIS')))
            if 'KEYSTONE' in config.sections():
                keystone_opts.update(dict(config.items('KEYSTONE')))
            SandeshConfig.update_options(sandesh_opts, config)

        # Override with CLI options
        # Don't surpress add_help here so it will handle -h

        parser = argparse.ArgumentParser(
            # Inherit options from config_parser
            parents=[conf_parser],
            # print script description with -h/--help
            description=__doc__,
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        defaults.update(redis_opts)
        defaults.update(keystone_opts)
        defaults.update(sandesh_opts)
        parser.set_defaults(**defaults)

        parser.add_argument("--host_ip",
            help="Host IP address")
        parser.add_argument("--redis_server_port",
            type=int,
            help="Redis server port")
        parser.add_argument("--redis_query_port",
            type=int,
            help="Redis query port")
        parser.add_argument("--redis_password",
            help="Redis server password")
        parser.add_argument("--collectors",
            help="List of Collector IP addresses in ip:port format",
            nargs="+")
        parser.add_argument("--http_server_port",
            type=int,
            help="HTTP server port")
        parser.add_argument("--rest_api_port",
            type=int,
            help="REST API port")
        parser.add_argument("--rest_api_ip",
            help="REST API IP address")
        parser.add_argument("--log_local", action="store_true",
            help="Enable local logging of sandesh messages")
        parser.add_argument(
            "--log_level",  
            help="Severity level for local logging of sandesh messages")
        parser.add_argument(
            "--log_category", 
            help="Category filter for local logging of sandesh messages")
        parser.add_argument("--log_file",
            help="Filename for the logs to be written to")
        parser.add_argument("--use_syslog",
            action="store_true",
            help="Use syslog for logging")
        parser.add_argument("--syslog_facility",
            help="Syslog facility to receive log lines")
        parser.add_argument("--dup", action="store_true",
            help="Internal use")
        parser.add_argument("--redis_uve_list",
            help="List of redis-uve in ip:port format. For internal use only",
            nargs="+")
        parser.add_argument(
            "--worker_id",
            help="Worker Id")
        parser.add_argument(
            "--logging_conf",
            help=("Optional logging configuration file, default: None"))
        parser.add_argument(
            "--logger_class",
            help=("Optional external logger class, default: None"))
        parser.add_argument("--partitions", type=int,
            help="Number of partitions for hashing UVE keys")
        parser.add_argument("--zk_list",
            help="List of zookeepers in ip:port format",
            nargs="+")
        parser.add_argument("--zk_prefix",
            help="System Prefix for zookeeper")
        parser.add_argument("--cloud_admin_role",
            help="Name of cloud-admin role")
        parser.add_argument("--aaa_mode", choices=APIAAAModes,
            help="AAA mode")
        parser.add_argument("--auth_host",
            help="IP address of keystone server")
        parser.add_argument("--auth_protocol",
            help="Keystone authentication protocol")
        parser.add_argument("--auth_port", type=int,
            help="Keystone server port")
        parser.add_argument("--admin_user",
            help="Name of keystone admin user")
        parser.add_argument("--admin_password",
            help="Password of keystone admin user")
        parser.add_argument("--admin_tenant_name",
            help="Tenant name for keystone admin user")
        parser.add_argument("--api_server",
            help="List of api-servers in ip:port format separated by space",
            nargs="+")
        parser.add_argument("--admin_port",
            help="Port with local auth for admin access")
        parser.add_argument("--api_server_use_ssl",
            help="Use SSL to connect with API server")
        parser.add_argument("--use_aggregated_uve_db",
            help="enable/disbale read uve from aggregated uve db")
        SandeshConfig.add_parser_arguments(parser)
        self._args = parser.parse_args(remaining_argv)
        if type(self._args.collectors) is str:
            self._args.collectors = self._args.collectors.split()
        if type(self._args.redis_uve_list) is str:
            self._args.redis_uve_list = self._args.redis_uve_list.split()
        if type(self._args.zk_list) is str:
            self._args.zk_list= self._args.zk_list.split()
        if type(self._args.api_server) is str:
            self._args.api_server = self._args.api_server.split()

        auth_conf_info = {}
        auth_conf_info['admin_user'] = self._args.admin_user
        auth_conf_info['admin_password'] = self._args.admin_password
        auth_conf_info['admin_tenant_name'] = self._args.admin_tenant_name
        auth_conf_info['auth_protocol'] = self._args.auth_protocol
        auth_conf_info['auth_host'] = self._args.auth_host
        auth_conf_info['auth_port'] = self._args.auth_port
        auth_conf_info['auth_uri'] = '%s://%s:%d' % (self._args.auth_protocol,
            self._args.auth_host, self._args.auth_port)
        auth_conf_info['api_server_use_ssl'] = self._args.api_server_use_ssl
        auth_conf_info['aaa_auth_enabled'] = \
            False if self._args.aaa_mode == AAA_MODE_NO_AUTH else True
        auth_conf_info['cloud_admin_role'] = self._args.cloud_admin_role
        auth_conf_info['admin_port'] = self._args.admin_port
        auth_conf_info['api_servers'] = self._args.api_server
        self._args.auth_conf_info = auth_conf_info
        if self._args.use_aggregated_uve_db.lower() == 'false' or\
           self._args.use_aggregated_uve_db == '0':
            self._usecache = False
        self._args.conf_file = args.conf_file
        self._args.sandesh_config = \
            SandeshConfig.from_parser_arguments(self._args)
    # end _parse_args

    def get_args(self):
        return self._args
    # end get_args

    def get_http_server_port(self):
        return int(self._args.http_server_port)
    # end get_http_server_port

    def get_uve_server(self):
        return self._uve_server
    # end get_uve_server

    def homepage_http_get(self):
        json_body = {}
        json_links = []

        base_url = bottle.request.urlparts.scheme + \
            '://' + bottle.request.urlparts.netloc

        for link in self._homepage_links:
            json_links.append(
                {'link': obj_to_dict(
                    LinkObject(link.name, base_url + link.href))})

        json_body = \
            {"href": base_url,
             "links": json_links
             }

        return json_body
    # end homepage_http_get

    def cleanup_uve_streamer(self, gv):
        self.gevs.remove(gv)

    def _set_non_admin_tablefilt(self, filters):
        """
        set tablefilt for non-admin users
        """
        if not self.is_role_cloud_admin():
            user_token_info = self.get_user_token_info()
            config_types = self.ObjectLogTypeToConfigObjectType.keys()
            if filters['tablefilt']:
                for filtr in filters['tablefilt']:
                    if not filtr in config_types:
                        raise bottle.HTTPResponse(status = 403,
                            body = 'Operation Forbidden',
                            headers = self._reject_auth_headers())
            else:
                filters['tablefilt'] = config_types
        else:
            user_token_info = None
        return filters, user_token_info
    # end _set_non_admin_tablefilt

    def _serve_streams(self, alarmsonly):
        req = bottle.request.query
        try:
            filters = OpServer._uve_filter_set(req)
        except Exception as e:
            return bottle.HTTPError(_ERRORS[errno.EBADMSG], e)

        if alarmsonly:
            filters['cfilt'] = {'UVEAlarms':set()}

        kfilter = filters.get('kfilt')
        patterns = None
        if kfilter is not None:
            patterns = set()
            for filt in kfilter:
                patterns.add(self._uve_server.get_uve_regex(filt))

        filters, token = self._set_non_admin_tablefilt(filters)
        bottle.response.set_header('Content-Type', 'text/event-stream')
        bottle.response.set_header('Cache-Control', 'no-cache')
        # This is needed to detect when the client hangs up
        rfile = bottle.request.environ['wsgi.input'].rfile

        body = gevent.queue.Queue()
        ph = UveStreamer(self._logger, body, rfile, self.get_agp,
            self._args.redis_password,
            filters['tablefilt'], filters['cfilt'], patterns, token=token)
        ph.set_cleanup_callback(self.cleanup_uve_streamer)
        self.gevs.append(ph)
        ph.start()
        return body

    @validate_user_token(only_cloud_admin=False, get_token_info=True)
    def uve_stream(self):
        return self._serve_streams(False)

    @validate_user_token(only_cloud_admin=False, get_token_info=True)
    def alarm_stream(self):
        return self._serve_streams(True)

    def documentation_http_get(self, filename):
        return bottle.static_file(
            filename, root='/usr/share/doc/contrail-analytics-api/html')
    # end documentation_http_get

    def documentation_messages_http_get(self, module, sfilename=None):
        filename = module
        if sfilename:
            filename = module + '/' + sfilename
        return bottle.static_file(
            filename, root='/usr/share/doc/contrail-docs/html/messages')
    # end documentation_messages_http_get

    def documentation_messages_css_get(self):
        return bottle.static_file('/doc-style.css',
            root='/usr/share/doc/contrail-docs/html/messages')
    # end documentation_messages_css_get

    def _http_get_common(self, request):
        return (True, '')
    # end _http_get_common

    def _http_put_common(self, request, obj_dict):
        return (True, '')
    # end _http_put_common

    def _http_delete_common(self, request, id):
        return (True, '')
    # end _http_delete_common

    def _http_post_common(self, request, obj_dict):
        return (True, '')
    # end _http_post_common

    @staticmethod
    def _get_redis_query_ip_from_qid(qid):
        try:
            ip = qid.rsplit('-', 1)[1]
            redis_ip = socket.inet_ntop(socket.AF_INET, 
                            struct.pack('>I', int(ip, 16)))
        except Exception as err:
            return None
        return redis_ip
    # end _get_redis_query_ip_from_qid

    def _query_status(self, request, qid):
        resp = {}
        redis_query_ip = OpServer._get_redis_query_ip_from_qid(qid)
        if redis_query_ip is None:
            return bottle.HTTPError(_ERRORS[errno.EINVAL], 
                    'Invalid query id')
        try:
            resp = redis_query_status(host=redis_query_ip,
                                      port=int(self._args.redis_query_port),
                                      redis_password=self._args.redis_password,
                                      qid=qid)
        except redis.exceptions.ConnectionError:
            return bottle.HTTPError(_ERRORS[errno.EIO],
                    'Failure in connection to the query DB')
        except Exception as e:
            self._logger.error("Exception: %s" % e)
            return bottle.HTTPError(_ERRORS[errno.EIO], 'Error: %s' % e)
        else:
            if resp is None:
                return bottle.HTTPError(_ERRORS[errno.ENOENT], 
                    'Invalid query id or Abandoned query id')
            resp_header = {'Content-Type': 'application/json'}
            resp_code = 200
            self._logger.debug("query [%s] status: %s" % (qid, resp))
            return bottle.HTTPResponse(
                json.dumps(resp), resp_code, resp_header)
    # end _query_status

    def _query_chunk(self, request, qid, chunk_id):
        redis_query_ip = OpServer._get_redis_query_ip_from_qid(qid)
        if redis_query_ip is None:
            yield bottle.HTTPError(_ERRORS[errno.EINVAL],
                    'Invalid query id')
        try:
            done = False
            gen = redis_query_chunk(host=redis_query_ip,
                                    port=int(self._args.redis_query_port),
                                    redis_password=self._args.redis_password,
                                    qid=qid, chunk_id=chunk_id)
            bottle.response.set_header('Content-Type', 'application/json')
            while not done:
                try:
                    yield gen.next()
                except StopIteration:
                    done = True
        except redis.exceptions.ConnectionError:
            yield bottle.HTTPError(_ERRORS[errno.EIO],
                    'Failure in connection to the query DB')
        except Exception as e:
            self._logger.error("Exception: %s" % str(e))
            yield bottle.HTTPError(_ERRORS[errno.ENOENT], 'Error: %s' % e)
        else:
            self._logger.info(
                "Query [%s] chunk #%d read at time %d"
                % (qid, chunk_id, time.time()))
    # end _query_chunk

    def _is_valid_stats_table_query(self, request, tabn):
        isT_ = False
        isT = False
        for key, value in request.iteritems():
            if key == "select_fields":
                for select_field in value:
                    if select_field == STAT_TIME_FIELD:
                        isT = True
                    elif select_field.find(STAT_TIMEBIN_FIELD) == 0:
                        isT_ = True
                    else:
                        agg_field = select_field.split('(')
                        if len(agg_field) == 2:
                            oper = agg_field[0]
                            field = agg_field[1].split(')')[0]
                            if oper != "COUNT":
                                if field == STAT_TIME_FIELD:
                                    isT = True
                                elif field == STAT_TIMEBIN_FIELD:
                                    isT_ = True
                                else:
                                    field_found = False
                                    for column in self._VIRTUAL_TABLES[tabn].schema.columns:
                                        if column.name == field:
                                            if column.datatype != "":
                                                field_found = True
                                    if field_found == False:
                                        reply = bottle.HTTPError(_ERRORS[errno.EINVAL], \
                                                            'Unknown field %s' %field)
                                        return reply
                            elif field.split('.')[-1] != \
                                      self._VIRTUAL_TABLES[tabn].name.split('.')[-1]:
                                reply = bottle.HTTPError(_ERRORS[errno.EINVAL], \
                                            'Invalid COUNT field %s' %field)
                                return reply
                        elif len(agg_field) == 1:
                            field_found = False
                            for column in self._VIRTUAL_TABLES[tabn].schema.columns:
                                if column.name == select_field:
                                    if column.datatype != "":
                                        field_found = True
                            if field_found == False:
                                reply = bottle.HTTPError(_ERRORS[errno.EINVAL], \
                                            'Invalid field %s' %select_field)
                                return reply

                    if isT and isT_:
                        reply = bottle.HTTPError(_ERRORS[errno.EINVAL], \
                                    "Stats query cannot have both T and T=")
                        return reply
        return None
    # end _is_valid_stats_table_query

    def _query(self, request):
        reply = {}
        try:
            redis_query_ip, = struct.unpack('>I', socket.inet_pton(
                                        socket.AF_INET, self._args.host_ip))
            qid = str(uuid.uuid1(redis_query_ip))
            self._logger.info('Received Query: %s' % (str(request.json)))
            self._logger.info("Starting Query %s" % qid)

            tabl = ""
            for key, value in request.json.iteritems():
                if key == "table":
                    tabl = value

            self._logger.info("Table is " + tabl)

            tabn = None
            for i in range(0, len(self._VIRTUAL_TABLES)):
                if self._VIRTUAL_TABLES[i].name == tabl:
                    tabn = i
                    break

            if (tabn is not None) and (tabl.find("StatTable") == 0):
                query_err = self._is_valid_stats_table_query(request.json, tabn)
                if query_err is not None:
                    yield query_err
                    return

            if (tabn is not None):
                tabtypes = {}
                for cols in self._VIRTUAL_TABLES[tabn].schema.columns:
                    if cols.datatype in ['long', 'int']:
                        tabtypes[cols.name] = 'int'
                    elif cols.datatype in ['ipv4']:
                        tabtypes[cols.name] = 'ipv4'
                    else:
                        tabtypes[cols.name] = 'string'

                self._logger.info(str(tabtypes))

            if (tabn is None):
                if not tabl.startswith("StatTable."):
                    tables = self._uve_server.get_tables()
                    if not tabl in tables:
                        reply = bottle.HTTPError(_ERRORS[errno.ENOENT],
                                'Table %s not found' % tabl)
                        yield reply
                        return
                else:
                    self._logger.info("Schema not known for dynamic table %s" % tabl)

            if tabl == OVERLAY_TO_UNDERLAY_FLOW_MAP:
                overlay_to_underlay_map = OverlayToUnderlayMapper(
                    request.json, 'localhost',
                    self._args.auth_conf_info['admin_port'],
                    self._args.auth_conf_info['admin_user'],
                    self._args.auth_conf_info['admin_password'], self._logger)
                try:
                    yield overlay_to_underlay_map.process_query()
                except OverlayToUnderlayMapperError as e:
                    yield bottle.HTTPError(_ERRORS[errno.EIO], str(e))
                return

            self.check_perms_and_update_where_clause(request, tabl, tabn)

            prg = redis_query_start('127.0.0.1',
                                    int(self._args.redis_query_port),
                                    self._args.redis_password,
                                    qid, request.json,
                                    self._VIRTUAL_TABLES[tabn].schema.columns
                                    if tabn else None)
            if prg is None:
                self._logger.error('QE Not Responding')
                yield bottle.HTTPError(_ERRORS[errno.EBUSY], 
                        'Query Engine is not responding')
                return

        except redis.exceptions.ConnectionError:
            yield bottle.HTTPError(_ERRORS[errno.EIO],
                    'Failure in connection to the query DB')
        except bottle.HTTPResponse:
            raise
        except Exception as e:
            self._logger.error("Exception: %s" % str(e))
            yield bottle.HTTPError(_ERRORS[errno.EIO],
                    'Error: %s' % e)
        else:
            redish = None
            if prg < 0:
                cod = -prg
                self._logger.error(
                    "Query Failed. Found Error %s" % errno.errorcode[cod])
                reply = bottle.HTTPError(_ERRORS[cod], errno.errorcode[cod])
                yield reply
            else:
                self._logger.info(
                    "Query Accepted at time %d , Progress %d"
                    % (time.time(), prg))
                # In Async mode, we should return with "202 Accepted" here
                # and also give back the status URI "/analytic/query/<qid>"
                # OpServers's client will poll the status URI
                if request.get_header('Expect') == '202-accepted' or\
                   request.get_header('Postman-Expect') == '202-accepted':
                    href = '/analytics/query/%s' % (qid)
                    resp_data = json.dumps({'href': href})
                    yield bottle.HTTPResponse(
                        resp_data, 202, {'Content-type': 'application/json'})
                else:
                    for gen in self._sync_query(request, qid):
                        yield gen
    # end _query

    def _sync_query(self, request, qid):
        # In Sync mode, Keep polling query status until final result is
        # available
        try:
            self._logger.info("Polling %s for query result" % ("REPLY:" + qid))
            prg = 0
            done = False
            while not done:
                gevent.sleep(1)
                resp = redis_query_status(host='127.0.0.1',
                                          port=int(
                                              self._args.redis_query_port),
                                          redis_password=self._args.redis_password,
                                          qid=qid)

                # We want to print progress only if it has changed
                if int(resp["progress"]) == prg:
                    continue

                self._logger.info(
                    "Query Progress is %s time %d" % (str(resp), time.time()))
                prg = int(resp["progress"])

                # Either there was an error, or the query is complete
                if (prg < 0) or (prg == 100):
                    done = True

            if prg < 0:
                cod = -prg
                self._logger.error("Query %s - Found Error %s" % \
                    (qid, errno.errorcode[cod]))
                reply = bottle.HTTPError(_ERRORS[cod], errno.errorcode[cod])
                yield reply
                return

            # In Sync mode, its time to read the final result. Status is in
            # "resp"
            done = False
            gen = redis_query_result(host='127.0.0.1',
                                     port=int(self._args.redis_query_port),
                                     redis_password=self._args.redis_password,
                                     qid=qid)
            bottle.response.set_header('Content-Type', 'application/json')
            while not done:
                try:
                    yield gen.next()
                except StopIteration:
                    done = True
            '''
            final_res = {}
            prg, final_res['value'] =\
                redis_query_result_dict(host=self._args.redis_server_ip,
                                        port=int(self._args.redis_query_port),
                                        qid=qid)
            yield json.dumps(final_res)
            '''

        except redis.exceptions.ConnectionError:
            yield bottle.HTTPError(_ERRORS[errno.EIO],
                    'Failure in connection to the query DB')
        except Exception as e:
            self._logger.error("Exception: %s" % str(e))
            yield bottle.HTTPError(_ERRORS[errno.EIO], 
                    'Error: %s' % e)
        else:
            self._logger.info(
                "Query Result available at time %d" % time.time())
        return
    # end _sync_query

    @validate_user_token(only_cloud_admin=False)
    def query_process(self):
        self._post_common(bottle.request, None)
        result = self._query(bottle.request)
        return result
    # end query_process

    @validate_user_token(only_cloud_admin=False)
    def query_status_get(self, queryId):
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            bottle.abort(code, msg)
        return self._query_status(bottle.request, queryId)
    # end query_status_get

    @validate_user_token(only_cloud_admin=False)
    def query_chunk_get(self, queryId, chunkId):
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            bottle.abort(code, msg)
        return self._query_chunk(bottle.request, queryId, int(chunkId))
    # end query_chunk_get

    @validate_user_token
    def show_queries(self):
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            bottle.abort(code, msg)
        queries = {}
        try:
            redish = StrictRedisWrapper(db=0, host='127.0.0.1',
                                       port=int(self._args.redis_query_port),
                                       password=self._args.redis_password)
            pending_queries = redish.lrange('QUERYQ', 0, -1)
            pending_queries_info = []
            for query_id in pending_queries:
                query_data = redis_query_info(redish, query_id)
                pending_queries_info.append(query_data)
            queries['pending_queries'] = pending_queries_info

            processing_queries = redish.lrange(
                'ENGINE:' + socket.gethostname(), 0, -1)
            processing_queries_info = []
            abandoned_queries_info = []
            error_queries_info = []
            for query_id in processing_queries:
                status = redis_query_status(host='127.0.0.1',
                                            port=int(
                                                self._args.redis_query_port),
                                            redis_password=self._args.redis_password,
                                            qid=query_id)
                query_data = redis_query_info(redish, query_id)
                if status is None:
                     abandoned_queries_info.append(query_data)
                elif status['progress'] < 0:
                     query_data['error_code'] = status['progress']
                     error_queries_info.append(query_data)
                else:
                     query_data['progress'] = status['progress']
                     processing_queries_info.append(query_data)
            queries['queries_being_processed'] = processing_queries_info
            queries['abandoned_queries'] = abandoned_queries_info
            queries['error_queries'] = error_queries_info
        except redis.exceptions.ConnectionError:
            return bottle.HTTPError(_ERRORS[errno.EIO],
                    'Failure in connection to the query DB')
        except Exception as err:
            self._logger.error("Exception in show queries: %s" % str(err))
            return bottle.HTTPError(_ERRORS[errno.EIO], 'Error: %s' % err)
        else:
            bottle.response.set_header('Content-Type', 'application/json')
            return json.dumps(queries)
    # end show_queries

    @staticmethod
    def _get_tfilter(cfilt):
        tfilter = {}
        for tfilt in cfilt:
            afilt = tfilt.split(':')
            try:
                attr_list = tfilter[afilt[0]]
            except KeyError:
                tfilter[afilt[0]] = set()
                attr_list = tfilter[afilt[0]]
            finally:
                if len(afilt) > 1:
                    attr_list.add(afilt[1])
                    tfilter[afilt[0]] = attr_list
        return tfilter
    # end _get_tfilter

    @staticmethod
    def _uve_filter_set(req):
        filters = {}
        filters['sfilt'] = req.get('sfilt')
        filters['mfilt'] = req.get('mfilt')

        if req.get('tablefilt'):
            infos = req['tablefilt'].split(',')
            filters['tablefilt'] = []
            for tf in infos:
                if tf and tf in UVE_MAP:
                    filters['tablefilt'].append(UVE_MAP[tf])
                else:
                    filters['tablefilt'].append(tf)
        else:
            filters['tablefilt'] = None
        if req.get('cfilt'):
            infos = req['cfilt'].split(',')
            filters['cfilt'] = OpServer._get_tfilter(infos)
        else:
            filters['cfilt'] = None
        if req.get('kfilt'):
            filters['kfilt'] = req['kfilt'].split(',')
        else:
            filters['kfilt'] = None
        filters['ackfilt'] = req.get('ackfilt')
        if filters['ackfilt'] is not None:
            if filters['ackfilt'] != 'true' and filters['ackfilt'] != 'false':
                raise ValueError('Invalid ackfilt. ackfilt must be true|false')
        return filters
    # end _uve_filter_set

    @staticmethod
    def _uve_http_post_filter_set(req):
        filters = {}
        try:
            filters['kfilt'] = req['kfilt']
            if not isinstance(filters['kfilt'], list):
                raise ValueError('Invalid kfilt')
        except KeyError:
            filters['kfilt'] = ['*']
        filters['sfilt'] = req.get('sfilt')
        filters['mfilt'] = req.get('mfilt')
        try:
            cfilt = req['cfilt']
            if not isinstance(cfilt, list):
                raise ValueError('Invalid cfilt')
        except KeyError:
            filters['cfilt'] = None
        else:
            filters['cfilt'] = OpServer._get_tfilter(cfilt)
        try:
            ackfilt = req['ackfilt']
        except KeyError:
            filters['ackfilt'] = None
        else:
            if not isinstance(ackfilt, bool):
                raise ValueError('Invalid ackfilt. ackfilt must be bool')
            filters['ackfilt'] = 'true' if ackfilt else 'false'
        return filters
    # end _uve_http_post_filter_set

    @validate_user_token(only_cloud_admin=False)
    def dyn_http_post(self, tables):
        (ok, result) = self._post_common(bottle.request, None)
        base_url = bottle.request.urlparts.scheme + \
            '://' + bottle.request.urlparts.netloc
        if not ok:
            (code, msg) = result
            bottle.abort(code, msg)
        uve_type = tables
        uve_tbl = uve_type
        if uve_type in UVE_MAP:
            uve_tbl = UVE_MAP[uve_type]

        user_resources = self.get_resource_list_from_uve_type(uve_type)

        try:
            req = bottle.request.json
            filters = OpServer._uve_http_post_filter_set(req)
        except Exception as err:
            yield bottle.HTTPError(_ERRORS[errno.EBADMSG], err)
        else:
            stats = AnalyticsApiStatistics(self._sandesh, uve_type)
            bottle.response.set_header('Content-Type', 'application/json')
            yield u'{"value": ['
            first = True
            num = 0
            byt = 0
            for key in filters['kfilt']:
                if key.find('*') != -1:
                    for gen in self._uve_server.multi_uve_get(uve_tbl, True,
                                                              filters,
                                                              base_url):
                        if user_resources is not None  and 'name' in gen:
                            if not gen['name'] in user_resources:
                                continue
                        dp = json.dumps(gen)
                        byt += len(dp)
                        if first:
                            yield u'' + dp
                            first = False
                        else:
                            yield u', ' + dp
                        num += 1
                    stats.collect(num,byt)
                    stats.sendwith()
                    yield u']}'
                    return
            first = True
            for key in filters['kfilt']:
                uve_name = uve_tbl + ':' + key
                _, rsp = self._uve_server.get_uve(uve_name, True, filters,
                                               base_url=base_url)
                num += 1
                if rsp != {}:
                    if user_resources is not None:
                        if not key in user_resources:
                            continue
                    data = {'name': key, 'value': rsp}
                    dp = json.dumps(data)
                    byt += len(dp)
                    if first:
                        yield u'' + dp
                        first = False
                    else:
                        yield u', ' + dp
            stats.collect(num,byt)
            stats.sendwith()
            yield u']}'
    # end dyn_http_post

    @validate_user_token(only_cloud_admin=False)
    def dyn_http_get(self, table, name):
        # common handling for all resource get
        (ok, result) = self._get_common(bottle.request)
        base_url = bottle.request.urlparts.scheme + \
            '://' + bottle.request.urlparts.netloc
        if not ok:
            (code, msg) = result
            bottle.abort(code, msg)
        uve_tbl = table 
        if table in UVE_MAP:
            uve_tbl = UVE_MAP[table]

        bottle.response.set_header('Content-Type', 'application/json')
        uve_name = uve_tbl + ':' + name
        req = bottle.request.query
        try:
            filters = OpServer._uve_filter_set(req)
        except Exception as e:
            yield bottle.HTTPError(_ERRORS[errno.EBADMSG], e)
        flat = False
        if 'flat' in req.keys() or any(filters.values()):
            flat = True

        stats = AnalyticsApiStatistics(self._sandesh, table)

        uve_name = uve_tbl + ':' + name
        user_resources = self.get_resource_list_from_uve_type(table)
        self._logger.debug("resource list for %s is %s" %(table, user_resources))
        if name.find('*') != -1:
            flat = True
            yield u'{"value": ['
            first = True
            if filters['kfilt'] is None:
                filters['kfilt'] = [name]
            num = 0
            byt = 0
            for gen in self._uve_server.multi_uve_get(uve_tbl, flat,
                    filters, base_url):
                if user_resources is not None and 'name' in gen:
                    if not gen['name'] in user_resources:
                        continue
                dp = json.dumps(gen)
                byt += len(dp)
                if first:
                    yield u'' + dp
                    first = False
                else:
                    yield u', ' + dp
                num += 1
            stats.collect(num,byt)
            stats.sendwith()
            yield u']}'
        else:
            _, rsp = self._uve_server.get_uve(uve_name, flat, filters,
                                           base_url=base_url)
            if user_resources is None or (user_resources and name in \
                    user_resources):
                dp = json.dumps(rsp)
                stats.collect(1, len(dp))
                stats.sendwith()
                yield dp
            else:
                yield bottle.HTTPResponse(status = 403,
                        body = 'Operation Forbidden',
                        headers = self._reject_auth_headers())
    # end dyn_http_get

    @validate_user_token(only_cloud_admin=False)
    def alarms_http_get(self):
        # common handling for all resource get
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            bottle.abort(code, msg)

        bottle.response.set_header('Content-Type', 'application/json')

        req = bottle.request.query
        try:
            filters = OpServer._uve_filter_set(req)
        except Exception as e:
            return bottle.HTTPError(_ERRORS[errno.EBADMSG], e)
        else:
            filters['cfilt'] = { 'UVEAlarms':set() }
            alarm_list = self._uve_server.get_alarms(filters)
            alms = {}
            for ak,av in alarm_list.iteritems():
                alm_type = ak
                if ak in _OBJECT_TABLES:
                    alm_type = _OBJECT_TABLES[ak].log_query_name
                res_list = self.get_resource_list_from_uve_type(alm_type, False)
                ulist = []
                for uk, uv in av.iteritems():
                   if res_list is None or uk in res_list:
                       ulist.append({'name':uk, 'value':uv})
                if ulist:
                    alms[alm_type] = ulist
            if self._uvepartitions_state == ConnectionStatus.UP:
                return json.dumps(alms)
            else:
                return bottle.HTTPError(_ERRORS[errno.EIO],json.dumps(alms))
    # end alarms_http_get

    @validate_user_token(only_cloud_admin=False)
    def dyn_list_http_get(self, tables):
        # common handling for all resource get
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            bottle.abort(code, msg)
        arg_line = bottle.request.url.rsplit('/', 1)[1]
        uve_args = arg_line.split('?')
        uve_type = tables[:-1]
        if len(uve_args) != 1:
            uve_filters = ''
            filters = uve_args[1].split('&')
            filters = \
                [filt for filt in filters if filt[:len('kfilt')] != 'kfilt']
            if len(filters):
                uve_filters = '&'.join(filters)
            else:
                uve_filters = 'flat'
        else:
            uve_filters = 'flat'

        bottle.response.set_header('Content-Type', 'application/json')
        uve_tbl = uve_type
        if uve_type in UVE_MAP:
            uve_tbl = UVE_MAP[uve_type]

        res_list = self.get_resource_list_from_uve_type(uve_type)
        self._logger.debug("resourse list for %s is %s" %(uve_type, res_list))
        req = bottle.request.query
        try:
            filters = OpServer._uve_filter_set(req)
        except Exception as e:
            return bottle.HTTPError(_ERRORS[errno.EBADMSG], e)
        else:
            base_url = bottle.request.urlparts.scheme + '://' + \
                bottle.request.urlparts.netloc + \
                '/analytics/uves/%s/' % (uve_type)
            uve_list = self._uve_server.get_uve_list(
                    uve_tbl, filters, True)
            if res_list is not None:
                uve_list = res_list.intersection(uve_list)
            uve_links =\
                [obj_to_dict(LinkObject(uve,
                                        base_url + uve + "?" + uve_filters))
                 for uve in uve_list]

            return json.dumps(uve_links)
    # end dyn_list_http_get

    @validate_user_token(only_cloud_admin=False)
    def analytics_http_get(self):
        # common handling for all resource get
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            bottle.abort(code, msg)

        base_url = bottle.request.urlparts.scheme + '://' + \
            bottle.request.urlparts.netloc + '/analytics/'
        analytics_links = [obj_to_dict(LinkObject(link, base_url + link))
                           for link in self._analytics_links]
        bottle.response.set_header('Content-Type', 'application/json')
        return json.dumps(analytics_links)
    # end analytics_http_get

    @validate_user_token(only_cloud_admin=False)
    def uves_http_get(self):
        # common handling for all resource get
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            bottle.abort(code, msg)

        base_url = bottle.request.urlparts.scheme + '://' + \
            bottle.request.urlparts.netloc + '/analytics/uves/'
        uvetype_links = []
        
        # Show the list of UVE table-types based on actual raw UVE contents 
        tables = self._uve_server.get_tables()
        known = set()
        for apiname,rawname in UVE_MAP.iteritems():
            known.add(rawname)
            entry = obj_to_dict(LinkObject(apiname + 's',
                                    base_url + apiname + 's'))
            uvetype_links.append(entry)
 
        for rawname in tables:
            if not rawname in known:
                entry = obj_to_dict(LinkObject(rawname + 's',
                                    base_url + rawname + 's'))
                uvetype_links.append(entry)

        bottle.response.set_header('Content-Type', 'application/json')
        if self._uvepartitions_state == ConnectionStatus.UP:
            return json.dumps(uvetype_links)
        else:
            return bottle.HTTPError(_ERRORS[errno.EIO],json.dumps(uvetype_links))
    # end _uves_http_get

    @validate_user_token
    def uve_types_http_get(self):
        # common handling for all resource get
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            bottle.abort(code, msg)
        uve_types = {}
        for name, info in _OBJECT_TABLES.iteritems():
            if info.is_uve:
                uve_types[info.log_query_name] = {
                    'global_system_object': info.global_system_object}
        bottle.response.set_header('Content-Type', 'application/json')
        return json.dumps(uve_types)
    # end uve_types_http_get

    @validate_user_token
    def alarms_ack_http_post(self):
        self._post_common(bottle.request, None)
        if ('application/json' not in bottle.request.headers['Content-Type']):
            self._logger.error('Content-type is not JSON')
            return bottle.HTTPError(_ERRORS[errno.EBADMSG],
                'Content-Type must be JSON')
        self._logger.info('Alarm Acknowledge request: %s' % 
            (bottle.request.json))
        alarm_ack_fields = set(['table', 'name', 'type', 'token'])
        bottle_req_fields = set(bottle.request.json.keys())
        if len(alarm_ack_fields - bottle_req_fields):
            return bottle.HTTPError(_ERRORS[errno.EINVAL],
                'Alarm acknowledge request does not contain the fields '
                '{%s}' % (', '.join(alarm_ack_fields - bottle_req_fields)))
        try:
            table = UVE_MAP[bottle.request.json['table']]
        except KeyError:
            # If the table name is not present in the UVE_MAP, then
            # send the raw table name to the generator.
            table = bottle.request.json['table']

        # Decode generator ip, introspect port and timestamp from the
        # the token field.
        try:
            token = json.loads(base64.b64decode(bottle.request.json['token']))
        except (TypeError, ValueError):
            self._logger.error('Alarm Ack Request: Failed to decode "token"')
            return bottle.HTTPError(_ERRORS[errno.EINVAL],
                'Failed to decode "token"')
        exp_token_fields = set(['host_ip', 'http_port', 'timestamp'])
        actual_token_fields = set(token.keys())
        if len(exp_token_fields - actual_token_fields):
            self._logger.error('Alarm Ack Request: Invalid token value')
            return bottle.HTTPError(_ERRORS[errno.EINVAL],
                'Invalid token value')
        generator_introspect = GeneratorIntrospectUtil(token['host_ip'],
                                 token['http_port'], self._args.sandesh_config)
        try:
            res = generator_introspect.send_alarm_ack_request(
                table, bottle.request.json['name'],
                bottle.request.json['type'], token['timestamp'])
        except Exception as e:
            self._logger.error('Alarm Ack Request: Introspect request failed')
            return bottle.HTTPError(_ERRORS[errno.EBUSY],
                'Failed to process the Alarm Ack Request')
        self._logger.debug('Alarm Ack Response: %s' % (res))
        if res['status'] == 'false':
            return bottle.HTTPError(_ERRORS[errno.EIO], res['error_msg'])
        self._logger.info('Alarm Ack Request successfully processed')
        return bottle.HTTPResponse(status=200)
    # end alarms_ack_http_post

    @validate_user_token
    def send_trace_buffer(self, source, module, instance_id, name):
        response = {}
        trace_req = SandeshTraceRequest(name)
        if module not in ModuleIds:
            response['status'] = 'fail'
            response['error'] = 'Invalid module'
            return json.dumps(response)
        module_id = ModuleIds[module]
        node_type = Module2NodeType[module_id]
        node_type_name = NodeTypeNames[node_type]
        if self._state_server.redis_publish(msg_type='send-tracebuffer',
                                            destination=source + ':' + 
                                            node_type_name + ':' + module +
                                            ':' + instance_id,
                                            msg=trace_req):
            response['status'] = 'pass'
        else:
            response['status'] = 'fail'
            response['error'] = 'No connection to Redis'
        return json.dumps(response)
    # end send_trace_buffer

    @validate_user_token
    def tables_process(self):
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            bottle.abort(code, msg)

        base_url = bottle.request.urlparts.scheme + '://' + \
            bottle.request.urlparts.netloc + '/analytics/table/'
        json_links = []
        known = set()
        for i in range(0, len(self._VIRTUAL_TABLES)):
            known.add(self._VIRTUAL_TABLES[i].name)
            link = LinkObject(self._VIRTUAL_TABLES[
                              i].name, base_url + self._VIRTUAL_TABLES[i].name)
            tbl_info = obj_to_dict(link)
            tbl_info['type'] = self._VIRTUAL_TABLES[i].schema.type
            if (self._VIRTUAL_TABLES[i].display_name is not None):
                    tbl_info['display_name'] =\
                        self._VIRTUAL_TABLES[i].display_name
            json_links.append(tbl_info)

        # Show the list of UVE table-types based on actual raw UVE contents
        tables = self._uve_server.get_tables()
        for rawname in tables:
            if not rawname in known:
                link = LinkObject(rawname, base_url + rawname)
                tbl_info = obj_to_dict(link)
                tbl_info['type'] = 'OBJECT'
                tbl_info['display_name'] = rawname
                json_links.append(tbl_info)

        bottle.response.set_header('Content-Type', 'application/json')
        return json.dumps(json_links)
    # end tables_process

    def monitor_analytics_db(self):
        while True:
            gevent.sleep(10*60)
            db_usage = self._analytics_db.get_dbusage_info()
            disk_usage_percentage = None
            if len(db_usage):
                disk_usage_percentage = int(math.ceil(max(db_usage.values())))
            pending_compaction_tasks_info = \
                self._analytics_db.get_pending_compaction_tasks()
            pending_compaction_tasks = None
            if len(pending_compaction_tasks_info):
                pending_compaction_tasks = \
                    max(pending_compaction_tasks_info.values())
            if any([disk_usage_percentage, pending_compaction_tasks]):
                self.handle_db_info(disk_usage_percentage,
                                    pending_compaction_tasks)
    # end monitor_analytics_db

    def handle_db_info(self, disk_usage_percentage=None,
                       pending_compaction_tasks=None):
        if disk_usage_percentage is not None:
            self.disk_usage_percentage = disk_usage_percentage
        if pending_compaction_tasks is not None:
            self.pending_compaction_tasks = pending_compaction_tasks
        source = self._hostname
        module_id = Module.COLLECTOR
        module = ModuleNames[module_id]
        node_type = Module2NodeType[module_id]
        node_type_name = NodeTypeNames[node_type]
        instance_id_str = INSTANCE_ID_DEFAULT
        destination = source + ':' + node_type_name + ':' + module + ':' + \
                instance_id_str
        req = DbInfoSetRequest(disk_usage_percentage, pending_compaction_tasks)
        if self._state_server.redis_publish(msg_type='db-info',
                                            destination=destination, msg=req):
            self._logger.info('redis-publish success for disk_usage (%u%%), '
                              'pending_compaction_tasks(%u)',
                              req.disk_usage_percentage,
                              req.pending_compaction_tasks)
        else:
            self._logger.error('redis-publish failure for disk_usage (%u%%), '
                               'pending_compaction_tasks(%u)',
                               req.disk_usage_percentage,
                               req.pending_compaction_tasks)
    # end handle_db_info

    @validate_user_token
    def _get_analytics_data_start_time(self):
        analytics_start_time = (self._analytics_db.get_analytics_start_time())[SYSTEM_OBJECT_START_TIME]
        response = {'analytics_data_start_time': analytics_start_time}
        return bottle.HTTPResponse(
            json.dumps(response), 200, {'Content-type': 'application/json'})
    # end _get_analytics_data_start_time

    @validate_user_token(only_cloud_admin=False)
    def table_process(self, table):
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            bottle.abort(code, msg)

        base_url = bottle.request.urlparts.scheme + '://' + \
            bottle.request.urlparts.netloc + '/analytics/table/' + table + '/'

        json_links = []
        for i in range(0, len(self._VIRTUAL_TABLES)):
            if (self._VIRTUAL_TABLES[i].name == table):
                link = LinkObject('schema', base_url + 'schema')
                json_links.append(obj_to_dict(link))
                break

        if(len(json_links) == 0):
            # search the UVE table in raw UVE content
            tables = self._uve_server.get_tables()
            if table in tables:
                link = LinkObject('schema', base_url + 'schema')
                json_links.append(obj_to_dict(link))

        bottle.response.set_header('Content-Type', 'application/json')
        return json.dumps(json_links)
    # end table_process

    @validate_user_token(only_cloud_admin=False)
    def table_schema_process(self, table):
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            bottle.abort(code, msg)

        bottle.response.set_header('Content-Type', 'application/json')
        for i in range(0, len(self._VIRTUAL_TABLES)):
            if (self._VIRTUAL_TABLES[i].name == table):
                return json.dumps(self._VIRTUAL_TABLES[i].schema,
                                  default=lambda obj: obj.__dict__)

        # Also check for the table in actual raw UVE contents
        tables = self._uve_server.get_tables()
        if table in tables:
            return json.dumps(_OBJECT_TABLE_SCHEMA,
                                  default=lambda obj: obj.__dict__)

        return (json.dumps({}))
    # end table_schema_process

    def generator_info(self, table, column):
        if ((column == MODULE) or (column == SOURCE)):
            sources = []
            moduleids = []
            ulist = self.redis_uve_list
            
            for redis_uve in ulist:
                redish = StrictRedisWrapper(
                    db=1,
                    host=redis_uve[0],
                    port=redis_uve[1],
                    password=self._args.redis_password)
                try:
                    for key in redish.smembers("NGENERATORS"):
                        source = key.split(':')[0]
                        module = key.split(':')[2]
                        if (sources.count(source) == 0):
                            sources.append(source)
                        if (moduleids.count(module) == 0):
                            moduleids.append(module)
                except Exception as e:
                    self._logger.error('Exception: %s' % e)
            if column == MODULE:
                return moduleids
            elif column == SOURCE:
                return sources
        elif (column == 'Category'):
            return self._CATEGORY_MAP
        elif (column == 'Level'):
            return self._LEVEL_LIST
        elif (column == STAT_OBJECTID_FIELD):
            objtab = None
            for t in self._VIRTUAL_TABLES:
              if t.schema.type == 'STAT':
                self._logger.error("found stat table %s" % t)
                stat_table = STAT_VT_PREFIX + "." + \
                    t.stat_type + "." + t.stat_attr
                if (table == stat_table):
                    objtab = t.obj_table
                    break
            if (objtab != None) and (objtab != "None"): 
                return list(self._uve_server.get_uve_list(objtab))

        return []
    # end generator_info

    @validate_user_token
    def column_process(self, table, column):
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            bottle.abort(code, msg)

        bottle.response.set_header('Content-Type', 'application/json')
        for i in range(0, len(self._VIRTUAL_TABLES)):
            if (self._VIRTUAL_TABLES[i].name == table):
                if self._VIRTUAL_TABLES[i].columnvalues.count(column) > 0:
                    return (json.dumps(self.generator_info(table, column)))

        # Also check for the table in actual raw UVE contents
        tables = self._uve_server.get_tables()
        if table in tables:
            return (json.dumps(self.generator_info(table, column)))
        return (json.dumps([]))
    # end column_process

    def send_analytics_api_info_uve(self):
        analytics_api_info = AnalyticsApiInfo()
        analytics_api_info.name = self._hostname
        analytics_api_info.analytics_node_ip = dict()
        analytics_api_info.analytics_node_ip[
                'rest_api_ip'] = self._args.rest_api_ip
        analytics_api_info.analytics_node_ip[
                'host_ip'] = self._args.host_ip
        uve_analytics_api_info = AnalyticsApiInfoUVE(
                data=analytics_api_info,
                sandesh = self._sandesh)
        uve_analytics_api_info.send(sandesh = self._sandesh)
    #end send_analytics_api_info_uve

    def handle_UVEDbCacheTablesRequest(self, req):
        tables = self._uve_server.get_uvedb_cache_tables()
        resp = UVEDbCacheTablesResponse()
        resp.tables = [UVEDbCacheTable(table) for table in tables]
        resp.response(req.context())
    # end handle_UVEDbCacheTablesRequest

    def handle_UVEDbCacheTableKeysRequest(self, req):
        uve_keys = self._uve_server.get_uvedb_cache_table_keys(req.table)
        resp = UVEDbCacheTableKeysResponse()
        resp.uves = [UVEDbCacheTableKey(req.table+':'+key) for key in uve_keys]
        resp.response(req.context())
    # end handle_UVEDbCacheTableKeysRequest

    def handle_UVEDbCacheUveRequest(self, req):
        try:
            table, uve_key = req.table_uve_key.split(':', 1)
        except ValueError:
            raise
        uve = self._uve_server.get_uvedb_cache_uve(table, uve_key)
        resp = UVEDbCacheUveResponse()
        if uve:
            resp.source = json.dumps(uve.pop('__SOURCE__'))
            resp.data = json.dumps(uve)
        resp.response(req.context())
    # end handle_UVEDbCacheUveRequest

    def start_uve_server(self):
        self._uve_server.run()

    #end start_uve_server

    def stop_webserver(self):
        if self._webserver:
            self._webserver.stop()
            self._webserver = None

    def start_webserver(self):
        pipe_start_app = bottle.app()
        try:
            self._webserver = ContrailGeventServer(
                                   host=self._args.rest_api_ip,
                                   port=self._args.rest_api_port)
            bottle.run(app=pipe_start_app, server=self._webserver)
        except Exception as e:
            self._logger.error("Exception: %s" % e)
            sys.exit()
    # end start_webserver

    def disc_agp(self, clist):
        new_agp = {}
        for elem in clist:
            instance_id = elem['instance-id']
            port = int(elem['redis-port']) 
            ip_address = elem['ip-address']
            # If AlarmGenerator sends partitions as NULL, its
            # unable to provide service
            if not elem['partitions']:
                continue
            parts = json.loads(elem['partitions'])
            for partstr,acq_time in parts.iteritems():
                partno = int(partstr)
                pi = PartInfo(instance_id = instance_id,
                              ip_address = ip_address,
                              acq_time = acq_time,
                              port = port)
                if partno not in new_agp:
                    new_agp[partno] = pi
                else:
                    if pi.acq_time > new_agp[partno].acq_time:
                        new_agp[partno] = pi
        if len(new_agp) == self._args.partitions and \
                len(self.agp) != self._args.partitions:
            ConnectionState.update(conn_type = ConnectionType.UVEPARTITIONS,
                name = 'UVE-Aggregation', status = ConnectionStatus.UP,
                message = 'Partitions:%d' % len(new_agp))
            self._uvepartitions_state = ConnectionStatus.UP
        if self._usecache and len(new_agp) != self._args.partitions:
            ConnectionState.update(conn_type = ConnectionType.UVEPARTITIONS,
                name = 'UVE-Aggregation', status = ConnectionStatus.DOWN,
                message = 'Partitions:%d' % len(new_agp))
            self._uvepartitions_state = ConnectionStatus.DOWN
        self.agp = new_agp        

    def get_agp(self):
        return self.agp

    def run(self):
        if self._usecache:
            self._uvedbstream.start()
            self.gevs.append(self._uvedbstream)
        self.send_analytics_api_info_uve()

        self.gevs += [
            gevent.spawn(self.start_webserver),
            gevent.spawn(self.start_uve_server),
            gevent.spawn(self.monitor_analytics_db)
            ]

        if self._ad is not None:
            self._ad.start()

        if self._vnc_api_client:
            self._vnc_api_client_connect = gevent.spawn(
                self._vnc_api_client.connect)
            self.gevs.append(self._vnc_api_client_connect)
        self._local_app = LocalApp(bottle.app(), self._args.auth_conf_info)
        self.gevs.append(gevent.spawn(self._local_app.start_http_server))

        try:
            gevent.joinall(self.gevs)
        except KeyboardInterrupt:
            self._logger.error('Exiting on ^C')
        except gevent.GreenletExit:
            self._logger.error('Exiting on gevent-kill')
        except:
            raise
        finally:
            self._logger.error('stopping everything')
            self.stop()
            exit(2)

    def stop(self):
        self._sandesh._client._connection.set_admin_state(down=True)
        self._sandesh.uninit()
        self.stop_webserver()
        if self._ad is not None:
            self._ad.kill()
        l = len(self.gevs)
        for idx in range(0,l):
            self._logger.error('killing %d of %d' % (idx+1, l))
            self.gevs[0].kill()
            self._logger.error('joining %d of %d' % (idx+1, l))
            self.gevs[0].join()
            self._logger.error('stopped %d of %d' % (idx+1, l))
            self.gevs.pop(0)

    def sigterm_handler(self):
        self.stop()
        exit()

    def sighup_handler(self):
        if self._args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read(self._args.conf_file)
            if 'DEFAULTS' in config.sections():
                try:
                    collectors = config.get('DEFAULTS', 'collectors')
                except ConfigParser.NoOptionError as e:
                    pass
                else:
                    if type(collectors) is str:
                        collectors = collectors.split()
                        new_chksum = hashlib.md5("".join(collectors)).hexdigest()
                        if new_chksum != self._chksum:
                            self._chksum = new_chksum
                            self.random_collectors = random.sample(collectors, len(collectors))
                        # Reconnect to achieve load-balance irrespective of list 
                        self._sandesh.reconfig_collectors(self.random_collectors)
                try:
                    api_servers = config.get('DEFAULTS', 'api_server')
                except ConfigParser.NoOptionError as e:
                    pass
                else:
                    if type(api_servers) is str:
                        api_servers = api_servers.split()
                    new_api_server_checksum = hashlib.md5(''.join(
                        api_servers)).hexdigest()
                    if new_api_server_checksum != self._api_server_checksum:
                        self._api_server_checksum = new_api_server_checksum
                        random_api_servers = random.sample(api_servers,
                            len(api_servers))
                        if self._vnc_api_client_connect:
                            self.gevs.remove(self._vnc_api_client_connect)
                            if not self._vnc_api_client_connect.ready():
                                self._vnc_api_client_connect.kill()
                        if self._vnc_api_client:
                            self._vnc_api_client_connect = gevent.spawn(
                                self._vnc_api_client.update_api_servers,
                                    random_api_servers)
                            self.gevs.append(self._vnc_api_client_connect)
            if 'REDIS' in config.sections():
                try:
                    new_redis_list = config.get('REDIS', 'redis_uve_list')
                except ConfigParser.NoOptionError:
                    pass
                else:
                    redis_uve_list = []
                    for redis_uve in new_redis_list.split():
                        redis_ip_port = redis_uve.split(':')
                        redis_elem = (redis_ip_port[0], int(redis_ip_port[1]))
                        redis_uve_list.append(redis_elem)
                    self._uve_server.update_redis_uve_list(redis_uve_list)
    # end sighup_handler

def main(args_str=' '.join(sys.argv[1:])):
    opserver = OpServer(args_str)
    gevent.hub.signal(signal.SIGTERM, opserver.sigterm_handler)
    """ @sighup
    SIGHUP handler to indicate configuration changes
    """
    gevent.hub.signal(signal.SIGHUP, opserver.sighup_handler)
    gv = gevent.getcurrent()
    gv._main_obj = opserver
    opserver.run()


if __name__ == '__main__':
    main()
