#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Layer that transforms VNC config objects to ifmap representation
"""
from cfgm_common.zkclient import ZookeeperClient, IndexAllocator
from gevent import ssl, monkey
monkey.patch_all()
import gevent
import gevent.event
from gevent.queue import Queue
import sys
import time
from pprint import pformat

from lxml import etree, objectify
import cgitb
import StringIO
import re

import socket
import errno
import subprocess
import netaddr
from bitarray import bitarray

from cfgm_common import ignore_exceptions
from cfgm_common.ifmap.client import client, namespaces
from cfgm_common.ifmap.request import NewSessionRequest, RenewSessionRequest,\
    EndSessionRequest, PublishRequest, SearchRequest, SubscribeRequest,\
    PurgeRequest, PollRequest
from cfgm_common.ifmap.id import IPAddress, MACAddress, Device,\
    AccessRequest, Identity, CustomIdentity
from cfgm_common.ifmap.operations import PublishUpdateOperation,\
    PublishNotifyOperation, PublishDeleteOperation, SubscribeUpdateOperation,\
    SubscribeDeleteOperation
from cfgm_common.ifmap.util import attr, link_ids
from cfgm_common.ifmap.response import Response, newSessionResult
from cfgm_common.ifmap.metadata import Metadata
from cfgm_common import obj_to_json
from cfgm_common.exceptions import ResourceExhaustionError, ResourceExistsError

import copy
import json
import uuid
import datetime
import pycassa
import pycassa.util
import pycassa.cassandra.ttypes
from pycassa.system_manager import *
from datetime import datetime
from pycassa.util import *
from pycassa.pool import AllServersUnavailable

import amqp.exceptions
import kombu
import signal, os


#from cfgm_common import vnc_type_conv
from provision_defaults import *
import cfgm_common.imid
from cfgm_common.exceptions import *
from vnc_quota import *
from gen.vnc_ifmap_client_gen import *
from gen.vnc_cassandra_client_gen import *
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus, \
    ConnectionType

from sandesh.traces.ttypes import DBRequestTrace, MessageBusNotifyTrace, \
    IfmapTrace

import logging
logger = logging.getLogger(__name__)

@ignore_exceptions
def get_trace_id():
    try:
        req_id = gevent.getcurrent().trace_request_id
    except Exception:
        req_id = 'req-%s' %(str(uuid.uuid4()))
        gevent.getcurrent().trace_request_id = req_id

    return req_id
# end get_trace_id

@ignore_exceptions
def trace_msg(trace_obj, trace_name, sandesh_hdl, error_msg=None):
    if trace_obj:
        if error_msg:
            trace_obj.error = error_msg
        trace_obj.trace_msg(name=trace_name, sandesh=sandesh_hdl)
# end trace_msg

class VncIfmapClient(VncIfmapClientGen):

    def handler(self, signum, frame):
        file = open("/tmp/api-server-ifmap-cache.txt", "w")
        file.write(str(self._id_to_metas))
        file.close()

    def __init__(self, db_client_mgr, ifmap_srv_ip, ifmap_srv_port,
                 uname, passwd, ssl_options, ifmap_srv_loc=None):
        super(VncIfmapClient, self).__init__()
        # TODO username/passwd from right place
        self._CONTRAIL_XSD = "http://www.contrailsystems.com/vnc_cfg.xsd"
        self._IPERMS_NAME = "id-perms"
        self._IPERMS_FQ_NAME = "contrail:" + self._IPERMS_NAME
        self._SUBNETS_NAME = "contrail:subnets"
        self._IPAMS_NAME = "contrail:ipams"
        self._SG_RULE_NAME = "contrail:sg_rules"
        self._POLICY_ENTRY_NAME = "contrail:policy_entry"

        self._NAMESPACES = {
            'env':   "http://www.w3.org/2003/05/soap-envelope",
            'ifmap':   "http://www.trustedcomputinggroup.org/2010/IFMAP/2",
            'meta':
            "http://www.trustedcomputinggroup.org/2010/IFMAP-METADATA/2",
            'contrail':   self._CONTRAIL_XSD
        }

        self._db_client_mgr = db_client_mgr
        self._sandesh = db_client_mgr._sandesh

        ConnectionState.update(conn_type = ConnectionType.IFMAP,
            name = 'IfMap', status = ConnectionStatus.INIT, message = '',
            server_addrs = ["%s:%s" % (ifmap_srv_ip, ifmap_srv_port)])

        # launch mapserver
        if ifmap_srv_loc:
            self._launch_mapserver(ifmap_srv_ip, ifmap_srv_port, ifmap_srv_loc)

        # Cache of metas populated in ifmap server. Useful in update to find
        # what things to remove in ifmap server
        self._id_to_metas = {}
        self.accumulator = None
        self.accumulated_request_len = 0
        # Set the signal handler
        signal.signal(signal.SIGUSR2, self.handler)

        mapclient = client(("%s" % (ifmap_srv_ip), "%s" % (ifmap_srv_port)),
                           uname, passwd, self._NAMESPACES, ssl_options)
        self._mapclient = mapclient

        connected = False
        while not connected:
            try:
                result = mapclient.call('newSession', NewSessionRequest())
                connected = True
            except socket.error as e:
                time.sleep(3)

        ConnectionState.update(conn_type = ConnectionType.IFMAP,
            name = 'IfMap', status = ConnectionStatus.UP, message = '',
            server_addrs = ["%s:%s" % (ifmap_srv_ip, ifmap_srv_port)])

        mapclient.set_session_id(newSessionResult(result).get_session_id())
        mapclient.set_publisher_id(newSessionResult(result).get_publisher_id())

        # Initialize ifmap-id handler (alloc|convert|parse etc.)
        self._imid_handler = Imid()
        imid = self._imid_handler

        # Publish init config (TODO this should come from api-server init)
        # config-root
        buf = cStringIO.StringIO()
        perms = Provision.defaults.perms['config-root']
        perms.exportChildren(buf, level=1, pretty_print=False)
        id_perms_xml = buf.getvalue()
        buf.close()
        update = {}
        meta = Metadata(self._IPERMS_NAME, '',
                        {'ifmap-cardinality': 'singleValue'},
                        ns_prefix='contrail', elements=id_perms_xml)
        self._update_id_self_meta(update, meta)
        self._publish_update("contrail:config-root:root", update)
    # end __init__

    def get_imid_handler(self):
        return self._imid_handler
    # end get_imid_handler

    # Parse ifmap-server returned search results and create list of tuples
    # of (ident-1, ident-2, link-attribs)
    def parse_result_items(self, srch_result, my_imid):
        xpath_expr = \
            '/env:Envelope/env:Body/ifmap:response/searchResult/resultItem'
        result_items = self._parse(srch_result, xpath_expr)

        return cfgm_common.imid.parse_result_items(result_items, my_imid)
    # end parse_result_items

    # In list of (ident-1, ident-2, link-attribs) tuples, return list of
    # ifmap-ids of other idents
    def get_others_in_result_list(self, result_list, my_imid):
        other_imid_list = []
        for result_elem in result_list:
            ident_1, ident_2, meta = result_elem
            if (ident_1 is None) or (ident_2 is None):
                continue

            other_imid = None
            if ident_1.attrib['name'] == my_imid:
                other_imid = ident_2.attrib['name']
            elif ident_2.attrib['name'] == my_imid:
                other_imid = ident_1.attrib['name']

            other_imid_list.append(other_imid)

        return other_imid_list
    # end get_others_in_result_list

    def _ensure_port_not_listened(self, server_ip, server_port):
        try:
            s = socket.create_connection((server_ip, server_port))
            s.close()
            print "IP %s port %s already listened on"\
                % (server_ip, server_port)
        except Exception as err:
            if err.errno == errno.ECONNREFUSED:
                return  # all is well
    # end _ensure_port_not_listened

    def _block_till_port_listened(self, server_name, server_ip, server_port):
        svr_running = False
        while not svr_running:
            try:
                s = socket.create_connection((server_ip, server_port))
                s.close()
                svr_running = True
            except Exception as err:
                if err.errno == errno.ECONNREFUSED:
                    print "%s not up, retrying in 2 secs" % (server_name)
                    time.sleep(2)
                else:
                    raise err
    # end _block_till_port_listened

    # launch ifmap server
    def _launch_mapserver(self, ifmap_srv_ip, ifmap_srv_port, ifmap_srv_loc):
        print 'Starting IFMAP server ...'
        self._ensure_port_not_listened(ifmap_srv_ip, ifmap_srv_port)
        logf_out = open('ifmap-server.out', 'w')
        logf_err = open('ifmap-server.err', 'w')
        self._mapserver = subprocess.Popen(['java', '-jar', 'build/irond.jar'],
                                           cwd=ifmap_srv_loc, stdout=logf_out,
                                           stderr=logf_err)
        self._block_till_port_listened(
            'ifmap-server', ifmap_srv_ip, ifmap_srv_port)
    # end _launch_mapserver

    @ignore_exceptions
    def _generate_ifmap_trace(self, oper, body):
        req_id = get_trace_id()
        ifmap_trace = IfmapTrace(request_id=req_id)
        ifmap_trace.operation = oper
        ifmap_trace.body = body

        return ifmap_trace
    # end _generate_ifmap_trace

    def _publish_with_trace(self, oper, oper_body, async):
        trace = self._generate_ifmap_trace(oper, oper_body)
        if async:
            method = getattr(self._mapclient, 'call_async_result')
        else:
            method = getattr(self._mapclient, 'call')

        try:
            sess_id = self._mapclient.get_session_id()
            method('publish', PublishRequest(sess_id, oper_body))
            trace_msg(trace, 'IfmapTraceBuf', self._sandesh)
        except Exception as e:
            trace_msg(trace, 'IfmapTraceBuf', self._sandesh, error_msg=str(e))
            log_str = 'Failed to publish %s body %s to ifmap: %s' %(oper,
                oper_body, str(e))
            logger.error(log_str)
            self._db_client_mgr.config_log_error(log_str)
            raise
    # end _publish_with_trace

    def _delete_id_self_meta(self, self_imid, meta_name):
        mapclient = self._mapclient

        del_str = str(PublishDeleteOperation(
                          id1=str(Identity(
                                  name=self_imid,
                                  type="other",
                                  other_type="extended")),
                          filter=meta_name))

        self._publish_with_trace('delete', del_str, async=False)

        # del meta from cache and del id if this was last meta
        if meta_name:
            prop_name = meta_name.replace('contrail:', '')
            del self._id_to_metas[self_imid][prop_name]
            if not self._id_to_metas[self_imid]:
                del self._id_to_metas[self_imid]
        else:
            del self._id_to_metas[self_imid]
    # end _delete_id_self_meta

    def _delete_id_pair_meta(self, id1, id2, metadata):
        mapclient = self._mapclient

        del_str = str(PublishDeleteOperation(
                      id1=str(Identity(
                              name=id1,
                              type="other",
                              other_type="extended")),
                      id2=str(Identity(
                              name=id2,
                              type="other",
                              other_type="extended")),
                      filter=metadata))

        self._publish_with_trace('delete', del_str, async=False)

        # del meta,id2 from cache and del id if this was last meta
        def _id_to_metas_delete(id1, id2, meta_name):
            if meta_name not in self._id_to_metas[id1]:
                return
            if not self._id_to_metas[id1][meta_name]:
                del self._id_to_metas[id1][meta_name]
                if not self._id_to_metas[id1]:
                    del self._id_to_metas[id1]
                return

            # if meta is prop, noop
            if 'id' not in self._id_to_metas[id1][meta_name][0]:
                return
            self._id_to_metas[id1][meta_name] = \
                 [{'id':m['id'], 'meta':m['meta']} \
                 for m in self._id_to_metas[id1][meta_name] if m['id'] != id2]

        if metadata:
            meta_name = metadata.replace('contrail:', '')
            # replace with remaining refs
            for (id_x, id_y) in [(id1, id2), (id2, id1)]:
                _id_to_metas_delete(id_x, id_y, meta_name)
        else: # no meta specified remove all links from id1 to id2
            for (id_x, id_y) in [(id1, id2), (id2, id1)]:
                meta_names = self._id_to_metas.get(id_x, {}).keys()
                for meta_name in meta_names:
                    _id_to_metas_delete(id_x, id_y, meta_name)
    # end _delete_id_pair_meta

    def _update_id_self_meta(self, update, meta):
        """ update: dictionary of the type
                update[<id> | 'self'] = list(metadata)
        """
        if 'self' in update:
            mlist = update['self']
        else:
            mlist = []
            update['self'] = mlist

        mlist.append(meta)
    # end _update_id_self_meta

    def _update_id_pair_meta(self, update, to_id, meta):
        if to_id in update:
            mlist = update[to_id]
        else:
            mlist = []
            update[to_id] = mlist
        mlist.append(meta)
     # end _update_id_pair_meta

    def _publish_update(self, self_imid, update):
        if self_imid not in self._id_to_metas:
            self._id_to_metas[self_imid] = {}

        def _build_request_id_self(imid, metalist):
            request = ''
            for m in metalist:
                request += str(PublishUpdateOperation(
                        id1=str(Identity(name=self_imid, type="other",
                                         other_type="extended")),
                        metadata=str(m),
                        lifetime='forever'))
            return request

        def _build_request_id_pair(id1, id2, metalist):
            request = ''
            for m in metalist:
                request += str(PublishUpdateOperation(
                    id1=str(Identity(name=id1, type="other",
                                     other_type="extended")),
                    id2=str(Identity(name=id2, type="other",
                                     other_type="extended")),
                    metadata=str(m),
                    lifetime='forever'))
            return request

        mapclient = self._mapclient
        requests = []
        if 'self' in update:
            metalist = update['self']
            requests.append(
                _build_request_id_self(self_imid, metalist))

            # remember what we wrote for diffing during next update
            for m in metalist:
                meta_name = m._Metadata__name.replace('contrail:', '')
                self._id_to_metas[self_imid][meta_name] = [{'meta':m}]

        for id2 in update:
            if id2 == 'self':
                continue
            metalist = update[id2]
            requests.append(
                _build_request_id_pair(self_imid, id2, metalist))

            # remember what we wrote for diffing during next update
            for m in metalist:
                meta_name = m._Metadata__name.replace('contrail:', '')
                if meta_name in self._id_to_metas[self_imid]:
                   self._id_to_metas[self_imid][meta_name].append({'meta':m,
                                                                  'id': id2})
                else:
                   self._id_to_metas[self_imid][meta_name] = [{'meta':m,
                                                               'id': id2}]

                if id2 not in self._id_to_metas:
                    self._id_to_metas[id2] = {}
                if meta_name in self._id_to_metas[id2]:
                   self._id_to_metas[id2][meta_name].append({'meta':m,
                                                             'id': self_imid})
                else:
                   self._id_to_metas[id2][meta_name] = [{'meta':m,
                                                         'id': self_imid}]
        if self.accumulator is not None:
            self.accumulator.append(requests)
            self.accumulated_request_len += len(requests)
            if self.accumulated_request_len >= 1024*1024:
                upd_str = \
                    ''.join(''.join(request) for request in \
                        self._ifmap_db.accumulator)
                self._publish_with_trace('update', upd_str, async=True)
                self.accumulator = []
                self.accumulated_request_len = 0
        else:
            upd_str = ''.join(requests)
            self._publish_with_trace('update', upd_str, async=True)
    # end _publish_update

    def _search(self, start_id, match_meta=None, result_meta=None,
                max_depth=1):
        # set ifmap search parmeters
        srch_params = {}
        srch_params['max-depth'] = str(max_depth)

        if match_meta is not None:
            srch_params['match-links'] = match_meta
        if result_meta is not None:
            # all => don't set result-filter, so server returns all id + meta
            if result_meta == "all":
                pass
            else:
                srch_params['result-filter'] = result_meta
        else:
            # default to return match_meta metadata types only
            srch_params['result-filter'] = match_meta

        mapclient = self._mapclient
        srch_req = SearchRequest(mapclient.get_session_id(), start_id,
                                 search_parameters=srch_params
                                 )
        result = mapclient.call('search', srch_req)

        return result
    # end _search

    def _parse(self, srch_result, xpath_expr):
        soap_doc = etree.parse(StringIO.StringIO(srch_result))
        result_items = soap_doc.xpath(xpath_expr,
                                      namespaces=self._NAMESPACES)

        return result_items
    # end _parse

    def _search_and_parse(self, start_id, xpath_expr,
                          match_meta=None, result_meta=None, max_depth=0):
        result = self._search(start_id, match_meta, result_meta, max_depth)
        result_items = self._parse(result, xpath_expr)

        return result_items
    # end _search_and_parse

    def _get_id_meta_refs(self, result_items, self_type, parent_type):
        # Given parsed result items from search, returns # of idents + metadata
        # referring to this ident (incl self + parent). In addition, parent's
        # name and names of non-parent, non-self idents referring to this ident
        # are returned. TODO should this be moved to cfgm/common
        ref_cnt = 0
        ref_set = set()
        ref_names = ""
        parent_imid = ""
        imid = self._imid_handler
        for r_item in result_items:
            if r_item.tag == 'identity':
                ident_name = r_item.attrib['name']
                ident_type = cfgm_common.imid.ifmap_id_to_type(ident_name)
                # No action if already encountered
                if ident_name in ref_set:
                    continue

                ref_cnt = ref_cnt + 1
                ref_set.add(ident_name)
                if (ident_type == self_type):
                    continue
                if (ident_type == parent_type):
                    parent_imid = r_item.attrib['name']
                    continue

                # non-parent, non-self refs
                ref_names = "%s %s" % (ref_names, ident_name)
            elif r_item.tag == 'metadata':
                # TBI figure out meta only belonging to self
                ref_cnt = ref_cnt + 1
                meta_elem = r_item.getchildren()[0]
                meta_name = re.sub("{.*}", "", meta_elem.tag)
                ref_names = "%s %s" % (ref_names, meta_name)

        return ref_cnt, parent_imid, ref_names
    # end _get_id_meta_refs

    def fq_name_to_ifmap_id(self, obj_type, fq_name):
        return cfgm_common.imid.get_ifmap_id_from_fq_name(obj_type, fq_name)
    # end fq_name_to_ifmap_id

    def ifmap_id_to_fq_name(self, ifmap_id):
        return cfgm_common.imid.get_fq_name_from_ifmap_id(ifmap_id)
    # end ifmap_id_to_fq_name

# end class VncIfmapClient


class Imid(ImidGen):
    pass
# end class Imid


class VncCassandraClient(VncCassandraClientGen):
    # Name to ID mapping keyspace + tables
    _UUID_KEYSPACE_NAME = 'config_db_uuid'

    # TODO describe layout
    _OBJ_UUID_CF_NAME = 'obj_uuid_table'

    # TODO describe layout
    _OBJ_FQ_NAME_CF_NAME = 'obj_fq_name_table'

    # has obj uuid as rowkey;  ascii as column type; <fq_name>, <ifmap_id>
    # <obj_json> <child_cf_col_name> as column values
    _UUID_CF_NAME = 'uuid_table'

    # has type:fq_name as rowkey; ascii as column type; <obj uuid> <ifmap_id>
    # as column values
    _FQ_NAME_CF_NAME = 'fq_name_table'

    # has ifmap_id as rowkey; ascii as column type
    # <obj uuid>, <fq_name> as column values
    # ifmap_id itself is contrail:<type>:<fq-name delimited by ':'>
    _IFMAP_ID_CF_NAME = 'ifmap_id_table'

    # has obj uuid:<child-type> as rowkey; timeuuid column type; <child obj
    # uuid> as column values
    _CHILDREN_CF_NAME = 'children_table'

    _SUBNET_CF_NAME = 'subnet_bitmask_table'

    # Useragent datastore keyspace + tables (used by quantum plugin currently)
    _USERAGENT_KEYSPACE_NAME = 'useragent'
    _USERAGENT_KV_CF_NAME = 'useragent_keyval_table'

    def __init__(self, db_client_mgr, cass_srv_list, reset_config, db_prefix):
        super(VncCassandraClient, self).__init__()
        self._db_client_mgr = db_client_mgr
        self._reset_config = reset_config
        self._cache_uuid_to_fq_name = {}
        if db_prefix:
            self._db_prefix = '%s_' %(db_prefix)
        else:
            self._db_prefix = ''
        self._server_list = cass_srv_list
        self._cassandra_init(cass_srv_list)
    # end __init__

    def _log_exceptions(self, func):
        def wrapper(*args, **kwargs):
            try:
                return func(*args, **kwargs)
            except AllServersUnavailable:
                ConnectionState.update(conn_type = ConnectionType.DATABASE,
                    name = 'Cassandra', status = ConnectionStatus.DOWN,
                    message = '', server_addrs = self._server_list)
                raise
        return wrapper
    # end _log_exceptions

    # Helper routines for cassandra
    def _cassandra_init(self, server_list):
        # 1. Ensure keyspace and schema/CFs exist
        # 2. Read in persisted data and publish to ifmap server

        ConnectionState.update(conn_type = ConnectionType.DATABASE,
            name = 'Cassandra', status = ConnectionStatus.INIT, message = '',
            server_addrs = server_list)

        pycassa.ColumnFamily.get = self._log_exceptions(pycassa.ColumnFamily.get)
        pycassa.ColumnFamily.xget = self._log_exceptions(pycassa.ColumnFamily.xget)
        pycassa.ColumnFamily.get_range = self._log_exceptions(pycassa.ColumnFamily.get_range)
        pycassa.ColumnFamily.insert = self._log_exceptions(pycassa.ColumnFamily.insert)
        pycassa.ColumnFamily.remove = self._log_exceptions(pycassa.ColumnFamily.remove)
        pycassa.batch.Mutator.send = self._log_exceptions(pycassa.batch.Mutator.send)
        uuid_ks_name = '%s%s' %(self._db_prefix, VncCassandraClient._UUID_KEYSPACE_NAME)
        obj_uuid_cf_info = (VncCassandraClient._OBJ_UUID_CF_NAME, None)
        obj_fq_name_cf_info = (VncCassandraClient._OBJ_FQ_NAME_CF_NAME, None)
        uuid_cf_info = (VncCassandraClient._UUID_CF_NAME, None)
        fq_name_cf_info = (VncCassandraClient._FQ_NAME_CF_NAME, None)
        ifmap_id_cf_info = (VncCassandraClient._IFMAP_ID_CF_NAME, None)
        subnet_cf_info = (VncCassandraClient._SUBNET_CF_NAME, None)
        children_cf_info = (
            VncCassandraClient._CHILDREN_CF_NAME, TIME_UUID_TYPE)
        self._cassandra_ensure_keyspace(
            server_list, uuid_ks_name,
            [obj_uuid_cf_info, obj_fq_name_cf_info,
             uuid_cf_info, fq_name_cf_info, ifmap_id_cf_info,
             subnet_cf_info, children_cf_info])

        useragent_ks_name = '%s%s' %(self._db_prefix, VncCassandraClient._USERAGENT_KEYSPACE_NAME)
        useragent_kv_cf_info = (VncCassandraClient._USERAGENT_KV_CF_NAME, None)
        self._cassandra_ensure_keyspace(server_list, useragent_ks_name,
                                        [useragent_kv_cf_info])

        uuid_pool = pycassa.ConnectionPool(
            uuid_ks_name, server_list, max_overflow=-1,
            use_threadlocal=True, prefill=True, pool_size=20, pool_timeout=30,
            max_retries=-1, timeout=0.5)
        useragent_pool = pycassa.ConnectionPool(
            useragent_ks_name, server_list, max_overflow=-1,
            use_threadlocal=True, prefill=True, pool_size=20, pool_timeout=30,
            max_retries=-1, timeout=0.5)

        rd_consistency = pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM
        wr_consistency = pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM
        self._obj_uuid_cf = pycassa.ColumnFamily(
            uuid_pool, VncCassandraClient._OBJ_UUID_CF_NAME,
            read_consistency_level = rd_consistency,
            write_consistency_level = wr_consistency)
        self._obj_fq_name_cf = pycassa.ColumnFamily(
            uuid_pool, VncCassandraClient._OBJ_FQ_NAME_CF_NAME,
            read_consistency_level = rd_consistency,
            write_consistency_level = wr_consistency)

        self._useragent_kv_cf = pycassa.ColumnFamily(
            useragent_pool, VncCassandraClient._USERAGENT_KV_CF_NAME,
            read_consistency_level = rd_consistency,
            write_consistency_level = wr_consistency)
        self._subnet_cf = pycassa.ColumnFamily(
            uuid_pool, VncCassandraClient._SUBNET_CF_NAME,
            read_consistency_level = rd_consistency,
            write_consistency_level = wr_consistency)

        ConnectionState.update(conn_type = ConnectionType.DATABASE,
            name = 'Cassandra', status = ConnectionStatus.UP, message = '',
            server_addrs = server_list)

    # end _cassandra_init

    def _cassandra_ensure_keyspace(self, server_list,
                                   keyspace_name, cf_info_list):
        # Retry till cassandra is up
        server_idx = 0
        num_dbnodes = len(server_list)
        connected = False
        while not connected:
            try:
                cass_server = server_list[server_idx]
                sys_mgr = SystemManager(cass_server)
                connected = True
            except Exception as e:
                # TODO do only for
                # thrift.transport.TTransport.TTransportException
                server_idx = (server_idx + 1) % num_dbnodes
                time.sleep(3)

        if self._reset_config:
            try:
                sys_mgr.drop_keyspace(keyspace_name)
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                # TODO verify only EEXISTS
                print "Warning! " + str(e)

        try:
            sys_mgr.create_keyspace(keyspace_name, SIMPLE_STRATEGY,
                                    {'replication_factor': str(num_dbnodes)})
        except pycassa.cassandra.ttypes.InvalidRequestException as e:
            # TODO verify only EEXISTS
            print "Warning! " + str(e)

        for cf_info in cf_info_list:
            try:
                (cf_name, comparator_type) = cf_info
                if comparator_type:
                    sys_mgr.create_column_family(
                        keyspace_name, cf_name,
                        comparator_type=comparator_type)
                else:
                    sys_mgr.create_column_family(keyspace_name, cf_name)
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                # TODO verify only EEXISTS
                print "Warning! " + str(e)
    # end _cassandra_ensure_keyspace

    def _create_prop(self, bch, obj_uuid, prop_name, prop_val):
        bch.insert(obj_uuid, {'prop:%s' % (prop_name): json.dumps(prop_val)})
    # end _create_prop

    def _update_prop(self, bch, obj_uuid, prop_name, new_props):
        if new_props[prop_name] is None:
            bch.remove(obj_uuid, columns=['prop:' + prop_name])
        else:
            bch.insert(
                obj_uuid,
                {'prop:' + prop_name: json.dumps(new_props[prop_name])})

        # prop has been accounted for, remove so only new ones remain
        del new_props[prop_name]
    # end _update_prop

    def _create_child(self, bch, parent_type, parent_uuid,
                      child_type, child_uuid):
        child_col = {'children:%s:%s' %
                     (child_type, child_uuid): json.dumps(None)}
        bch.insert(parent_uuid, child_col)

        parent_col = {'parent:%s:%s' %
                      (parent_type, parent_uuid): json.dumps(None)}
        bch.insert(child_uuid, parent_col)
    # end _create_child

    def _read_child(self, result, obj_uuid, child_type,
                    child_uuid, child_tstamp):
        if '%ss' % (child_type) not in result:
            result['%ss' % (child_type)] = []

        child_info = {}
        child_info['to'] = self.uuid_to_fq_name(child_uuid)
        child_info['href'] = self._db_client_mgr.generate_url(
            child_type, child_uuid)
        child_info['uuid'] = child_uuid
        child_info['tstamp'] = child_tstamp

        result['%ss' % (child_type)].append(child_info)
    # end _read_child

    def _delete_child(self, bch, parent_type, parent_uuid,
                      child_type, child_uuid):
        child_col = {'children:%s:%s' %
                     (child_type, child_uuid): json.dumps(None)}
        bch.remove(parent_uuid, columns=[
                   'children:%s:%s' % (child_type, child_uuid)])
    # end _delete_child

    def _create_ref(self, bch, obj_type, obj_uuid, ref_type,
                    ref_uuid, ref_data):
        bch.insert(
            obj_uuid, {'ref:%s:%s' %
                  (ref_type, ref_uuid): json.dumps(ref_data)})
        if obj_type == ref_type:
            bch.insert(
                ref_uuid, {'ref:%s:%s' %
                      (obj_type, obj_uuid): json.dumps(ref_data)})
        else:
            bch.insert(
                ref_uuid, {'backref:%s:%s' %
                      (obj_type, obj_uuid): json.dumps(ref_data)})
    # end _create_ref

    def _read_ref(self, result, obj_uuid, ref_type, ref_uuid, ref_data_json):
        if '%s_refs' % (ref_type) not in result:
            result['%s_refs' % (ref_type)] = []

        ref_data = json.loads(ref_data_json)
        ref_info = {}
        try:
            ref_info['to'] = self.uuid_to_fq_name(ref_uuid)
        except NoIdError as e:
            ref_info['to'] = ['ERROR']

        if ref_data:
            try:
                ref_info['attr'] = ref_data['attr']
            except KeyError:
                # TODO remove backward compat old format had attr directly
                ref_info['attr'] = ref_data

        ref_info['href'] = self._db_client_mgr.generate_url(
            ref_type, ref_uuid)
        ref_info['uuid'] = ref_uuid

        result['%s_refs' % (ref_type)].append(ref_info)
    # end _read_ref

    def _read_back_ref(self, result, obj_uuid, back_ref_type,
                       back_ref_uuid, back_ref_data_json):
        if '%s_back_refs' % (back_ref_type) not in result:
            result['%s_back_refs' % (back_ref_type)] = []

        back_ref_info = {}
        back_ref_info['to'] = self.uuid_to_fq_name(back_ref_uuid)
        back_ref_data = json.loads(back_ref_data_json)
        if back_ref_data:
            try:
                back_ref_info['attr'] = back_ref_data['attr']
            except KeyError:
                # TODO remove backward compat old format had attr directly
                back_ref_info['attr'] = back_ref_data

        back_ref_info['href'] = self._db_client_mgr.generate_url(
            back_ref_type, back_ref_uuid)
        back_ref_info['uuid'] = back_ref_uuid

        result['%s_back_refs' % (back_ref_type)].append(back_ref_info)
    # end _read_back_ref

    def _update_ref(self, bch, obj_type, obj_uuid, ref_type,
                    old_ref_uuid, new_ref_infos):
        if ref_type not in new_ref_infos:
            # update body didn't touch this type, nop
            return

        if old_ref_uuid not in new_ref_infos[ref_type]:
            # remove old ref
            bch.remove(obj_uuid, columns=[
                       'ref:%s:%s' % (ref_type, old_ref_uuid)])
            if obj_type == ref_type:
                bch.remove(old_ref_uuid, columns=[
                           'ref:%s:%s' % (obj_type, obj_uuid)])
            else:
                bch.remove(old_ref_uuid, columns=[
                           'backref:%s:%s' % (obj_type, obj_uuid)])
                self._db_client_mgr.dbe_cache_invalidate({'uuid':
                                                         old_ref_uuid})
        else:
            # retain old ref with new ref attr
            new_ref_data = new_ref_infos[ref_type][old_ref_uuid]
            bch.insert(
                obj_uuid,
                {'ref:%s:%s' %
                 (ref_type, old_ref_uuid): json.dumps(new_ref_data)})
            if obj_type == ref_type:
                bch.insert(
                    old_ref_uuid,
                    {'ref:%s:%s' %
                     (obj_type, obj_uuid): json.dumps(new_ref_data)})
            else:
                bch.insert(
                    old_ref_uuid,
                    {'backref:%s:%s' %
                     (obj_type, obj_uuid): json.dumps(new_ref_data)})
                self._db_client_mgr.dbe_cache_invalidate({'uuid':
                                                         old_ref_uuid})
            # uuid has been accounted for, remove so only new ones remain
            del new_ref_infos[ref_type][old_ref_uuid]
    # end _update_ref

    def _delete_ref(self, bch, obj_type, obj_uuid, ref_type, ref_uuid):
        send = False
        if bch is None:
            send = True
            bch = self._cassandra_db._obj_uuid_cf.batch()
        bch.remove(obj_uuid, columns=['ref:%s:%s' % (ref_type, ref_uuid)])
        if obj_type == ref_type:
            bch.remove(ref_uuid, columns=[
                       'ref:%s:%s' % (obj_type, obj_uuid)])
        else:
            bch.remove(ref_uuid, columns=[
                       'backref:%s:%s' % (obj_type, obj_uuid)])
        if send:
            bch.send()
    # end _delete_ref

    def is_latest(self, id, tstamp):
        id_perms_json = self._obj_uuid_cf.get(
            id, columns=['prop:id_perms'])['prop:id_perms']
        id_perms = json.loads(id_perms_json)
        if id_perms['last_modified'] == tstamp:
            return True
        else:
            return False
    # end is_latest

    def cache_uuid_to_fq_name_add(self, id, fq_name):
        self._cache_uuid_to_fq_name[id] = fq_name
    # end cache_uuid_to_fq_name_add

    def cache_uuid_to_fq_name_del(self, id):
        try:
            del self._cache_uuid_to_fq_name[id]
        except KeyError:
            pass
    # end cache_uuid_to_fq_name_del

    def update_last_modified(self, bch, obj_uuid, id_perms=None):
        if id_perms is None:
            id_perms = json.loads(self._obj_uuid_cf.get(obj_uuid, ['prop:id_perms'])['prop:id_perms'])
        id_perms['last_modified'] = datetime.datetime.utcnow().isoformat()
        self._update_prop(bch, obj_uuid, 'id_perms', {'id_perms': id_perms})
    # end update_last_modified

    def uuid_to_fq_name(self, id):
        try:
            #TODO remove from cache on delete_notify
            return self._cache_uuid_to_fq_name[id]
        except KeyError:
            try:
                fq_name_json = self._obj_uuid_cf.get(
                    id, columns=['fq_name'])['fq_name']
            except pycassa.NotFoundException:
                raise NoIdError(id)

            fq_name = json.loads(fq_name_json)
            self.cache_uuid_to_fq_name_add(id, fq_name)
            return fq_name
    # end uuid_to_fq_name

    def uuid_to_obj_type(self, id):
        try:
            type_json = self._obj_uuid_cf.get(id, columns=['type'])['type']
        except pycassa.NotFoundException:
            raise NoIdError(id)
        return json.loads(type_json)
    # end uuid_to_fq_name

    def fq_name_to_uuid(self, obj_type, fq_name):
        method_name = obj_type.replace('-', '_')
        fq_name_str = ':'.join(fq_name)
        col_start = '%s:' % (fq_name_str)
        col_fin = '%s;' % (fq_name_str)
        try:
            col_info_iter = self._obj_fq_name_cf.xget(
                method_name, column_start=col_start, column_finish=col_fin)
        except pycassa.NotFoundException:
            raise NoIdError('%s %s' % (obj_type, fq_name))

        col_infos = list(col_info_iter)

        if len(col_infos) == 0:
            raise NoIdError('%s %s' % (obj_type, fq_name))

        for (col_name, col_val) in col_infos:
            obj_uuid = col_name.split(':')[-1]

        return obj_uuid
    # end fq_name_to_uuid

    def uuid_to_obj_dict(self, id):
        try:
            obj_cols = self._obj_uuid_cf.get(id)
        except pycassa.NotFoundException:
            raise NoIdError(id)
        return obj_cols
    # end uuid_to_obj_dict

    def uuid_to_obj_perms(self, id):
        try:
            id_perms_json = self._obj_uuid_cf.get(
                id, columns=['prop:id_perms'])['prop:id_perms']
            id_perms = json.loads(id_perms_json)
        except pycassa.NotFoundException:
            raise NoIdError(id)
        return id_perms
    # end uuid_to_obj_perms

    def useragent_kv_store(self, key, value):
        columns = {'value': value}
        self._useragent_kv_cf.insert(key, columns)
    # end useragent_kv_store

    def useragent_kv_retrieve(self, key):
        if key:
            try:
                columns = self._useragent_kv_cf.get(key)
            except pycassa.NotFoundException:
                raise NoUserAgentKey
            return columns['value']
        else:  # no key specified, return entire contents
            kv_list = []
            for ua_key, ua_cols in self._useragent_kv_cf.get_range():
                kv_list.append({'key': ua_key, 'value': ua_cols['value']})
            return kv_list
    # end useragent_kv_retrieve

    def useragent_kv_delete(self, key):
        self._useragent_kv_cf.remove(key)
    # end useragent_kv_delete

    def subnet_add_cols(self, subnet_fq_name, col_dict):
        self._subnet_cf.insert(subnet_fq_name, col_dict)
    # end subnet_add_cols

    def subnet_delete_cols(self, subnet_fq_name, col_names):
        self._subnet_cf.remove(subnet_fq_name, col_names)
    # end subnet_delete_cols

    def subnet_retrieve(self, subnet_fq_name):
        try:
            cols_iter = self._subnet_cf.xget(subnet_fq_name)
        except pycassa.NotFoundException:
            # ok to fail as not all subnets will have in-use addresses
            return None

        cols_dict = dict((k, v) for k, v in cols_iter)

        return cols_dict
    # end subnet_retrieve

    def subnet_delete(self, subnet_fq_name):
        try:
            self._subnet_cf.remove(subnet_fq_name)
        except pycassa.NotFoundException:
            # ok to fail as not all subnets will have bitmask allocated
            return None
    # end subnet_delete

    def walk(self, fn):
        walk_results = []
        for obj_uuid, _ in self._obj_uuid_cf.get_range():
            obj_cols_iter = self._obj_uuid_cf.xget(obj_uuid)
            obj_cols = dict((k, v) for k, v in obj_cols_iter)
            result = fn(obj_uuid, obj_cols)
            if result:
                walk_results.append(result)

        return walk_results
    # end walk
# end class VncCassandraClient


class VncKombuClient(object):
    def _init_server_conn(self, rabbit_ip, rabbit_port, rabbit_user, rabbit_password, rabbit_vhost,
                          delete_old_q=False):
        ConnectionState.update(conn_type = ConnectionType.DATABASE,
            name = 'RabbitMQ', status = ConnectionStatus.INIT, message = '',
            server_addrs = ["%s:%s" % (rabbit_ip, rabbit_port)])

        while True:
            try:
                self._conn = kombu.Connection(hostname=rabbit_ip,
                                              port=rabbit_port,
                                              userid=rabbit_user,
                                              password=rabbit_password,
                                              virtual_host=rabbit_vhost)

                ConnectionState.update(conn_type = ConnectionType.DATABASE,
                    name = 'RabbitMQ', status = ConnectionStatus.UP,
                    message = '',
                    server_addrs = ["%s:%s" % (rabbit_ip, rabbit_port)])

                if delete_old_q:
                    bound_q = self._update_queue_obj(self._conn.channel())
                    try:
                        bound_q.delete()
                    except amqp.exceptions.ChannelError as e:
                        logger.error("Unable to delete the old amqp Q: %s" % str(e))
                        pass

                self._obj_update_q = self._conn.SimpleQueue(self._update_queue_obj)

                old_subscribe_greenlet = self._dbe_oper_subscribe_greenlet
                self._dbe_oper_subscribe_greenlet = gevent.spawn(self._dbe_oper_subscribe)
                if old_subscribe_greenlet:
                    old_subscribe_greenlet.kill()

                break
            except Exception as e:
                print "Exception in _init_server_conn: %s" %(str(e))
                time.sleep(2)
    # end _init_server_conn

    def __init__(self, db_client_mgr, rabbit_ip, rabbit_port, ifmap_db, rabbit_user, rabbit_password, rabbit_vhost):
        self._db_client_mgr = db_client_mgr
        self._sandesh = db_client_mgr._sandesh
        self._ifmap_db = ifmap_db
        self._rabbit_ip = rabbit_ip
        self._rabbit_port = rabbit_port
        self._rabbit_user = rabbit_user
        self._rabbit_password = rabbit_password
        self._rabbit_vhost = rabbit_vhost

        obj_upd_exchange = kombu.Exchange('vnc_config.object-update', 'fanout',
                                          durable=False)

        listen_port = self._db_client_mgr.get_server_port()
        q_name = 'vnc_config.%s-%s' %(socket.gethostname(), listen_port)
        self._update_queue_obj = kombu.Queue(q_name, obj_upd_exchange)

        self._publish_queue = Queue()
        self._dbe_publish_greenlet = gevent.spawn(self._dbe_oper_publish)
        self._dbe_oper_subscribe_greenlet = None
        if self._rabbit_vhost == "__NONE__":
            return
        self._init_server_conn(self._rabbit_ip, self._rabbit_port, self._rabbit_user, self._rabbit_password, self._rabbit_vhost,
                               delete_old_q=True)
    # end __init__

    def _obj_update_q_put(self, oper_info):
        if self._rabbit_vhost == "__NONE__":
            return
        self._publish_queue.put(oper_info)
    # end _obj_update_q_put

    def _dbe_oper_publish(self):
        while True:
            try:
                message = self._publish_queue.get()
                while True:
                    try:
                        self._obj_update_q.put(message, serializer='json')
                        break
                    except Exception as e:
                        log_str = "Disconnected from rabbitmq. Reinitializing connection: %s" % str(e)
                        logger.warn(log_str)
                        self._db_client_mgr.config_log_error(log_str)
                        time.sleep(1)
                        self._init_server_conn(self._rabbit_ip, self._rabbit_port,
                            self._rabbit_user, self._rabbit_password, self._rabbit_vhost)
            except Exception as e:
                log_str = "Unknown exception in _dbe_oper_publish greenlet" + str(e)
                logger.error(log_str)
                self._db_client_mgr.config_log_error(log_str)
                time.sleep(1)
    # end _dbe_oper_publish

    def dbe_oper_publish_pending(self):
        return self._publish_queue.qsize()
    # end dbe_oper_publish_pending

    @ignore_exceptions
    def _generate_msgbus_notify_trace(self, oper_info):
        req_id = oper_info.get('request-id',
            'req-%s' %(str(uuid.uuid4())))
        gevent.getcurrent().trace_request_id = req_id

        notify_trace = MessageBusNotifyTrace(request_id=req_id)
        notify_trace.operation = oper_info.get('oper', '')
        notify_trace.body = json.dumps(oper_info)

        return notify_trace
    # end _generate_msgbus_notify_trace

    def _dbe_oper_subscribe(self):
        if self._rabbit_vhost == "__NONE__":
            return
        self._db_client_mgr.wait_for_resync_done()

        with self._conn.SimpleQueue(self._update_queue_obj) as queue:
            while True:
                try:
                    message = queue.get()
                except Exception as e:
                    logger.warn("Disconnected from rabbitmq. Reinitializing connection: %s" % str(e))
                    self._init_server_conn(self._rabbit_ip, self._rabbit_port, self._rabbit_user, self._rabbit_password, self._rabbit_vhost)
                    # never reached
                    continue

                trace = None
                try:
                    oper_info = message.payload

                    if logger.isEnabledFor(logging.DEBUG):
                        logger.debug("\nNotification Message: %s\n" %(pformat(oper_info)))
                    trace = self._generate_msgbus_notify_trace(oper_info)

                    if oper_info['oper'] == 'CREATE':
                        self._dbe_create_notification(oper_info)
                    if oper_info['oper'] == 'UPDATE':
                        self._dbe_update_notification(oper_info)
                    elif oper_info['oper'] == 'DELETE':
                        self._dbe_delete_notification(oper_info)

                    trace_msg(trace, 'MessageBusNotifyTraceBuf', self._sandesh)
                except Exception as e:
                    string_buf = cStringIO.StringIO()
                    cgitb.Hook(file=string_buf, format="text").handle(sys.exc_info())
                    errmsg = string_buf.getvalue()
                    logger.error("Exception in _dbe_oper_subscribe :\n%s" %(errmsg))
                    self._db_client_mgr.config_log_error(string_buf.getvalue())
                    trace_msg(trace, name='MessageBusNotifyTraceBuf',
                              sandesh=self._sandesh, error_msg=errmsg)
                finally:
                    try:
                        message.ack()
                    except Exception as e:
                        logger.warn("Disconnected from rabbitmq. Reinitializing connection: %s" % str(e))
                        self._init_server_conn(self._rabbit_ip, self._rabbit_port, self._rabbit_user, self._rabbit_password, self._rabbit_vhost)
                        # never reached

    #end _dbe_oper_subscribe

    def dbe_create_publish(self, obj_type, obj_ids, obj_dict):
        req_id = get_trace_id()
        oper_info = {'request-id': req_id,
                     'oper': 'CREATE',
                     'type': obj_type,
                     'obj_dict': obj_dict}
        oper_info.update(obj_ids)
        self._obj_update_q_put(oper_info)
    # end dbe_create_publish

    def _dbe_create_notification(self, obj_info):
        obj_dict = obj_info['obj_dict']

        try:
            r_class = self._db_client_mgr.get_resource_class(obj_info['type'])
            if r_class:
                r_class.dbe_create_notification(obj_info, obj_dict)
        except Exception as e:
            err_msg = "Failed to invoke type specific dbe_create_notification %s" %(str(e))
            self._db_client_mgr.config_log_error(err_msg)
            raise
        finally:
            method_name = obj_info['type'].replace('-', '_')
            method = getattr(self._ifmap_db, "_ifmap_%s_create" % (method_name))
            (ok, result) = method(obj_info, obj_dict)
            if not ok:
                raise Exception(result)
    #end _dbe_create_notification

    def dbe_update_publish(self, obj_type, obj_ids):
        oper_info = {'oper': 'UPDATE', 'type': obj_type}
        oper_info.update(obj_ids)
        self._obj_update_q_put(oper_info)
    # end dbe_update_publish

    def _dbe_update_notification(self, obj_info):
        (ok, result) = self._db_client_mgr.dbe_read(obj_info['type'], obj_info)
        if not ok:
            raise Exception(result)

        new_obj_dict = result

        try:
            r_class = self._db_client_mgr.get_resource_class(obj_info['type'])
            if r_class:
                r_class.dbe_update_notification(obj_info)
        except:
            self._db_client_mgr.config_log_error("Failed to invoke type specific dbe_update_notification")
            raise
        finally:
            ifmap_id = self._db_client_mgr.uuid_to_ifmap_id(obj_info['type'],
                                                            obj_info['uuid'])
            method_name = obj_info['type'].replace('-', '_')
            method = getattr(self._ifmap_db, "_ifmap_%s_update" % (method_name))
            (ok, ifmap_result) = method(ifmap_id, new_obj_dict)
            if not ok:
                raise Exception(ifmap_result)
    #end _dbe_update_notification

    def dbe_delete_publish(self, obj_type, obj_ids, obj_dict):
        oper_info = {'oper': 'DELETE', 'type': obj_type, 'obj_dict': obj_dict}
        oper_info.update(obj_ids)
        self._obj_update_q_put(oper_info)
    # end dbe_delete_publish

    def _dbe_delete_notification(self, obj_info):
        obj_dict = obj_info['obj_dict']

        db_client_mgr = self._db_client_mgr
        db_client_mgr._cassandra_db.cache_uuid_to_fq_name_del(obj_dict['uuid'])

        try:
            r_class = self._db_client_mgr.get_resource_class(obj_info['type'])
            if r_class:
                r_class.dbe_delete_notification(obj_info, obj_dict)
        except:
            db_client_mgr.config_log_error("Failed to invoke type specific dbe_delete_notification")
            raise
        finally:
            method_name = obj_info['type'].replace('-', '_')
            method = getattr(self._ifmap_db, "_ifmap_%s_delete" % (method_name))
            (ok, ifmap_result) = method(obj_info)
            if not ok:
                raise Exception(ifmap_result)
    #end _dbe_delete_notification

# end class VncKombuClient


class VncZkClient(object):
    _SUBNET_PATH = "/api-server/subnets"
    _FQ_NAME_TO_UUID_PATH = "/fq-name-to-uuid"
    _MAX_SUBNET_ADDR_ALLOC = 65535

    def __init__(self, instance_id, zk_server_ip, reset_config, db_prefix):
        self._db_prefix = db_prefix
        if db_prefix:
            client_pfx = db_prefix + '-'
            zk_path_pfx = db_prefix + '/'
        else:
            client_pfx = ''
            zk_path_pfx = ''

        client_name = client_pfx + 'api-' + instance_id
        self._subnet_path = zk_path_pfx + self._SUBNET_PATH
        self._fq_name_to_uuid_path = zk_path_pfx + self._FQ_NAME_TO_UUID_PATH

        while True:
            try:
                self._zk_client = ZookeeperClient(client_name, zk_server_ip)
                break
            except gevent.event.Timeout as e:
                pass

        if reset_config:
            self._zk_client.delete_node(self._subnet_path, True);
            self._zk_client.delete_node(self._fq_name_to_uuid_path, True);
        self._subnet_allocators = {}
    # end __init__

    def create_subnet_allocator(self, subnet, subnet_alloc_list,
                                addr_from_start):
        # TODO handle subnet resizing change, ignore for now
        if subnet not in self._subnet_allocators:
            if addr_from_start is None:
                addr_from_start = False
            self._subnet_allocators[subnet] = IndexAllocator(
                self._zk_client, self._subnet_path+'/'+subnet+'/',
                size=0, start_idx=0, reverse=not addr_from_start,
                alloc_list=subnet_alloc_list,
		max_alloc=self._MAX_SUBNET_ADDR_ALLOC)
    # end create_subnet_allocator

    def delete_subnet_allocator(self, subnet):
        self._subnet_allocators.pop(subnet, None)
        IndexAllocator.delete_all(self._zk_client,
                                  self._subnet_path+'/'+subnet+'/')
    # end delete_subnet_allocator

    def _get_subnet_allocator(self, subnet):
        return self._subnet_allocators.get(subnet)
    # end _get_subnet_allocator

    def subnet_is_addr_allocated(self, subnet, addr):
        allocator = self._get_subnet_allocator(subnet)
        return allocator.read(addr)
    # end subnet_is_addr_allocated

    def subnet_alloc_req(self, subnet, addr=None):
        allocator = self._get_subnet_allocator(subnet)
        try:
            if addr is not None:
                if allocator.read(addr) is not None:
                    return addr
                else:
                    return allocator.reserve(addr)
            else:
                return allocator.alloc()
        except ResourceExhaustionError:
            return None
    # end subnet_alloc_req

    def subnet_free_req(self, subnet, addr):
        allocator = self._get_subnet_allocator(subnet)
        if allocator:
            allocator.delete(addr)
    # end subnet_free_req

    def create_fq_name_to_uuid_mapping(self, obj_type, fq_name, id):
        fq_name_str = ':'.join(fq_name)
        zk_path = self._fq_name_to_uuid_path+'/%s:%s' %(obj_type.replace('-', '_'),
                                             fq_name_str)
        self._zk_client.create_node(zk_path, id)
    # end create_fq_name_to_uuid_mapping

    def delete_fq_name_to_uuid_mapping(self, obj_type, fq_name):
        fq_name_str = ':'.join(fq_name)
        zk_path = self._fq_name_to_uuid_path+'/%s:%s' %(obj_type.replace('-', '_'),
                                             fq_name_str)
        self._zk_client.delete_node(zk_path)
    # end delete_fq_name_to_uuid_mapping

    def is_connected(self):
        return self._zk_client.is_connected()
    # end is_connected

# end VncZkClient


class VncDbClient(object):
    def __init__(self, api_svr_mgr, ifmap_srv_ip, ifmap_srv_port, uname,
                 passwd, cass_srv_list,
                 rabbit_server, rabbit_port, rabbit_user, rabbit_password, rabbit_vhost,
                 reset_config=False, ifmap_srv_loc=None,
                 zk_server_ip=None, db_prefix=''):

        self._api_svr_mgr = api_svr_mgr
        self._sandesh = api_svr_mgr._sandesh

        # certificate auth
        ssl_options = None
        if api_svr_mgr._args.use_certs:
            ssl_options = {
                'keyfile': api_svr_mgr._args.keyfile,
                'certfile': api_svr_mgr._args.certfile,
                'ca_certs': api_svr_mgr._args.ca_certs,
                'cert_reqs': ssl.CERT_REQUIRED,
                'ciphers': 'ALL'
            }

        self._db_resync_done = gevent.event.Event()

        logger.info("connecting to ifmap on %s:%s as %s" % (ifmap_srv_ip, ifmap_srv_port, uname))

        self._ifmap_db = VncIfmapClient(
            self, ifmap_srv_ip, ifmap_srv_port,
            uname, passwd, ssl_options, ifmap_srv_loc)

        logger.info("connecting to cassandra on %s" % (cass_srv_list,))

        self._cassandra_db = VncCassandraClient(
            self, cass_srv_list, reset_config, db_prefix)

        self._msgbus = VncKombuClient(self, rabbit_server, rabbit_port, self._ifmap_db,
                                      rabbit_user, rabbit_password,
                                      rabbit_vhost)
        self._zk_db = VncZkClient(api_svr_mgr._args.worker_id, zk_server_ip,
                                  reset_config, db_prefix)
    # end __init__

    def _update_default_quota(self):
        """ Read the default quotas from the configuration
        and update it in the project object if not already
        updated.
        """
        default_quota = QuotaHelper.default_quota

        proj_id = self.fq_name_to_uuid('project',
                                       ['default-domain', 'default-project'])
        (ok, proj_dict) = self.dbe_read('project', {'uuid':proj_id})
        if not ok:
            return
        quota = QuotaType()

        proj_dict['quota'] = default_quota
        self.dbe_update('project', {'uuid':proj_id}, proj_dict)
    # end _update_default_quota

    def db_resync(self):
        # Read contents from cassandra and publish to ifmap
        mapclient = self._ifmap_db._mapclient
        self._ifmap_db.accumulator = []
        self._ifmap_db.accumulated_request_len = 0
        start_time = datetime.datetime.utcnow()
        self._cassandra_db.walk(self._dbe_resync)
        if self._ifmap_db.accumulated_request_len:
            upd_str = ''.join(''.join(request) \
                              for request in self._ifmap_db.accumulator)
            mapclient.call_async_result('publish',
                    PublishRequest(mapclient.get_session_id(), upd_str))
        self._ifmap_db.accumulator = None
        self._ifmap_db.accumulated_request_len = 0
        self._update_default_quota()
        end_time = datetime.datetime.utcnow()
        logger.info("Time elapsed in syncing ifmap: %s" % (str(end_time - start_time)))
        self._db_resync_done.set()
    # end db_resync

    def wait_for_resync_done(self):
        self._db_resync_done.wait()
    # end db_resync

    def db_check(self):
        # Read contents from cassandra and report any read exceptions
        check_results = self._cassandra_db.walk(self._dbe_check)

        return check_results
    # end db_check

    def db_read(self):
        # Read contents from cassandra
        read_results = self._cassandra_db.walk(self._dbe_read)
        return read_results
    # end db_check

    def _uuid_to_longs(self, id):
        msb_id = id.int >> 64
        lsb_id = id.int & ((1 << 64) - 1)
        return msb_id, lsb_id
    # end _uuid_to_longs

    def set_uuid(self, obj_type, obj_dict, id, persist=True):
        if persist:
            # set the mapping from name to uuid in zk to ensure single creator
            fq_name = obj_dict['fq_name']
            self._zk_db.create_fq_name_to_uuid_mapping(obj_type, fq_name, str(id))

        # set uuid in the perms meta
        mslong, lslong = self._uuid_to_longs(id)
        obj_dict['id_perms']['uuid'] = {}
        obj_dict['id_perms']['uuid']['uuid_mslong'] = mslong
        obj_dict['id_perms']['uuid']['uuid_lslong'] = lslong

        obj_dict['uuid'] = str(id)

        return True
    # end set_uuid

    def _alloc_set_uuid(self, obj_type, obj_dict):
        id = uuid.uuid4()
        ok = self.set_uuid(obj_type, obj_dict, id)

        return (ok, obj_dict['uuid'])
    # end _alloc_set_uuid

    def match_uuid(self, obj_dict, obj_uuid):
        new_mslong, new_lslong = self._uuid_to_longs(uuid.UUID(obj_uuid))
        old_mslong = obj_dict['id_perms']['uuid']['uuid_mslong']
        old_lslong = obj_dict['id_perms']['uuid']['uuid_lslong']
        if new_mslong == old_mslong and new_lslong == old_lslong:
            return True

        return False
    # end

    def _dbe_resync(self, obj_uuid, obj_cols):
        obj_type = None
        try:
            obj_type = json.loads(obj_cols['type'])
            method = getattr(self._cassandra_db,
                             "_cassandra_%s_read" % (obj_type))
            (ok, obj_dicts) = method([obj_uuid])
            obj_dict = obj_dicts[0]

            # TODO remove backward compat (use RT instead of VN->LR ref)
            if (obj_type == 'virtual_network' and
                'logical_router_refs' in obj_dict):
                for router in obj_dict['logical_router_refs']:
                    self._cassandra_db._delete_ref(None, obj_type, obj_uuid,
                                                   'logical_router',
                                                   router['uuid'])

        except Exception as e:
            self.config_object_error(
                obj_uuid, None, obj_type, 'dbe_resync:cassandra_read', str(e))
            return

        try:
            parent_type = obj_dict.get('parent_type', None)
            method = getattr(self._ifmap_db, "_ifmap_%s_alloc" % (obj_type))
            (ok, result) = method(parent_type, obj_dict['fq_name'])
            (my_imid, parent_imid) = result
        except Exception as e:
            self.config_object_error(
                obj_uuid, None, obj_type, 'dbe_resync:ifmap_alloc', str(e))
            return

        try:
            obj_ids = {'uuid': obj_uuid, 'imid': my_imid,
                       'parent_imid': parent_imid}
            method = getattr(self._ifmap_db, "_ifmap_%s_create" % (obj_type))
            (ok, result) = method(obj_ids, obj_dict)
        except Exception as e:
            self.config_object_error(
                obj_uuid, None, obj_type, 'dbe_resync:ifmap_create', str(e))
            return
    # end _dbe_resync

    def _dbe_check(self, obj_uuid, obj_cols):
        obj_type = None
        try:
            obj_type = json.loads(obj_cols['type'])
            method = getattr(self._cassandra_db,
                             "_cassandra_%s_read" % (obj_type))
            (ok, obj_dict) = method([obj_uuid])
        except Exception as e:
            return {'uuid': obj_uuid, 'type': obj_type, 'error': str(e)}
    # end _dbe_check

    def _dbe_read(self, obj_uuid, obj_cols):
        obj_type = None
        try:
            obj_type = json.loads(obj_cols['type'])
            method = getattr(self._cassandra_db,
                             "_cassandra_%s_read" % (obj_type))
            (ok, obj_dict) = method([obj_uuid])
            result_dict = obj_dict[0]
            result_dict['type'] = obj_type
            result_dict['uuid'] = obj_uuid
            return result_dict
        except Exception as e:
            return {'uuid': obj_uuid, 'type': obj_type, 'error': str(e)}
    # end _dbe_read

    @ignore_exceptions
    def _generate_db_request_trace(self, oper, obj_type, obj_ids, obj_dict):
        req_id = get_trace_id()

        body = dict(obj_dict)
        body['type'] = obj_type
        body.update(obj_ids)
        db_trace = DBRequestTrace(request_id=req_id)
        db_trace.operation = oper
        db_trace.body = json.dumps(body)
        return db_trace
    # end _generate_db_request_trace

    # Public Methods
    # Returns created ifmap_id
    def dbe_alloc(self, obj_type, obj_dict, uuid_requested=None):
        try:
            if uuid_requested:
                obj_uuid = uuid_requested
                ok = self.set_uuid(obj_type, obj_dict, uuid.UUID(uuid_requested), False)
            else:
                (ok, obj_uuid) = self._alloc_set_uuid(obj_type, obj_dict)
        except ResourceExistsError as e:
            return (409, str(e))

        parent_type = obj_dict.get('parent_type', None)
        method_name = obj_type.replace('-', '_')
        method = getattr(self._ifmap_db, "_ifmap_%s_alloc" % (method_name))
        (ok, result) = method(parent_type, obj_dict['fq_name'])
        if not ok:
            self.dbe_release(obj_type, obj_dict['fq_name'])
            return False, result

        (my_imid, parent_imid) = result
        obj_ids = {
            'uuid': obj_dict['uuid'],
            'imid': my_imid, 'parent_imid': parent_imid}

        return (True, obj_ids)
    # end dbe_alloc

    def dbe_trace(oper):
        def wrapper1(func):
            def wrapper2(self, obj_type, obj_ids, obj_dict):
                trace = self._generate_db_request_trace(oper, obj_type,
                                                        obj_ids, obj_dict)
                try:
                    ret = func(self, obj_type, obj_ids, obj_dict)
                    trace_msg(trace, 'DBRequestTraceBuf',
                              self._sandesh)
                    return ret
                except Exception as e:
                    trace_msg(trace, 'DBRequestTraceBuf',
                              self._sandesh, error_msg=str(e))
                    raise

            return wrapper2
        return wrapper1
    # dbe_trace

    @dbe_trace('create')
    def dbe_create(self, obj_type, obj_ids, obj_dict):
        method_name = obj_type.replace('-', '_')
        method = getattr(
            self._cassandra_db, "_cassandra_%s_create" % (method_name))
        (ok, result) = method(obj_ids, obj_dict)

        # publish to ifmap via msgbus
        self._msgbus.dbe_create_publish(obj_type, obj_ids, obj_dict)

        return (ok, result)
    # end dbe_create

    # input id is ifmap-id + uuid
    def dbe_read(self, obj_type, obj_ids, obj_fields=None):
        method_name = obj_type.replace('-', '_')
        method = getattr(
            self._cassandra_db, "_cassandra_%s_read" % (method_name))
        try:
            (ok, cassandra_result) = method([obj_ids['uuid']], obj_fields)
        except NoIdError as e:
            return (False, str(e))

        return (ok, cassandra_result[0])
    # end dbe_read

    def dbe_read_multi(self, obj_type, obj_ids_list, obj_fields=None):
        method_name = obj_type.replace('-', '_')
        method = getattr(
            self._cassandra_db, "_cassandra_%s_read" % (method_name))
        try:
            (ok, cassandra_result) = method([obj_id['uuid']
                                                for obj_id in obj_ids_list],
                                            obj_fields)
        except NoIdError as e:
            return (False, str(e))

        return (ok, cassandra_result)
    # end dbe_read_multi


    def dbe_is_latest(self, obj_ids, tstamp):
        try:
            is_latest = self._cassandra_db.is_latest(obj_ids['uuid'], tstamp)
            return (True, is_latest)
        except Exception as e:
            return (False, str(e))
    # end dbe_is_latest

    @dbe_trace('update')
    def dbe_update(self, obj_type, obj_ids, new_obj_dict):
        method_name = obj_type.replace('-', '_')
        method = getattr(self._cassandra_db,
                         "_cassandra_%s_update" % (method_name))
        (ok, cassandra_result) = method(obj_ids['uuid'], new_obj_dict)

        # publish to ifmap via redis
        self._msgbus.dbe_update_publish(obj_type, obj_ids)

        return (ok, cassandra_result)
    # end dbe_update

    def dbe_list(self, obj_type, parent_uuids=None, back_ref_uuids=None,
                 obj_uuids=None, count=False):
        method_name = obj_type.replace('-', '_')
        method = getattr(
            self._cassandra_db, "_cassandra_%s_list" % (method_name))
        (ok, cassandra_result) = method(parent_uuids=parent_uuids,
                                        back_ref_uuids=back_ref_uuids,
                                        obj_uuids=obj_uuids,
                                        count=count)
        return (ok, cassandra_result)
    # end dbe_list

    @dbe_trace('delete')
    def dbe_delete(self, obj_type, obj_ids, obj_dict):
        method_name = obj_type.replace('-', '_')
        method = getattr(
            self._cassandra_db, "_cassandra_%s_delete" % (method_name))
        (ok, cassandra_result) = method(obj_ids['uuid'])

        # publish to ifmap via redis
        self._msgbus.dbe_delete_publish(obj_type, obj_ids, obj_dict)

        # finally remove mapping in zk
        fq_name = cfgm_common.imid.get_fq_name_from_ifmap_id(obj_ids['imid'])
        self.dbe_release(obj_type, fq_name)

        return ok, cassandra_result
    # end dbe_delete

    def dbe_release(self, obj_type, obj_fq_name):
        self._zk_db.delete_fq_name_to_uuid_mapping(obj_type, obj_fq_name)
    # end dbe_release

    def dbe_cache_invalidate(self, obj_ids):
        pass
    # end dbe_cache_invalidate

    def dbe_oper_publish_pending(self):
        return self._msgbus.dbe_oper_publish_pending()
    # end dbe_oper_publish_pending

    def useragent_kv_store(self, key, value):
        self._cassandra_db.useragent_kv_store(key, value)
    # end useragent_kv_store

    def useragent_kv_retrieve(self, key):
        return self._cassandra_db.useragent_kv_retrieve(key)
    # end useragent_kv_retrieve

    def useragent_kv_delete(self, key):
        return self._cassandra_db.useragent_kv_delete(key)
    # end useragent_kv_delete

    def subnet_is_addr_allocated(self, subnet, addr):
        return self._zk_db.subnet_is_addr_allocated(subnet, addr)
    # end subnet_is_addr_allocated

    def subnet_alloc_req(self, subnet, addr=None):
        return self._zk_db.subnet_alloc_req(subnet, addr)
    # end subnet_alloc_req

    def subnet_free_req(self, subnet, addr):
        self._zk_db.subnet_free_req(subnet, addr)
    # end subnet_free_req

    def subnet_create_allocator(self, subnet, subnet_alloc_list,
                                addr_from_start):
        self._zk_db.create_subnet_allocator(subnet, subnet_alloc_list,
                                            addr_from_start)
    # end subnet_create_allocator

    def subnet_delete_allocator(self, subnet):
        self._zk_db.delete_subnet_allocator(subnet)
    # end subnet_delete_allocator

    def uuid_vnlist(self):
        return self._cassandra_db.uuid_vnlist()
    # end uuid_vnlist

    def uuid_to_ifmap_id(self, obj_type, id):
        fq_name = self.uuid_to_fq_name(id)
        return self.fq_name_to_ifmap_id(obj_type, fq_name)
    # end uuid_to_ifmap_id

    def fq_name_to_uuid(self, obj_type, fq_name):
        obj_uuid = self._cassandra_db.fq_name_to_uuid(obj_type, fq_name)
        return obj_uuid
    # end fq_name_to_uuid

    def uuid_to_fq_name(self, obj_uuid):
        return self._cassandra_db.uuid_to_fq_name(obj_uuid)
    # end uuid_to_fq_name

    def uuid_to_obj_type(self, obj_uuid):
        return self._cassandra_db.uuid_to_obj_type(obj_uuid)
    # end uuid_to_obj_type

    def ifmap_id_to_fq_name(self, ifmap_id):
        return self._ifmap_db.ifmap_id_to_fq_name(ifmap_id)
    # end ifmap_id_to_fq_name

    def fq_name_to_ifmap_id(self, obj_type, fq_name):
        return self._ifmap_db.fq_name_to_ifmap_id(obj_type, fq_name)
    # end fq_name_to_ifmap_id

    def uuid_to_obj_dict(self, obj_uuid):
        return self._cassandra_db.uuid_to_obj_dict(obj_uuid)
    # end uuid_to_obj_dict

    def uuid_to_obj_perms(self, obj_uuid):
        return self._cassandra_db.uuid_to_obj_perms(obj_uuid)
    # end uuid_to_obj_perms

    def ref_update(self, obj_type, obj_uuid, ref_type, ref_uuid, ref_data, operation):
        obj_uuid_cf = self._cassandra_db._obj_uuid_cf
        bch = obj_uuid_cf.batch()
        if operation == 'ADD':
            self._cassandra_db._create_ref(bch, obj_type, obj_uuid, ref_type, ref_uuid, ref_data)
        elif operation == 'DELETE':
            self._cassandra_db._delete_ref(bch, obj_type, obj_uuid, ref_type, ref_uuid)
        else:
            pass
        self._cassandra_db.update_last_modified(bch, obj_uuid)
        bch.send()
        self._msgbus.dbe_update_publish(obj_type.replace('_', '-'), {'uuid':obj_uuid})
        return obj_uuid
    # ref_update

    def get_resource_class(self, resource_type):
        return self._api_svr_mgr.get_resource_class(resource_type)
    # end get_resource_class

    # Helper routines for REST
    def generate_url(self, obj_type, obj_uuid):
        return self._api_svr_mgr.generate_url(obj_type, obj_uuid)
    # end generate_url

    def config_object_error(self, id, fq_name_str, obj_type,
                            operation, err_str):
        self._api_svr_mgr.config_object_error(
            id, fq_name_str, obj_type, operation, err_str)
    # end config_object_error

    def config_log_error(self, err_str):
        self._api_svr_mgr.config_log_error(err_str)
    # end config_log_error

    def get_server_port(self):
        return self._api_svr_mgr.get_server_port()
    # end get_server_port

# end class VncDbClient
