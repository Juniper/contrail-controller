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
from uveserver import UVEServer
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
import errno
import copy
import datetime

from pysandesh.sandesh_base import *
from pysandesh.sandesh_session import SandeshWriter
from pysandesh.gen_py.sandesh_trace.ttypes import SandeshTraceRequest
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.connection_info.ttypes import ConnectionType,\
    ConnectionStatus, ConnectivityStatus
from pysandesh.gen_py.connection_info.constants import \
    ConnectionStatusNames
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, CategoryNames,\
     ModuleCategoryMap, Module2NodeType, NodeTypeNames, ModuleIds,\
     INSTANCE_ID_DEFAULT
from sandesh.viz.constants import _TABLES, _OBJECT_TABLES,\
    _OBJECT_TABLE_SCHEMA, _OBJECT_TABLE_COLUMN_VALUES, \
    _STAT_TABLES, STAT_OBJECTID_FIELD, STAT_VT_PREFIX, \
    STAT_TIME_FIELD, STAT_TIMEBIN_FIELD, STAT_UUID_FIELD, \
    STAT_SOURCE_FIELD, SOURCE, MODULE
from sandesh.viz.constants import *
from sandesh.analytics.ttypes import *
from sandesh.analytics.cpuinfo.ttypes import ProcessCpuInfo
from sandesh.discovery.ttypes import CollectorTrace
from opserver_util import OpServerUtils
from cpuinfo import CpuInfoData
from sandesh_req_impl import OpserverSandeshReqImpl

_ERRORS = {
    errno.EBADMSG: 400,
    errno.EINVAL: 404,
    errno.ENOENT: 410,
    errno.EIO: 500,
    errno.EBUSY: 503
}

@bottle.error(400)
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


def obj_to_dict(obj):
    # Non-null fields in object get converted to json fields
    return dict((k, v) for k, v in obj.__dict__.iteritems())
# end obj_to_dict


def redis_query_start(host, port, qid, inp):
    redish = redis.StrictRedis(db=0, host=host, port=port)
    for key, value in inp.items():
        redish.hset("QUERY:" + qid, key, json.dumps(value))
    query_metadata = {}
    query_metadata['enqueue_time'] = OpServerUtils.utc_timestamp_usec()
    redish.hset("QUERY:" + qid, 'query_metadata', json.dumps(query_metadata))
    redish.hset("QUERY:" + qid, 'enqueue_time',
                OpServerUtils.utc_timestamp_usec())
    redish.lpush("QUERYQ", qid)

    res = redish.blpop("REPLY:" + qid, 10)
    if res is None:
        return None
    # Put the status back on the queue for the use of the status URI
    redish.lpush("REPLY:" + qid, res[1])

    resp = json.loads(res[1])
    return int(resp["progress"])
# end redis_query_start


def redis_query_status(host, port, qid):
    redish = redis.StrictRedis(db=0, host=host, port=port)
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


def redis_query_chunk_iter(host, port, qid, chunk_id):
    redish = redis.StrictRedis(db=0, host=host, port=port)

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


def redis_query_chunk(host, port, qid, chunk_id):
    res_iter = redis_query_chunk_iter(host, port, qid, chunk_id)

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


def redis_query_result(host, port, qid):
    try:
        status = redis_query_status(host, port, qid)
    except redis.exceptions.ConnectionError:
        # Update connection info
        ConnectionState.update(conn_type = ConnectionType.REDIS,
            name = 'Query', status = ConnectionStatus.DOWN,
            message = 'Query[%s] result : Connection Error' % (qid),
            server_addrs = ['%s:%d' % (host, port)]) 
        yield bottle.HTTPError(_ERRORS[errno.EIO],
                'Failure in connection to the query DB')
    except Exception as e:
        # Update connection info
        ConnectionState.update(conn_type = ConnectionType.REDIS,
            name = 'Query', status = ConnectionStatus.DOWN,
            message = 'Query[%s] result : Exception: %s' % (qid, str(e)),
            server_addrs = ['%s:%d' % (host, port)])
        self._logger.error("Exception: %s" % e)
        yield bottle.HTTPError(_ERRORS[errno.EIO], 'Error: %s' % e)
    else:
        if status is None:
            yield bottle.HTTPError(_ERRORS[errno.ENOENT], 
                    'Invalid query id (or) query result purged from DB')
        if status['progress'] == 100:
            for chunk in status['chunks']:
                chunk_id = int(chunk['href'].rsplit('/', 1)[1])
                for gen in redis_query_chunk(host, port, qid, chunk_id):
                    yield gen
        else:
            yield {}
    # Update connection info
    ConnectionState.update(conn_type = ConnectionType.REDIS,
        message = 'Query[%s] result' % (qid),
        status = ConnectionStatus.UP,
        server_addrs = ['%s:%d' % (host, port)],
        name = 'Query')
    return
# end redis_query_result


def redis_query_result_dict(host, port, qid):

    stat = redis_query_status(host, port, qid)
    prg = int(stat["progress"])
    res = []

    if (prg < 0) or (prg == 100):

        done = False
        gen = redis_query_result(host, port, qid)
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

    def __init__(self, logger):
        self._logger = logger
        self._redis_list = []
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
            redis_inst = redis.StrictRedis(redis_server[0], 
                                           redis_server[1], db=0)
            try:
                redis_inst.publish('analytics', redis_msg)
            except redis.exceptions.ConnectionError:
                # Update connection info
                ConnectionState.update(conn_type = ConnectionType.REDIS,
                    name = 'UVE', status = ConnectionStatus.DOWN,
                    message = 'Connection Error',
                    server_addrs = ['%s:%d' % (redis_server[0], \
                        redis_server[1])])
                self._logger.error('No Connection to Redis [%s:%d].'
                                   'Failed to publish message.' \
                                   % (redis_server[0], redis_server[1]))
        return True
    # end redis_publish

