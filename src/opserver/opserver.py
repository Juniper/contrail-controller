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
import bottle
import json
import uuid
import argparse
import time
import redis
import base64
import sys
import socket
import struct
import errno
import copy

from pysandesh.sandesh_base import *
from pysandesh.sandesh_session import SandeshWriter
from pysandesh.gen_py.sandesh_trace.ttypes import SandeshTraceRequest
from sandesh.mirror.ttypes import *
from sandesh.vns.ttypes import Module, Category
from sandesh.vns.constants import ModuleNames, CategoryNames, ModuleCategoryMap
from sandesh.viz.constants import _TABLES, _OBJECT_TABLES,\
    _OBJECT_TABLE_SCHEMA, _OBJECT_TABLE_COLUMN_VALUES
from sandesh.viz.constants import *
from sandesh.analytics_cpuinfo.ttypes import *
from opserver_util import OpServerUtils
from cpuinfo import CpuInfoData

_ERRORS = {
    errno.EBADMSG: 400,
    errno.EINVAL: 404,
    errno.ENOENT: 410,
    errno.EIO: 500
}


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

    k, res = redish.blpop("REPLY:" + qid, 10)

    if k is None or res is None:
        return None
    # Put the status back on the queue for the use of the status URI
    redish.lpush("REPLY:" + qid, res)

    resp = json.loads(res)
    return int(resp["progress"])
# end redis_query_start