# end class OpStateServer

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
        * ``/analytics/table/<table>/column-values``:
        * ``/analytics/table/<table>/column-values/<column>``:
        * ``/analytics/query/<queryId>``
        * ``/analytics/query/<queryId>/chunk-final/<chunkId>``
        * ``/analytics/send-tracebuffer/<source>/<module>/<name>``

    The supported **POST** APIs are:
        * ``/analytics/query``:
    """

    def __new__(cls, *args, **kwargs):
        obj = super(OpServer, cls).__new__(cls, *args, **kwargs)
        bottle.route('/', 'GET', obj.homepage_http_get)
        bottle.route('/analytics', 'GET', obj.analytics_http_get)
        bottle.route('/analytics/uves', 'GET', obj.uves_http_get)
        bottle.route(
            '/analytics/virtual-networks', 'GET', obj.uve_list_http_get)
        bottle.route(
            '/analytics/virtual-machines', 'GET', obj.uve_list_http_get)
        bottle.route(
            '/analytics/service-instances', 'GET', obj.uve_list_http_get)
        bottle.route('/analytics/service-chains', 'GET', obj.uve_list_http_get)
        bottle.route('/analytics/vrouters', 'GET', obj.uve_list_http_get)
        bottle.route('/analytics/bgp-routers', 'GET', obj.uve_list_http_get)
        bottle.route('/analytics/bgp-peers', 'GET', obj.uve_list_http_get)
        bottle.route('/analytics/xmpp-peers', 'GET', obj.uve_list_http_get)
        bottle.route('/analytics/collectors', 'GET', obj.uve_list_http_get)
        bottle.route('/analytics/generators', 'GET', obj.uve_list_http_get)
        bottle.route('/analytics/config-nodes', 'GET', obj.uve_list_http_get)
        bottle.route(
            '/analytics/virtual-network/<name>', 'GET', obj.uve_http_get)
        bottle.route(
            '/analytics/virtual-machine/<name>', 'GET', obj.uve_http_get)
        bottle.route(
            '/analytics/service-instance/<name>', 'GET', obj.uve_http_get)
        bottle.route(
            '/analytics/service-chain/<name>', 'GET', obj.uve_http_get)
        bottle.route('/analytics/vrouter/<name>', 'GET', obj.uve_http_get)
        bottle.route('/analytics/bgp-router/<name>', 'GET', obj.uve_http_get)
        bottle.route('/analytics/bgp-peer/<name>', 'GET', obj.uve_http_get)
        bottle.route('/analytics/xmpp-peer/<name>', 'GET', obj.uve_http_get)
        bottle.route('/analytics/collector/<name>', 'GET', obj.uve_http_get)
        bottle.route('/analytics/generator/<name>', 'GET', obj.uve_http_get)
        bottle.route('/analytics/config-node/<name>', 'GET', obj.uve_http_get)
        bottle.route('/analytics/query', 'POST', obj.query_process)
        bottle.route('/analytics/query/<queryId>', 'GET', obj.query_status_get)
        bottle.route('/analytics/query/<queryId>/chunk-final/<chunkId>',
                     'GET', obj.query_chunk_get)
        bottle.route('/analytics/queries', 'GET', obj.show_queries)
        bottle.route('/analytics/tables', 'GET', obj.tables_process)
        bottle.route('/analytics/table/<table>', 'GET', obj.table_process)
        bottle.route(
            '/analytics/table/<table>/schema', 'GET', obj.table_schema_process)
        for i in range(0, len(_TABLES)):
            if len(_TABLES[i].columnvalues) > 0:
                bottle.route('/analytics/table/<table>/column-values',
                             'GET', obj.column_values_process)
                bottle.route('/analytics/table/<table>/column-values/<column>',
                             'GET', obj.column_process)
        bottle.route('/analytics/send-tracebuffer/<source>/<module>/<instance_id>/<name>',
                     'GET', obj.send_trace_buffer)
        bottle.route('/documentation/<filename:path>', 'GET',
                     obj.documentation_http_get)

        for uve in UVE_MAP:
            bottle.route(
                '/analytics/uves/' + uve + 's', 'GET', obj.uve_list_http_get)
            bottle.route(
                '/analytics/uves/' + uve + '/<name>', 'GET', obj.uve_http_get)
            bottle.route(
                '/analytics/uves/' + uve, 'POST', obj.uve_http_post)

        return obj
    # end __new__

    def disc_publish(self):
        try:
            import discoveryclient.client as client
        except:
            try:
                # TODO: Try importing from the server. This should go away..
                import discovery.client as client
            except:
                raise Exception('Could not get Discovery Client')

        data = {
            'ip-address': self._args.host_ip,
            'port': self._args.rest_api_port,
        }
        self.disc = client.DiscoveryClient(
            self._args.disc_server_ip,
            self._args.disc_server_port,
            ModuleNames[Module.OPSERVER])
        self._logger.info("Disc Publish to %s : %d - %s"
                          % (self._args.disc_server_ip,
                             self._args.disc_server_port, str(data)))
        self.disc.publish(self._moduleid, data)
    # end

    def _get_process_connectivity_status(self, conn_infos):
        for conn_info in conn_infos:
            if conn_info.status != ConnectionStatusNames[ConnectionStatus.UP]:
                return (ConnectivityStatus.NON_FUNCTIONAL,
                        conn_info.type + ':' + conn_info.name)
        return (ConnectivityStatus.FUNCTIONAL, '')
    #end _get_process_connectivity_status

    def __init__(self):
        self._args = None
        self._parse_args()

        self._homepage_links = []
        self._homepage_links.append(
            LinkObject('documentation', '/documentation/index.html'))
        self._homepage_links.append(LinkObject('analytics', '/analytics'))

        super(OpServer, self).__init__()
        module = Module.OPSERVER
        self._moduleid = ModuleNames[module]
        node_type = Module2NodeType[module]
        self._node_type_name = NodeTypeNames[node_type]
        if self._args.worker_id:
            self._instance_id = self._args.worker_id
        else:
            self._instance_id = INSTANCE_ID_DEFAULT
        self._hostname = socket.gethostname()
        if self._args.dup:
            self._hostname += 'dup'
        opserver_sandesh_req_impl = OpserverSandeshReqImpl(self) 
        sandesh_global.init_generator(self._moduleid, self._hostname,
                                      self._node_type_name, self._instance_id,
                                      self._args.collectors, 'opserver_context',
                                      int(self._args.http_server_port),
                                      ['opserver.sandesh'])
        sandesh_global.set_logging_params(
            enable_local_log=self._args.log_local,
            category=self._args.log_category,
            level=self._args.log_level,
            file=self._args.log_file,
            enable_syslog=self._args.use_syslog,
            syslog_facility=self._args.syslog_facility)
        ConnectionState.init(sandesh_global, self._hostname, self._moduleid, 
            self._instance_id, self._get_process_connectivity_status, 
            AnalyticsProcessStatusUVE, AnalyticsProcessStatus) 
        
        # Trace buffer list
        self.trace_buf = [
            {'name':'DiscoveryMsg', 'size':1000}
        ]
        # Create trace buffers 
        for buf in self.trace_buf:
            sandesh_global.trace_buffer_create(name=buf['name'], size=buf['size'])

        self._logger = sandesh_global._logger
        self._get_common = self._http_get_common
        self._put_common = self._http_put_common
        self._delete_common = self._http_delete_common
        self._post_common = self._http_post_common

        self._collector_pool = None
        self._state_server = OpStateServer(self._logger)
        self._uve_server = UVEServer(('127.0.0.1',
                                      self._args.redis_server_port),
                                     self._logger)

        self._LEVEL_LIST = []
        for k in SandeshLevel._VALUES_TO_NAMES:
            if (k < SandeshLevel.UT_START):
                d = {}
                d[k] = SandeshLevel._VALUES_TO_NAMES[k]
                self._LEVEL_LIST.append(d)
        self._CATEGORY_MAP =\
            dict((ModuleNames[k], [CategoryNames[ce] for ce in v])
                 for k, v in ModuleCategoryMap.iteritems())

        self.disc = None
        if self._args.disc_server_ip:
            self.disc_publish()
        else:
            self.redis_uve_list = []
            try:
                if type(self._args.redis_uve_list) is str:
                    self._args.redis_uve_list = self._args.redis_uve_list.split()
                for redis_uve in self._args.redis_uve_list:
                    redis_ip_port = redis_uve.split(':')
                    redis_ip_port = (redis_ip_port[0], int(redis_ip_port[1]))
                    self.redis_uve_list.append(redis_ip_port)
            except Exception as e:
                self._logger.error('Failed to parse redis_uve_list: %s' % e)
            else:
                self._state_server.update_redis_list(self.redis_uve_list)
                self._uve_server.update_redis_uve_list(self.redis_uve_list)

        self._analytics_links = ['uves', 'tables', 'queries']

        self._VIRTUAL_TABLES = copy.deepcopy(_TABLES)

        for t in _OBJECT_TABLES:
            obj = query_table(
                name=t, display_name=_OBJECT_TABLES[t].objtable_display_name,
                schema=_OBJECT_TABLE_SCHEMA,
                columnvalues=_OBJECT_TABLE_COLUMN_VALUES)
            self._VIRTUAL_TABLES.append(obj)

        for t in _STAT_TABLES:
            stat_id = t.stat_type + "." + t.stat_attr
            scols = []

            keyln = stat_query_column(name=STAT_SOURCE_FIELD, datatype='string', index=True)
            scols.append(keyln)

            tln = stat_query_column(name=STAT_TIME_FIELD, datatype='int', index=False)
            scols.append(tln)

            teln = stat_query_column(name=STAT_TIMEBIN_FIELD, datatype='int', index=False)
            scols.append(teln)

            uln = stat_query_column(name=STAT_UUID_FIELD, datatype='uuid', index=False)
            scols.append(uln)

            cln = stat_query_column(name="COUNT(" + t.stat_attr + ")",
                    datatype='int', index=False)
            scols.append(cln)

            isname = False
            for aln in t.attributes:
                if aln.name==STAT_OBJECTID_FIELD:
                    isname = True
                scols.append(aln)
                if aln.datatype in ['int','double']:
                    sln = stat_query_column(name= "SUM(" + aln.name + ")",
                            datatype=aln.datatype, index=False)
                    scols.append(sln)

            if not isname: 
                keyln = stat_query_column(name=STAT_OBJECTID_FIELD, datatype='string', index=True)
                scols.append(keyln)

            sch = query_schema_type(type='STAT', columns=scols)

            stt = query_table(
                name = STAT_VT_PREFIX + "." + stat_id,
                display_name = t.display_name,
                schema = sch,
                columnvalues = [STAT_OBJECTID_FIELD, SOURCE])
            self._VIRTUAL_TABLES.append(stt)

        bottle.route('/', 'GET', self.homepage_http_get)
        bottle.route('/analytics', 'GET', self.analytics_http_get)
        bottle.route('/analytics/uves', 'GET', self.uves_http_get)
        bottle.route(
            '/analytics/virtual-networks', 'GET', self.uve_list_http_get)
        bottle.route(
            '/analytics/virtual-machines', 'GET', self.uve_list_http_get)
        bottle.route(
            '/analytics/service-instances', 'GET', self.uve_list_http_get)
        bottle.route(
            '/analytics/service-chains', 'GET', self.uve_list_http_get)
        bottle.route('/analytics/vrouters', 'GET', self.uve_list_http_get)
        bottle.route('/analytics/bgp-routers', 'GET', self.uve_list_http_get)
        bottle.route('/analytics/collectors', 'GET', self.uve_list_http_get)
        bottle.route('/analytics/generators', 'GET', self.uve_list_http_get)
        bottle.route('/analytics/config-nodes', 'GET', self.uve_list_http_get)
        bottle.route(
            '/analytics/virtual-network/<name>', 'GET', self.uve_http_get)
        bottle.route(
            '/analytics/virtual-machine/<name>', 'GET', self.uve_http_get)
        bottle.route(
            '/analytics/service-instance/<name>', 'GET', self.uve_http_get)
        bottle.route(
            '/analytics/service-chain/<name>', 'GET', self.uve_http_get)
        bottle.route('/analytics/vrouter/<name>', 'GET', self.uve_http_get)
        bottle.route('/analytics/bgp-router/<name>', 'GET', self.uve_http_get)
        bottle.route('/analytics/collector/<name>', 'GET', self.uve_http_get)
        bottle.route('/analytics/generator/<name>', 'GET', self.uve_http_get)
        bottle.route('/analytics/config-node/<name>', 'GET', self.uve_http_get)
        bottle.route('/analytics/query', 'POST', self.query_process)
        bottle.route(
            '/analytics/query/<queryId>', 'GET', self.query_status_get)
        bottle.route('/analytics/query/<queryId>/chunk-final/<chunkId>',
                     'GET', self.query_chunk_get)
        bottle.route('/analytics/queries', 'GET', self.show_queries)
        bottle.route('/analytics/tables', 'GET', self.tables_process)
        bottle.route('/analytics/table/<table>', 'GET', self.table_process)
        bottle.route('/analytics/table/<table>/schema',
                     'GET', self.table_schema_process)
        for i in range(0, len(self._VIRTUAL_TABLES)):
            if len(self._VIRTUAL_TABLES[i].columnvalues) > 0:
                bottle.route('/analytics/table/<table>/column-values',
                             'GET', self.column_values_process)
                bottle.route('/analytics/table/<table>/column-values/<column>',
                             'GET', self.column_process)
        bottle.route('/analytics/send-tracebuffer/<source>/<module>/<instance_id>/<name>',
                     'GET', self.send_trace_buffer)
        bottle.route('/documentation/<filename:path>',
                     'GET', self.documentation_http_get)

        for uve in UVE_MAP:
            bottle.route(
                '/analytics/uves/' + uve + 's', 'GET', self.uve_list_http_get)
            bottle.route(
                '/analytics/uves/' + uve + '/<name>', 'GET', self.uve_http_get)
            bottle.route(
                '/analytics/uves/' + uve, 'POST', self.uve_http_post)
    # end __init__

    def _parse_args(self, args_str=' '.join(sys.argv[1:])):
        '''
        Eg. python opserver.py --host_ip 127.0.0.1
                               --redis_server_port 6379
                               --redis_query_port 6379
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
                               --redis_uve_list 127.0.0.1:6379
        '''
        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'host_ip'            : "127.0.0.1",
            'collectors'         : ['127.0.0.1:8086'],
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
            'redis_uve_list'     : ['127.0.0.1:6379']
        }
        redis_opts = {
            'redis_server_port'  : 6379,
            'redis_query_port'   : 6379,
        }
        disc_opts = {
            'disc_server_ip'     : None,
            'disc_server_port'   : 5998,
        }

        config = None
        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read([args.conf_file])
            defaults.update(dict(config.items("DEFAULTS")))
            if 'REDIS' in config.sections():
                redis_opts.update(dict(config.items('REDIS')))
            if 'DISCOVERY' in config.sections():
                disc_opts.update(dict(config.items('DISCOVERY')))

        # Override with CLI options
        # Don't surpress add_help here so it will handle -h

        parser = argparse.ArgumentParser(
            # Inherit options from config_parser
            parents=[conf_parser],
            # print script description with -h/--help
            description=__doc__,
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        defaults.update(redis_opts)
        defaults.update(disc_opts)
        parser.set_defaults(**defaults)

        parser.add_argument("--host_ip",
            help="Host IP address")
        parser.add_argument("--redis_server_port",
            type=int,
            help="Redis server port")
        parser.add_argument("--redis_query_port",
            type=int,
            help="Redis query port")
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
        parser.add_argument("--disc_server_ip",
            help="Discovery Server IP address")
        parser.add_argument("--disc_server_port",
            type=int,
            help="Discovery Server port")
        parser.add_argument("--dup", action="store_true",
            help="Internal use")
        parser.add_argument("--redis_uve_list",
            help="List of redis-uve in ip:port format. For internal use only",
            nargs="+")
        parser.add_argument(
            "--worker_id",
            help="Worker Id")

        self._args = parser.parse_args(remaining_argv)
        if type(self._args.collectors) is str:
            self._args.collectors = self._args.collectors.split()
        if type(self._args.redis_uve_list) is str:
            self._args.redis_uve_list = self._args.redis_uve_list.split()
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

    def documentation_http_get(self, filename):
        return bottle.static_file(
            filename, root='/usr/share/doc/python-vnc_opserver/html')
    # end documentation_http_get

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
                                      qid=qid)
        except redis.exceptions.ConnectionError:
            # Update connection info
            ConnectionState.update(conn_type = ConnectionType.REDIS,
                name = 'Query', status = ConnectionStatus.DOWN,
                message = 'Query[%s] status : Connection Error' % (qid),
                server_addrs = ['%s:%s' % (redis_query_ip, \
                    str(self._args.redis_query_port))])
            return bottle.HTTPError(_ERRORS[errno.EIO],
                    'Failure in connection to the query DB')
        except Exception as e:
            # Update connection info
            ConnectionState.update(conn_type = ConnectionType.REDIS,
                name = 'Query', status = ConnectionStatus.DOWN,
                message = 'Query[%s] status : Exception %s' % (qid, str(e)),
                server_addrs = ['%s:%s' % (redis_query_ip, \
                    str(self._args.redis_query_port))])
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
                                    qid=qid, chunk_id=chunk_id)
            bottle.response.set_header('Content-Type', 'application/json')
            while not done:
                try:
                    yield gen.next()
                except StopIteration:
                    done = True
        except redis.exceptions.ConnectionError:
            # Update connection info
            ConnectionState.update(conn_type = ConnectionType.REDIS,
                name = 'Query', status = ConnectionStatus.DOWN,
                message = 'Query [%s] chunk #%d : Connection Error' % \
                    (qid, chunk_id),
                server_addrs = ['%s:%s' % (redis_query_ip, \
                    str(self._args.redis_query_port))])
            yield bottle.HTTPError(_ERRORS[errno.EIO],
                    'Failure in connection to the query DB')
        except Exception as e:
            # Update connection info
            ConnectionState.update(conn_type = ConnectionType.REDIS,
                name = 'Query', status = ConnectionStatus.DOWN,
                message = 'Query [%s] chunk #%d : Exception %s' % \
                    (qid, chunk_id, str(e)),
                server_addrs = ['%s:%s' % (redis_query_ip, \
                    str(self._args.redis_query_port))])
            self._logger.error("Exception: %s" % str(e))
            yield bottle.HTTPError(_ERRORS[errno.ENOENT], 'Error: %s' % e)
        else:
            self._logger.info(
                "Query [%s] chunk #%d read at time %d"
                % (qid, chunk_id, time.time()))
    # end _query_chunk

    def _query(self, request):
        reply = {}
        try:
            redis_query_ip, = struct.unpack('>I', socket.inet_pton(
                                        socket.AF_INET, self._args.host_ip))
            qid = str(uuid.uuid1(redis_query_ip))
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
                    reply = bottle.HTTPError(_ERRORS[errno.ENOENT], 
                                'Table %s not found' % tabl)
                    yield reply
                    return
                else:
                    self._logger.info("Schema not known for dynamic table %s" % tabl)

            prg = redis_query_start('127.0.0.1',
                                    int(self._args.redis_query_port),
                                    qid, request.json)
            if prg is None:
                # Update connection info
                ConnectionState.update(conn_type = ConnectionType.REDIS,
                    name = 'Query', status = ConnectionStatus.DOWN,
                    message = 'Query[%s] Query Engine not responding' % qid,
                    server_addrs = ['127.0.0.1' + ':' + 
                        str(self._args.redis_query_port)])  
                self._logger.error('QE Not Responding')
                yield bottle.HTTPError(_ERRORS[errno.EBUSY], 
                        'Query Engine is not responding')
                return

        except redis.exceptions.ConnectionError:
            # Update connection info
            ConnectionState.update(conn_type = ConnectionType.REDIS,
                name = 'Query', status = ConnectionStatus.DOWN,
                message = 'Query[%s] Connection Error' % (qid),
                server_addrs = ['127.0.0.1' + ':' + 
                    str(self._args.redis_query_port)])  
            yield bottle.HTTPError(_ERRORS[errno.EIO],
                    'Failure in connection to the query DB')
        except Exception as e:
            # Update connection info
            ConnectionState.update(conn_type = ConnectionType.REDIS,
                name = 'Query', status = ConnectionStatus.DOWN,
                message = 'Query[%s] Exception: %s' % (qid, str(e)),
                server_addrs = ['127.0.0.1' + ':' + 
                    str(self._args.redis_query_port)])  
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
                self._logger.error("Found Error %s" % errno.errorcode[cod])
                reply = bottle.HTTPError(_ERRORS[cod], errno.errorcode[cod])
                yield reply
                return

            # In Sync mode, its time to read the final result. Status is in
            # "resp"
            done = False
            gen = redis_query_result(host='127.0.0.1',
                                     port=int(self._args.redis_query_port),
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
            # Update connection info
            ConnectionState.update(conn_type = ConnectionType.REDIS,
                name = 'Query', status = ConnectionStatus.DOWN,
                message = 'Sync Query[%s] Connection Error' % qid,
                server_addrs = ['127.0.0.1' + ':' + 
                    str(self._args.redis_query_port)])  
            yield bottle.HTTPError(_ERRORS[errno.EIO],
                    'Failure in connection to the query DB')
        except Exception as e:
            # Update connection info
            ConnectionState.update(conn_type = ConnectionType.REDIS,
                name = 'Query', status = ConnectionStatus.DOWN,
                message = 'Sync Query[%s] Exception: %s' % (qid, str(e)),
                server_addrs = ['127.0.0.1' + ':' + 
                    str(self._args.redis_query_port)])  
            self._logger.error("Exception: %s" % str(e))
            yield bottle.HTTPError(_ERRORS[errno.EIO], 
                    'Error: %s' % e)
        else:
            # Update connection info
            ConnectionState.update(conn_type = ConnectionType.REDIS,
                name = 'Query', status = ConnectionStatus.UP,
                message = 'Sync Query[%s] at %s' % \
                    (qid, datetime.datetime.now().isoformat()),
                server_addrs = ['127.0.0.1' + ':' + 
                    str(self._args.redis_query_port)]) 
            self._logger.info(
                "Query Result available at time %d" % time.time())
        return
    # end _sync_query

    def query_process(self):
        self._post_common(bottle.request, None)
        result = self._query(bottle.request)
        return result
    # end query_process

    def query_status_get(self, queryId):
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            abort(code, msg)
        return self._query_status(bottle.request, queryId)
    # end query_status_get

    def query_chunk_get(self, queryId, chunkId):
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            abort(code, msg)
        return self._query_chunk(bottle.request, queryId, int(chunkId))
    # end query_chunk_get

    def show_queries(self):
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            abort(code, msg)
        queries = {}
        try:
            redish = redis.StrictRedis(db=0, host='127.0.0.1',
                                       port=int(self._args.redis_query_port))
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
            # Update connection info
            ConnectionState.update(conn_type = ConnectionType.REDIS,
                name = 'Query', status = ConnectionStatus.DOWN,
                message = 'Show queries : Connection Error',
                server_addrs = ['127.0.0.1' + ':' + 
                    str(self._args.redis_query_port)])  
            return bottle.HTTPError(_ERRORS[errno.EIO],
                    'Failure in connection to the query DB')
        except Exception as err:
            # Update connection info
            ConnectionState.update(conn_type = ConnectionType.REDIS,
                name = 'Query', status = ConnectionStatus.DOWN,
                message = 'Show queries : Exception %s' % str(err),
                server_addrs = ['127.0.0.1' + ':' + 
                    str(self._args.redis_query_port)])  
            self._logger.error("Exception in show queries: %s" % str(err))
            return bottle.HTTPError(_ERRORS[errno.EIO], 'Error: %s' % err)
        else:
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
        sfilter = None
        mfilter = None
        tfilter = None
        kfilter = None
        any_filter = False
        if 'sfilt' in req.keys():
            any_filter = True
            sfilter = req.sfilt
        if 'mfilt' in req.keys():
            any_filter = True
            mfilter = req.mfilt
        if 'cfilt' in req.keys():
            any_filter = True
            infos = req.cfilt.split(',')
            tfilter = OpServer._get_tfilter(infos)
        if 'kfilt' in req.keys():
            any_filter = True
            kfilter = req.kfilt.split(',')
        return any_filter, kfilter, sfilter, mfilter, tfilter
    # end _uve_filter_set

    @staticmethod
    def _uve_http_post_filter_set(req):
        try:
            kfilter = req['kfilt']
            if not isinstance(kfilter, list):
                raise ValueError('Invalid kfilt')
        except KeyError:
            kfilter = ['*']
        try:
            sfilter = req['sfilt']
        except KeyError:
            sfilter = None
        try:
            mfilter = req['mfilt']
        except KeyError:
            mfilter = None
        try:
            cfilt = req['cfilt']
            if not isinstance(cfilt, list):
                raise ValueError('Invalid cfilt')
        except KeyError:
            tfilter = None
        else:
            tfilter = OpServer._get_tfilter(cfilt)
        return True, kfilter, sfilter, mfilter, tfilter
    # end _uve_http_post_filter_set

    def uve_http_post(self):
        (ok, result) = self._post_common(bottle.request, None)
        if not ok:
            (code, msg) = result
            abort(code, msg)
        uve_type = bottle.request.url.rsplit('/', 1)[1]
        try:
            uve_tbl = UVE_MAP[uve_type]
        except Exception as e:
            yield bottle.HTTPError(_ERRORS[errno.EINVAL], 
                                   'Invalid table name')
        else:
            try:
                req = bottle.request.json
                _, kfilter, sfilter, mfilter, tfilter = \
                    OpServer._uve_http_post_filter_set(req)
            except Exception as err:
                yield bottle.HTTPError(_ERRORS[errno.EBADMSG], err)
            bottle.response.set_header('Content-Type', 'application/json')
            yield u'{"value": ['
            first = True
            for key in kfilter:
                if key.find('*') != -1:
                    uve_name = uve_tbl + ':*'
                    for gen in self._uve_server.multi_uve_get(uve_name, True,
                                                              kfilter, sfilter,
                                                              mfilter, tfilter):
                        if first:
                            yield u'' + json.dumps(gen)
                            first = False
                        else:
                            yield u', ' + json.dumps(gen)
                    yield u']}'
                    return
            first = True
            for key in kfilter:
                uve_name = uve_tbl + ':' + key
                rsp = self._uve_server.get_uve(uve_name, True, sfilter,
                                               mfilter, tfilter)
                if rsp != {}:
                    data = {'name': key, 'value': rsp}
                    if first:
                        yield u'' + json.dumps(data)
                        first = False
                    else:
                        yield u', ' + json.dumps(data)
            yield u']}'
    # end uve_http_post

    def uve_http_get(self, name):
        # common handling for all resource get
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            abort(code, msg)
        uve_type = bottle.request.url.rsplit('/', 2)[1]
        try:
            uve_tbl = UVE_MAP[uve_type]
        except Exception as e:
            yield {}
        else:
            bottle.response.set_header('Content-Type', 'application/json')
            uve_name = uve_tbl + ':' + name
            req = bottle.request.query

            flat = False
            if 'flat' in req.keys():
                flat = True

            any_filter, kfilter, sfilter, mfilter, tfilter = \
                OpServer._uve_filter_set(req)
            if any_filter:
                flat = True

            uve_name = uve_tbl + ':' + name
            if name.find('*') != -1:
                flat = True
                yield u'{"value": ['
                first = True
                for gen in self._uve_server.multi_uve_get(uve_name, flat,
                                                          kfilter, sfilter,
                                                          mfilter, tfilter):
                    if first:
                        yield u'' + json.dumps(gen)
                        first = False
                    else:
                        yield u', ' + json.dumps(gen)
                yield u']}'
            else:
                rsp = self._uve_server.get_uve(uve_name, flat, sfilter,
                                               mfilter, tfilter)
                yield json.dumps(rsp)
    # end uve_http_get

    def uve_list_http_get(self):
        # common handling for all resource get
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            abort(code, msg)
        arg_line = bottle.request.url.rsplit('/', 1)[1]
        uve_args = arg_line.split('?')
        uve_type = uve_args[0][:-1]
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

        try:
            uve_tbl = UVE_MAP[uve_type]
        except Exception as e:
            return {}
        else:
            bottle.response.set_header('Content-Type', 'application/json')
            req = bottle.request.query

            _, kfilter, sfilter, mfilter, tfilter = \
                OpServer._uve_filter_set(req)

            uve_list = self._uve_server.get_uve_list(
                uve_tbl, kfilter, sfilter, mfilter, tfilter, True)
            base_url = bottle.request.urlparts.scheme + '://' + \
                bottle.request.urlparts.netloc + \
                '/analytics/uves/%s/' % (uve_type)
            uve_links =\
                [obj_to_dict(LinkObject(uve,
                                        base_url + uve + "?" + uve_filters))
                 for uve in uve_list]
            return json.dumps(uve_links)
    # end uve_list_http_get

    def analytics_http_get(self):
        # common handling for all resource get
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            abort(code, msg)

        base_url = bottle.request.urlparts.scheme + '://' + \
            bottle.request.urlparts.netloc + '/analytics/'
        analytics_links = [obj_to_dict(LinkObject(link, base_url + link))
                           for link in self._analytics_links]
        return json.dumps(analytics_links)
    # end analytics_http_get

    def uves_http_get(self):
        # common handling for all resource get
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            abort(code, msg)

        base_url = bottle.request.urlparts.scheme + '://' + \
            bottle.request.urlparts.netloc + '/analytics/uves/'
        uvetype_links =\
            [obj_to_dict(
             LinkObject(uvetype + 's', base_url + uvetype + 's'))
                for uvetype in UVE_MAP]
        return json.dumps(uvetype_links)
    # end uves_http_get

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

    def tables_process(self):
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            abort(code, msg)

        base_url = bottle.request.urlparts.scheme + '://' + \
            bottle.request.urlparts.netloc + '/analytics/table/'
        json_links = []
        for i in range(0, len(self._VIRTUAL_TABLES)):
            link = LinkObject(self._VIRTUAL_TABLES[
                              i].name, base_url + self._VIRTUAL_TABLES[i].name)
            tbl_info = obj_to_dict(link)
            tbl_info['type'] = self._VIRTUAL_TABLES[i].schema.type
            if (self._VIRTUAL_TABLES[i].display_name is not None):
                    tbl_info['display_name'] =\
                        self._VIRTUAL_TABLES[i].display_name
            json_links.append(tbl_info)

        return json.dumps(json_links)
    # end tables_process

    def table_process(self, table):
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            abort(code, msg)

        base_url = bottle.request.urlparts.scheme + '://' + \
            bottle.request.urlparts.netloc + '/analytics/table/' + table + '/'

        json_links = []
        for i in range(0, len(self._VIRTUAL_TABLES)):
            if (self._VIRTUAL_TABLES[i].name == table):
                link = LinkObject('schema', base_url + 'schema')
                json_links.append(obj_to_dict(link))
                if len(self._VIRTUAL_TABLES[i].columnvalues) > 0:
                    link = LinkObject(
                        'column-values', base_url + 'column-values')
                    json_links.append(obj_to_dict(link))
                break

        return json.dumps(json_links)
    # end table_process

    def table_schema_process(self, table):
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            abort(code, msg)

        for i in range(0, len(self._VIRTUAL_TABLES)):
            if (self._VIRTUAL_TABLES[i].name == table):
                return json.dumps(self._VIRTUAL_TABLES[i].schema,
                                  default=lambda obj: obj.__dict__)

        return (json.dumps({}))
    # end table_schema_process

    def column_values_process(self, table):
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            abort(code, msg)

        base_url = bottle.request.urlparts.scheme + '://' + \
            bottle.request.urlparts.netloc + \
            '/analytics/table/' + table + '/column-values/'

        json_links = []
        for i in range(0, len(self._VIRTUAL_TABLES)):
            if (self._VIRTUAL_TABLES[i].name == table):
                for col in self._VIRTUAL_TABLES[i].columnvalues:
                    link = LinkObject(col, base_url + col)
                    json_links.append(obj_to_dict(link))
                break

        return (json.dumps(json_links))
    # end column_values_process

    def generator_info(self, table, column):
        if ((column == MODULE) or (column == SOURCE)):
            sources = []
            moduleids = []
            for redis_uve in self.redis_uve_list:
                redish = redis.StrictRedis(
                    db=1,
                    host=redis_uve[0],
                    port=redis_uve[1])
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
            for t in _STAT_TABLES:
                stat_table = STAT_VT_PREFIX + "." + \
                    t.stat_type + "." + t.stat_attr
                if (table == stat_table):
                    objtab = t.obj_table
                    break
            if (objtab != None) and (objtab != "None"): 
            #import pdb; pdb.set_trace()
                return list(self._uve_server.get_uve_list(objtab,
                        None, None, None, None, False))

        return []
    # end generator_info

    def column_process(self, table, column):
        (ok, result) = self._get_common(bottle.request)
        if not ok:
            (code, msg) = result
            abort(code, msg)

        for i in range(0, len(self._VIRTUAL_TABLES)):
            if (self._VIRTUAL_TABLES[i].name == table):
                if self._VIRTUAL_TABLES[i].columnvalues.count(column) > 0:
                    return (json.dumps(self.generator_info(table, column)))

        return (json.dumps([]))
    # end column_process

    def start_uve_server(self):
        self._uve_server.run()
    #end start_uve_server

    def start_webserver(self):
        pipe_start_app = bottle.app()
        bottle.run(app=pipe_start_app, host=self._args.rest_api_ip,
                   port=self._args.rest_api_port, server='gevent')
    # end start_webserver

    def cpu_info_logger(self):
        opserver_cpu_info = CpuInfoData()
        while True:
            mod_cpu_info = ModuleCpuInfo()
            mod_cpu_info.module_id = self._moduleid
            mod_cpu_info.instance_id = self._instance_id
            mod_cpu_info.cpu_info = opserver_cpu_info.get_cpu_info(
                system=False)
            mod_cpu_state = ModuleCpuState()
            mod_cpu_state.name = self._hostname

            # At some point, the following attributes will be deprecated in favor of cpu_info
            mod_cpu_state.module_cpu_info = [mod_cpu_info]
            mod_cpu_state.opserver_cpu_share = mod_cpu_info.cpu_info.cpu_share
            mod_cpu_state.opserver_mem_virt =\
                mod_cpu_info.cpu_info.meminfo.virt

            opserver_cpu_state_trace = ModuleCpuStateTrace(data=mod_cpu_state)
            opserver_cpu_state_trace.send()

            aly_cpu_state = AnalyticsCpuState()
            aly_cpu_state.name = self._hostname

            aly_cpu_info = ProcessCpuInfo()
            aly_cpu_info.module_id= self._moduleid
            aly_cpu_info.inst_id = self._instance_id
            aly_cpu_info.cpu_share = mod_cpu_info.cpu_info.cpu_share
            aly_cpu_info.mem_virt = mod_cpu_info.cpu_info.meminfo.virt
            aly_cpu_state.cpu_info = [aly_cpu_info]

            aly_cpu_state_trace = AnalyticsCpuStateTrace(data=aly_cpu_state)
            aly_cpu_state_trace.send()

            gevent.sleep(60)
    #end cpu_info_logger

    def poll_collector_list(self):
        '''
        Analytics node may be brought up/down any time. For UVE aggregation,
        Opserver needs to know the list of all Analytics nodes (redis-uves).
        Presently, Discovery server supports only pull mechanism to get the
        Publisher list. Periodically poll the Collector list [in lieu of 
        redi-uve nodes] from the discovery. 
        ** Remove this code when the push mechanism to update the discovery clients
        on the addition/deletion of Publisher nodes for a given service is 
        supported by the Discovery server.
        '''
        if self.disc:
            while True:
                self.redis_uve_list = []
                try:
                    sub_obj = \
                        self.disc.subscribe(ModuleNames[Module.COLLECTOR], 0)
                    collectors = sub_obj.info 
                except Exception as e:
                    self._logger.error('Failed to get collector-list from ' \
                                       'discovery server')
                else:
                    if collectors:
                        disc_trace = CollectorTrace()
                        disc_trace.collectors = []
                        for collector in collectors:
                            self.redis_uve_list.append((collector['ip-address'], 
                                                       self._args.redis_server_port))
                            disc_trace.collectors.append(collector['ip-address'])
                        disc_trace.trace_msg(name='DiscoveryMsg')
                        self._uve_server.update_redis_uve_list(self.redis_uve_list)
                        self._state_server.update_redis_list(self.redis_uve_list)
                if self.redis_uve_list:
                    gevent.sleep(60)
                else:
                    gevent.sleep(5)
    # end poll_collector_list

def main():
    opserver = OpServer()
    gevent.joinall([
        gevent.spawn(opserver.start_webserver),
        gevent.spawn(opserver.cpu_info_logger),
        gevent.spawn(opserver.start_uve_server),
        gevent.spawn(opserver.poll_collector_list)
    ])

if __name__ == '__main__':
    main()