def redis_query_status(host, port, qid):
    redish = redis.StrictRedis(db=0, host=host, port=port)
    resp = {"progress": 0}
    chunks = []
    # For now, the number of chunks will be always 1
    res = redish.lrange("REPLY:" + qid, -1, -1)
    chunk_resp = json.loads(res[0])
    ttl = redish.ttl("REPLY:" + qid)
    if int(ttl) != -1:
        chunk_resp["ttl"] = int(ttl)
    query_time = redish.hmget("QUERY:" + qid, ["start_time", "end_time"])
    chunk_resp["start_time"] = int(query_time[0])
    chunk_resp["end_time"] = int(query_time[1])
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
    except Exception as e:
        self._logger.error("Exception: %s" % e)
        yield bottle.HTTPError(status=_ERRORS[errno.ENOENT])
    else:
        if status['progress'] == 100:
            for chunk in status['chunks']:
                chunk_id = int(chunk['href'].rsplit('/', 1)[1])
                for gen in redis_query_chunk(host, port, qid, chunk_id):
                    yield gen
        else:
            yield {}
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

    def __init__(self, host, port):
        self._host = host
        self._port = port
        self._state = {}

    def redis_publisher_init(self):
        print 'Initialize Redis Publisher'
        self._redis_pub_client = redis.StrictRedis(
            host=self._host, port=int(self._port), db=0)

    def redis_publish(self, msg_type, destination, msg):
        # Get the sandesh encoded in XML format
        sandesh = SandeshWriter.encode_sandesh(msg)
        msg_encode = base64.b64encode(sandesh)
        redis_msg = '{"type":"%s","destination":"%s","message":"%s"}' \
            % (msg_type, destination, msg_encode)
        # Publish message in the Redis bus
        try:
            self._redis_pub_client.publish('analytics', redis_msg)
        except redis.exceptions.ConnectionError:
            print 'No Connection to Redis. Failed to publish message.'
            return False
        return True


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
                bottle.route(
                    '/analytics/table/<table>/column-values/<column>',
                    'GET', obj.column_process)
        bottle.route('/analytics/send-tracebuffer/<source>/<module>/<name>',
                     'GET', obj.send_trace_buffer)
        bottle.route(
            '/request-analyzer/<name>', 'GET', obj.analyzer_add_request)
        bottle.route('/delete-analyzer/<name>',
                     'GET', obj.analyzer_delete_request)
        bottle.route('/analyzers', 'GET', obj.analyzer_show_request)
        bottle.route(
            '/request-mirroring/<name>', 'GET', obj.mirror_add_request)
        bottle.route('/delete-mirroring/<name>',
                     'GET', obj.mirror_delete_request)
        bottle.route('/mirrors', 'GET', obj.mirror_show_request)

        bottle.route('/documentation/<filename:path>',
                     'GET', obj.documentation_http_get)

        for uve in UVE_MAP:
            bottle.route(
                '/analytics/uves/' + uve + 's', 'GET', obj.uve_list_http_get)
            bottle.route(
                '/analytics/uves/' + uve + '/<name>', 'GET', obj.uve_http_get)

        return obj
    # end __new__

    def disc_publish(self):
        try:
            import discovery.client as client
        except:
            raise Exception('Could not get Discovery Client')
        else:
            ipaddr = socket.gethostbyname(socket.gethostname())
            data = {
                'ip-address': ipaddr,
                'port': self._args.rest_api_port,
            }
            disc = client.DiscoveryClient(
                self._args.disc_server_ip,
                self._args.disc_server_port,
                ModuleNames[Module.OPSERVER])
            self._logger.info("Disc Publish to %s : %d - %s"
                              % (self._args.disc_server_ip,
                                 self._args.disc_server_port, str(data)))
            disc.publish(self._moduleid, data)
    # end

    def __init__(self):
        self._args = None
        self._parse_args()

        self._homepage_links = []
        self._homepage_links.append(
            LinkObject('documentation', '/documentation/index.html'))
        self._homepage_links.append(LinkObject('analytics', '/analytics'))

        super(OpServer, self).__init__()
        self._moduleid = ModuleNames[Module.OPSERVER]
        self._hostname = socket.gethostname()
        collectors = None
        if self._args.collector and self._args.collector_port:
            collectors = [
                (self._args.collector, int(self._args.collector_port))]
        sandesh_global.init_generator(self._moduleid, self._hostname,
                                      collectors, 'opserver_context',
                                      int(self._args.http_server_port),
                                      ['sandesh'])
        sandesh_global.set_logging_params(
            enable_local_log=self._args.log_local,
            category=self._args.log_category,
            level=self._args.log_level,
            file=self._args.log_file)

        self._logger = sandesh_global._logger
        self._get_common = self._http_get_common
        self._put_common = self._http_put_common
        self._delete_common = self._http_delete_common
        self._post_common = self._http_post_common

        self._collector_pool = None
        self._state_server = None

        self._LEVEL_LIST = []
        for k in SandeshLevel._VALUES_TO_NAMES:
            if (k < SandeshLevel.UT_START):
                d = {}
                d[k] = SandeshLevel._VALUES_TO_NAMES[k]
                self._LEVEL_LIST.append(d)
        self._CATEGORY_MAP =\
            dict((ModuleNames[k], [CategoryNames[ce] for ce in v])
                 for k, v in ModuleCategoryMap.iteritems())

        if self._args.disc_server_ip:
            self.disc_publish()

        self._analytics_links = ['uves', 'tables', 'queries']

        self._VIRTUAL_TABLES = copy.deepcopy(_TABLES)

        for t in _OBJECT_TABLES:
            obj = query_table(
                name=t, display_name=_OBJECT_TABLES[t].objtable_display_name,
                schema=_OBJECT_TABLE_SCHEMA,
                columnvalues=_OBJECT_TABLE_COLUMN_VALUES)
            self._VIRTUAL_TABLES.append(obj)

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
                bottle.route('/analytics/table/<table>/column-values', 'GET',
                             self.column_values_process)
                bottle.route(
                    '/analytics/table/<table>/column-values/<column>', 'GET',
                    self.column_process)
        bottle.route('/analytics/send-tracebuffer/<source>/<module>/<name>',
                     'GET', self.send_trace_buffer)
        bottle.route(
            '/request-analyzer/<name>', 'GET', self.analyzer_add_request)
        bottle.route('/delete-analyzer/<name>',
                     'GET', self.analyzer_delete_request)
        bottle.route('/analyzers', 'GET', self.analyzer_show_request)
        bottle.route(
            '/request-mirroring/<name>', 'GET', self.mirror_add_request)
        bottle.route('/delete-mirroring/<name>',
                     'GET', self.mirror_delete_request)
        bottle.route('/mirrors', 'GET', self.mirror_show_request)

        bottle.route('/documentation/<filename:path>',
                     'GET', self.documentation_http_get)

        for uve in UVE_MAP:
            bottle.route(
                '/analytics/uves/' + uve + 's', 'GET', self.uve_list_http_get)
            bottle.route(
                '/analytics/uves/' + uve + '/<name>', 'GET', self.uve_http_get)
    # end __init__

    def _parse_args(self):
        '''
        Eg. python opserver.py --cassandra_server_ip 127.0.0.1
                               --cassandra_server_port 9160
                               --redis_server_ip 10.1.1.1
                               --redis_server_port 6381
                               --redis_query_port 6380
                               --collector 127.0.0.1
                               --collector_port 8086
                               --http_server_port 8090
                               --rest_api_port 8081
                               --rest_api_ip 0.0.0.0
                               --log_local
                               --log_level SYS_DEBUG
                               --log_category test
                               --log_file <stdout>
        '''

        parser = argparse.ArgumentParser(
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)

        parser.add_argument("--redis_server_ip",
                            default='127.0.0.1',
                            help="Redis server IP address")
        parser.add_argument("--redis_server_port",
                            type=int,
                            default=6381,
                            help="Redis server port")
        parser.add_argument("--redis_query_port",
                            type=int,
                            default=6380,
                            help="Redis query port")
        parser.add_argument("--collector",
                            default='127.0.0.1',
                            help="Collector IP address")
        parser.add_argument("--collector_port",
                            type=int,
                            default=8086,
                            help="Collector port")
        parser.add_argument("--http_server_port",
                            type=int,
                            default=8090,
                            help="HTTP server port")
        parser.add_argument("--rest_api_port",
                            type=int,
                            default=8081,
                            help="REST API port")
        parser.add_argument("--rest_api_ip",
                            default='0.0.0.0',
                            help="REST API IP address")
        parser.add_argument("--log_local", action="store_true",
                            default=False,
                            help="Enable local logging of sandesh messages")
        parser.add_argument(
            "--log_level", default='SYS_DEBUG',
            help="Severity level for local logging of sandesh messages")
        parser.add_argument(
            "--log_category", default='',
            help="Category filter for local logging of sandesh messages")
        parser.add_argument("--log_file",
                            default=Sandesh._DEFAULT_LOG_FILE,
                            help="Filename for the logs to be written to")
        parser.add_argument("--disc_server_ip",
                            default=None,
                            help="Discovery Server IP address")
        parser.add_argument("--disc_server_port",
                            type=int,
                            default=5998,
                            help="Discovery Server port")

        self._args = parser.parse_args()
    # end _parse_args

    def get_args(self):
        return self._args
    # end get_args

    def get_collector(self):
        return self._args.collector
    # end get_collector

    def get_collector_port(self):
        return int(self._args.collector_port)
    # end get_collector_port

    def get_http_server_port(self):
        return int(self._args.http_server_port)
    # end get_http_server_port

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

    def _query_status(self, request, qid):
        resp = {}
        try:
            resp = redis_query_status(host='127.0.0.1',
                                      port=int(self._args.redis_query_port),
                                      qid=qid)
        except Exception as e:
            self._logger.error("Exception: %s" % e)
            return bottle.HTTPError(status=_ERRORS[errno.ENOENT])
        else:
            resp_header = {'Content-Type': 'application/json'}
            resp_code = 200
            self._logger.debug("query [%s] status: %s" % (qid, resp))
            return bottle.HTTPResponse(
                json.dumps(resp), resp_code, resp_header)
    # end _query_status

    def _query_chunk(self, request, qid, chunk_id):
        try:
            done = False
            gen = redis_query_chunk(host='127.0.0.1',
                                    port=int(self._args.redis_query_port),
                                    qid=qid, chunk_id=chunk_id)
            bottle.response.set_header('Content-Type', 'application/json')
            while not done:
                try:
                    yield gen.next()
                except StopIteration:
                    done = True
        except Exception as e:
            self._logger.error("Exception: %s" % str(e))
            yield bottle.HTTPError(status=_ERRORS[errno.ENOENT])
        else:
            self._logger.info(
                "Query [%s] chunk #%d read at time %d"
                % (qid, chunk_id, time.time()))
    # end _query_chunk

    def _query(self, request):
        reply = {}
        try:
            remote_addr, = struct.unpack(
                'I',
                socket.inet_pton(socket.AF_INET, bottle.request.remote_addr))
            qid = str(uuid.uuid1(socket.ntohl(remote_addr)))
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

            if (tabn is None):
                reply = bottle.HTTPError(status=_ERRORS[errno.ENOENT])
                yield reply
                raise Exception('Table %s not found' % tabl)

            tabtypes = {}
            for cols in self._VIRTUAL_TABLES[tabn].schema.columns:
                if cols.datatype in ['long', 'int']:
                    tabtypes[cols.name] = 'int'
                elif cols.datatype in ['ipv4']:
                    tabtypes[cols.name] = 'ipv4'
                else:
                    tabtypes[cols.name] = 'string'

            self._logger.info(str(tabtypes))
            prg = redis_query_start('127.0.0.1',
                                    int(self._args.redis_query_port),
                                    qid, request.json)
            if prg is None:
                raise Exception('QE Not Responding')

        except Exception as e:
            self._logger.error("Exception: %s" % str(e))

        finally:
            redish = None
            if prg < 0:
                cod = -prg
                self._logger.error(
                    "Query Failed. Found Error %s" % errno.errorcode[cod])
                reply = bottle.HTTPError(status=501)
                reply = bottle.HTTPError(status=_ERRORS[cod])
                yield reply
                return
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
                reply = bottle.HTTPError(status=501)
                reply = bottle.HTTPError(status=_ERRORS[cod])
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

        except Exception as e:
            self._logger.error("Exception: %s" % str(e))
        else:
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
            for query_id in processing_queries:
                status = redis_query_status(host='127.0.0.1',
                                            port=int(
                                                self._args.redis_query_port),
                                            qid=query_id)
                query_data = redis_query_info(redish, query_id)
                query_data['progress'] = status['progress']
                processing_queries_info.append(query_data)
            queries['queries_being_processed'] = processing_queries_info
        except Exception as err:
            self._logger.error("Exception in show queries: %s" % str(err))
            return {}
        else:
            return json.dumps(queries)
    # end show_queries

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
            tfilter = {}
            infos = req.cfilt.split(',')
            for tfilt in infos:
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
        if 'kfilt' in req.keys():
            any_filter = True
            kfilter = req.kfilt.split(',')

        return any_filter, kfilter, sfilter, mfilter, tfilter

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

    def analyzer_init(self):
        # TODO: Analyzer table and mirror table should be stored in
        # cassandra/Redis for persistency
        self._analyzer_tbl = {}
        self._mirror_tbl = {}
    # end analyzer_init

    def analyzer_add_request(self, name):
        response = {'status': 'pass'}
        try:
            analyzer = self._analyzer_tbl[name]
        except KeyError:
            self._analyzer_tbl[name] = {
                'ip': socket.gethostbyname(socket.getfqdn()),
                'udp_port': 8099,
                'vnc_port': 5941,
                'mirrors': []
            }
            analyzer = self._analyzer_tbl[name]
        finally:
            # TODO: Need to pass appropriate values
            response['vnc'] = 'http://%s:%s' % (
                analyzer['ip'], analyzer['vnc_port'])
            return json.dumps(response)
    # end analyzer_add_request

    def analyzer_delete_request(self, name):
        response = {'status': 'fail'}
        try:
            analyzer = self._analyzer_tbl[name]
        except KeyError:
            print 'Analyzer "%s" not present' % (name)
            response['error'] = 'Analyzer not present'
            return json.dumps(response)
        else:
            if len(analyzer['mirrors']):
                response['error'] = 'Mirror list not empty'
                return json.dumps(response)
            del self._analyzer_tbl[name]
            response['status'] = 'pass'
            return json.dumps(response)
    # end analyzer_delete_request

    def analyzer_show_request(self):
        req = bottle.request.query
        if req.name:
            try:
                analyzer = self._analyzer_tbl[req.name]
            except KeyError:
                bottle.abort(404, 'Analyzer "%s" not present' % (req.name))
            else:
                link = 'http://%s:%s' % (analyzer['ip'], analyzer['vnc_port'])
                analyzer_info = {'name': req.name, 'vnc': link,
                                 'mirrors': analyzer['mirrors']}
                response = {'analyzer': analyzer_info}
                return json.dumps(response)
        else:
            analyzer_list = []
            for analyzer_name, analyzer in self._analyzer_tbl.iteritems():
                link = 'http://%s:%s' % (analyzer['ip'], analyzer['vnc_port'])
                analyzer_list.append({'name': analyzer_name, 'vnc': link})
            response = {'analyzers': analyzer_list}
            return json.dumps(response)
    # end analyzer_show_request

    def mirror_add_request(self, name):
        response = {'status': 'fail'}
        req = bottle.request.query
        if not req.apply_vn or not req.analyzer_name:
            print 'Invalid mirror add request'
            response[
                'error'] = '"apply_vn" and/or "analyzer_name" not specified'
            return json.dumps(response)
        try:
            mirror_info = self._mirror_tbl[name]
        except KeyError:
            mirror_info = {}
        else:
            print 'Mirror [%s] already present' % (name)
            response['error'] = 'Mirror already present'
            return json.dumps(response)
        mirror_req = MirrorCreateReq()
        mirror_req.handle = name
        mirror_req.apply_vn = req.apply_vn
        try:
            analyzer = self._analyzer_tbl[req.analyzer_name]
        except KeyError:
            print 'Invalid analyzer'
            response['error'] = 'Invalid analyzer'
            return json.dumps(response)
        else:
            mirror_req.ip = analyzer['ip']
            mirror_req.udp_port = analyzer['udp_port']

        mirror_info['apply_vn'] = mirror_req.apply_vn
        mirror_info['analyzer_name'] = req.analyzer_name
        if req.src_vn:
            mirror_req.src_vn = req.src_vn
            mirror_info['src_vn'] = req.src_vn
        if req.src_ip_prefix:
            mirror_req.src_ip_prefix = req.src_ip_prefix
            mirror_info['src_ip_prefix'] = req.src_ip_prefix
        if req.src_ip_prefix_len:
            try:
                mirror_req.src_ip_prefix_len = int(req.src_ip_prefix_len)
            except ValueError:
                print 'Invalid src ip prefix length [%s]'\
                    % (req.src_ip_prefix_len)
                response['error'] = 'Invalid source ip prefix length'
                return json.dumps(response)
            else:
                mirror_info['src_ip_prefix_len'] = mirror_req.src_ip_prefix_len
        if req.dst_vn:
            mirror_req.dst_vn = req.dst_vn
            mirror_info['dst_vn'] = req.dst_vn
        if req.dst_ip_prefix:
            mirror_req.dst_ip_prefix = req.dst_ip_prefix
            mirror_info['dst_ip_prefix'] = req.dst_ip_prefix
        if req.dst_ip_prefix_len:
            try:
                mirror_req.dst_ip_prefix_len = int(req.dst_ip_prefix_len)
            except ValueError:
                print 'Invalid dst ip prefix length [%s]'\
                    % (req.dst_ip_prefix_len)
                response['error'] = 'Invalid destination ip prefix length'
                return json.dumps(response)
            else:
                mirror_info['dst_ip_prefix_len'] = mirror_req.dst_ip_prefix_len
        if req.start_src_port:
            try:
                mirror_req.start_src_port = int(req.start_src_port)
            except ValueError:
                print 'Invalid start src port [%s]' % (req.start_src_port)
                response['error'] = 'Invalid start source port'
                return json.dumps(response)
            else:
                mirror_info['start_src_port'] = mirror_req.start_src_port
                if not req.end_src_port:
                    mirror_req.end_src_port = mirror_req.start_src_port
        if req.end_src_port:
            try:
                mirror_req.end_src_port = int(req.end_src_port)
            except ValueError:
                print 'Invalid end src port [%s]' % (req.end_src_port)
                response['error'] = 'Invalid end source port'
                return json.dumps(response)
            else:
                mirror_info['end_src_port'] = mirror_req.end_src_port
        if req.start_dst_port:
            try:
                mirror_req.start_dst_port = int(req.start_dst_port)
            except ValueError:
                print 'Invalid start dst port [%s]' % (req.start_dst_port)
                response['error'] = 'Invalid start destination port'
                return json.dumps(response)
            else:
                mirror_info['start_dst_port'] = mirror_req.start_dst_port
                if not req.end_dst_port:
                    mirror_req.end_dst_port = mirror_req.start_dst_port
        if req.end_dst_port:
            try:
                mirror_req.end_dst_port = int(req.end_dst_port)
            except ValueError:
                print 'Invalid end dst port [%s]' % (req.end_dst_port)
                response['error'] = 'Invalid end destination port'
                return json.dumps(response)
            else:
                mirror_info['end_dst_port'] = mirror_req.end_dst_port
        if req.protocol:
            try:
                mirror_req.protocol = int(req.protocol)
            except ValueError:
                print 'Invalid protocol [%s]' % (req.protocol)
                response['error'] = 'Invalid protocol'
                return json.dumps(response)
            else:
                mirror_info['protocol'] = mirror_req.protocol
        if req.time_period:
            try:
                mirror_req.time_period = int(req.time_period)
            except ValueError:
                print 'Invalid time period [%s]' % (req.time_period)
                response['error'] = 'Invalid time period'
                return json.dumps(response)
            else:
                mirror_info['time_period'] = mirror_req.time_period

        # TODO: Get the appropriate destination, if the src_ip/dst_ip
        # [based on ingress/egress mirror] is provided.
        # If the prefix_len is not 32, then set the "destination source"
        # to "*". If the src_ip/dst_ip is not provided, set the
        # "destination source" to "*".
        # We may want to optimize this by specifying the list of destinations
        # which hosts# the VMs in the specified VN.
        # destination format is "source:module", where source is the hostname
        dest = '*:' + ModuleNames[Module.VROUTER_AGENT]
        if self._state_server.redis_publish(
                msg_type='Request', destination=dest, msg=mirror_req):
            print 'Successfully published mirror add [%s] request' % (name)
            self._mirror_tbl[name] = mirror_info
            analyzer['mirrors'].append({'name': name})
            self._analyzer_tbl[req.analyzer_name] = analyzer
            response['status'] = 'pass'
            return json.dumps(response)
        print 'Failed to publish mirror add [%s] request' % (name)
        response['error'] = 'Failed to publish mirror add request'
        return json.dumps(response)
    # end mirror_add_request

    def mirror_delete_request(self, name):
        response = {'status': 'fail'}
        try:
            mirror = self._mirror_tbl[name]
        except KeyError:
            print 'Mirror "%s" not present' % (name)
            response['error'] = 'Mirror not present'
            return json.dumps(response)
        mirror_req = MirrorDeleteReq()
        mirror_req.handle = name
        dest = '*:' + ModuleNames[Module.VROUTER_AGENT]
        if self._state_server.redis_publish(msg_type='Request',
                                            destination=dest, msg=mirror_req):
            print 'Successfully published mirror delete [%s] request' % (name)
            analyzer_name = self._mirror_tbl[name]['analyzer_name']
            try:
                analyzer = self._analyzer_tbl[analyzer_name]
            except KeyError:
                pass
            else:
                analyzer['mirrors'].remove({'name': name})
                self._analyzer_tbl[analyzer_name] = analyzer

            del self._mirror_tbl[name]
            response['status'] = 'pass'
            return json.dumps(response)
        print 'Failed to publish mirror delete [%s] request' % (name)
        response['error'] = 'Failed to publish mirror delete request'
        return json.dumps(response)
    # end mirror_delete_request

    def mirror_show_request(self):
        req = bottle.request.query
        if req.name:
            try:
                mirror_info = self._mirror_tbl[req.name]
            except KeyError:
                bottle.abort(404, 'Mirror "%s" not present' % (req.name))
            else:
                mirror_info['name'] = req.name
                response = {'mirror': mirror_info}
                return json.dumps(response)
        else:
            mirror_list = []
            for mirror_name, mirror_info in self._mirror_tbl.iteritems():
                mirror_info['name'] = mirror_name
                mirror_list.append(mirror_info)
            response = {'mirrors': mirror_list}
            return json.dumps(response)
    # end mirror_show_request

    def send_trace_buffer(self, source, module, name):
        response = {}
        trace_req = SandeshTraceRequest(name)
        if self._state_server.redis_publish(msg_type='send-tracebuffer',
                                            destination=source + ':' + module,
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
        if ((column == 'ModuleId') or (column == 'Source')):
            try:
                redish = redis.StrictRedis(
                    db=0, host=self._args.redis_server_ip,
                    port=int(self._args.redis_server_port))
                sources = []
                moduleids = []
                for key in redish.smembers("GENERATORS"):
                    source = key.split(':')[0]
                    module = key.split(':')[1]
                    if (sources.count(source) == 0):
                        sources.append(source)
                    if (moduleids.count(module) == 0):
                        moduleids.append(module)
                if (column == 'ModuleId'):
                    return moduleids
                elif (column == 'Source'):
                    return sources
            except Exception as e:
                print e
        elif (column == 'Category'):
            return self._CATEGORY_MAP
        elif (column == 'Level'):
            return self._LEVEL_LIST

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

    def start_state_server(self):
        self._state_server = OpStateServer(
            self._args.redis_server_ip, self._args.redis_server_port)
        self._state_server.redis_publisher_init()
    # end start_state_server

    def start_uve_server(self):
        self._uve_server = UVEServer(
            self._args.redis_server_ip, self._args.redis_server_port)
        self._uve_server.run()
    # end start_uve_server

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
            mod_cpu_info.cpu_info = opserver_cpu_info.get_cpu_info(
                system=False)
            mod_cpu_state = ModuleCpuState()
            mod_cpu_state.name = self._hostname
            mod_cpu_state.module_cpu_info = [mod_cpu_info]
            mod_cpu_state.opserver_cpu_share = mod_cpu_info.cpu_info.cpu_share
            mod_cpu_state.opserver_mem_virt =\
                mod_cpu_info.cpu_info.meminfo.virt
            opserver_cpu_state_trace = ModuleCpuStateTrace(data=mod_cpu_state)
            opserver_cpu_state_trace.send()
            gevent.sleep(60)
    # end cpu_info_logger

# end class OpServer


def main():
    opserver = OpServer()
    opserver.start_state_server()
    opserver.analyzer_init()
    gevent.joinall([
        gevent.spawn(opserver.start_uve_server),
        gevent.spawn(opserver.start_webserver),
        gevent.spawn(opserver.cpu_info_logger)
    ])

if __name__ == '__main__':
    main()
