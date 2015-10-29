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
from gevent.queue import Queue, Empty
import sys
import time
import functools
from pprint import pformat

from lxml import etree, objectify
import cgitb
import StringIO
import re

import socket
from netaddr import IPNetwork

from cfgm_common.uve.vnc_api.ttypes import *
from cfgm_common.uve.vnc_api.sandesh_dynamic_uve.ttypes import DynamicElement
from cfgm_common import ignore_exceptions
from cfgm_common.ifmap.client import client, namespaces
from cfgm_common.ifmap.request import NewSessionRequest, PublishRequest
from cfgm_common.ifmap.id import Identity
from cfgm_common.ifmap.operations import PublishUpdateOperation,\
    PublishDeleteOperation
from cfgm_common.ifmap.response import Response, newSessionResult
from cfgm_common.ifmap.metadata import Metadata
from cfgm_common.imid import escape
from cfgm_common.exceptions import ResourceExhaustionError, ResourceExistsError
from cfgm_common.vnc_cassandra import VncCassandraClient
from cfgm_common.vnc_kombu import VncKombuClient


import copy
from cfgm_common import jsonutils as json
import uuid
import datetime
import pycassa
import pycassa.util
import pycassa.cassandra.ttypes
from pycassa.system_manager import *
from pycassa.util import *

import signal, os


#from cfgm_common import vnc_type_conv
from provision_defaults import *
import cfgm_common.imid
from cfgm_common.exceptions import *
from vnc_quota import *
import gen
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus, \
    ConnectionType
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

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

class VncIfmapClient(object):

    def handler(self, signum, frame):
        file = open("/tmp/api-server-ifmap-cache.txt", "w")
        file.write(pformat(self._id_to_metas))
        file.close()

    def __init__(self, db_client_mgr, ifmap_srv_ip, ifmap_srv_port,
                 uname, passwd, ssl_options):
        for resource_type in gen.vnc_api_server_gen.all_resource_types:
            obj_type = resource_type.replace('-', '_')
            for oper in ('alloc', 'set', 'create', 'read_to_meta_index',
                         'update', 'delete'):
                method = getattr(self, '_object_%s' %(oper))
                bound_method = functools.partial(method, resource_type)
                functools.update_wrapper(bound_method, method)
                setattr(self, '_ifmap_%s_%s' %(obj_type, oper), bound_method)

        self._ifmap_srv_ip = ifmap_srv_ip
        self._ifmap_srv_port = ifmap_srv_port
        self._username = uname
        self._password = passwd
        self._ssl_options = ssl_options
        self._dequeue_greenlet = None
        self._CONTRAIL_XSD = "http://www.contrailsystems.com/vnc_cfg.xsd"
        self._IPERMS_NAME = "id-perms"
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
        self._conn_state = ConnectionStatus.INIT

        self._reset()

        # Set the signal handler
        signal.signal(signal.SIGUSR2, self.handler)

        self._init_conn()
        self._publish_config_root()
        self._health_checker_greenlet = gevent.spawn(self._health_checker)
    # end __init__

    def _object_alloc(self, res_type, parent_type, fq_name):
        my_fqn = ':'.join(fq_name)
        parent_fqn = ':'.join(fq_name[:-1])

        my_imid = 'contrail:%s:%s' %(res_type, my_fqn)
        if parent_fqn:
            if parent_type is None:
                return (None, None)
            parent_imid = 'contrail:' + parent_type + ':' + parent_fqn
        else: # parent is config-root
            parent_imid = 'contrail:config-root:root'

        # Normalize/escape special chars
        my_imid = escape(my_imid)
        parent_imid = escape(parent_imid)

        return True, (my_imid, parent_imid)
    # end _object_alloc

    def _object_set(self, res_type, my_imid, existing_metas, obj_dict):
        obj_type = res_type.replace('-', '_')
        obj_class = self._db_client_mgr.get_resource_class(res_type)
        update = {}

        # Properties Meta
        for prop_field in obj_class.prop_fields:
            field = obj_dict.get(prop_field)
            if field is None:
                continue
            # construct object of xsd-type and get its xml repr
            # e.g. virtual_network_properties
            is_simple, prop_type = obj_class.prop_field_types[prop_field]
            # e.g. virtual-network-properties
            prop_meta = obj_class.prop_field_metas[prop_field]
            if is_simple:
                norm_str = escape(str(field))
                meta = Metadata(prop_meta, norm_str,
                       {'ifmap-cardinality':'singleValue'}, ns_prefix = 'contrail')

                if (existing_metas and prop_meta in existing_metas and
                    str(existing_metas[prop_meta][0]['meta']) == str(meta)):
                    # no change
                    pass
                else:
                    self._update_id_self_meta(update, meta)
            else: # complex type
                prop_cls = cfgm_common.utils.str_to_class(prop_type,
                                                          __name__)
                buf = cStringIO.StringIO()
                # perms might be inserted at server as obj.
                # obj construction diff from dict construction.
                if isinstance(field, dict):
                    prop_cls(**field).exportChildren(
                        buf, level=1, name_=prop_meta, pretty_print=False)
                else: # object
                    field.exportChildren(
                        buf, level=1, name_=prop_meta, pretty_print=False)
                prop_xml = buf.getvalue()
                buf.close()
                meta = Metadata(prop_meta, '',
                    {'ifmap-cardinality':'singleValue'}, ns_prefix='contrail',
                    elements=prop_xml)

                if (existing_metas and prop_meta in existing_metas and
                    str(existing_metas[prop_meta][0]['meta']) == str(meta)):
                    # no change
                    pass
                else:
                    self._update_id_self_meta(update, meta)
        # end for all property types

        # References Meta
        for ref_field in obj_class.ref_fields:
            refs = obj_dict.get(ref_field)
            if not refs:
                continue
            for ref in refs:
                ref_fq_name = ref['to']
                ref_type, ref_link_type, _ = \
                    obj_class.ref_field_types[ref_field]
                ref_meta = obj_class.ref_field_metas[ref_field]
                ref_imid = cfgm_common.imid.get_ifmap_id_from_fq_name(
                    ref_type, ref_fq_name)
                ref_data = ref.get('attr')
                if ref_data:
                    buf = cStringIO.StringIO()
                    attr_cls = cfgm_common.utils.str_to_class(ref_link_type,
                                                              __name__)
                    attr_cls(**ref_data).exportChildren(
                        buf, level=1, name_=ref_meta, pretty_print=False)
                    ref_link_xml = buf.getvalue()
                    buf.close()
                else:
                    ref_link_xml = ''
                meta = Metadata(ref_meta, '',
                    {'ifmap-cardinality':'singleValue'}, ns_prefix = 'contrail',
                    elements=ref_link_xml)
                self._update_id_pair_meta(update, ref_imid, meta)
        # end for all ref types

        self._publish_update(my_imid, update)
        return (True, '')
    # end _object_set

    def _object_create(self, res_type, obj_ids, obj_dict):
        obj_type = res_type.replace('-', '_')

        if not 'parent_type' in obj_dict:
            # parent is config-root
            parent_type = 'config-root'
            parent_imid = 'contrail:config-root:root'
        else:
            parent_type = obj_dict['parent_type']
            parent_imid = obj_ids.get('parent_imid', None)

        # Parent Link Meta
        update = {}
        parent_cls = self._db_client_mgr.get_resource_class(parent_type)
        parent_link_meta = parent_cls.children_field_metas.get('%ss' %(obj_type))
        if parent_link_meta:
            meta = Metadata(parent_link_meta, '',
                       {'ifmap-cardinality':'singleValue'}, ns_prefix = 'contrail')
            self._update_id_pair_meta(update, obj_ids['imid'], meta)
            self._publish_update(parent_imid, update)

        (ok, result) = self._object_set(res_type, obj_ids['imid'], None, obj_dict)
        return (ok, result)
    # end _object_create

    def _object_read_to_meta_index(self, res_type, ifmap_id):
        obj_type = res_type.replace('-', '_')
        obj_cls = self._db_client_mgr.get_resource_class(res_type)
        # metas is a dict where key is meta-name and val is list of dict
        # of form [{'meta':meta}, {'id':id1, 'meta':meta}, {'id':id2, 'meta':meta}]
        metas = {}
        for meta_name, meta_val in self._id_to_metas[ifmap_id].items():
            metas[meta_name] = meta_val

        return metas
    # end _object_read_to_meta_index

    def _object_update(self, res_type, ifmap_id, new_obj_dict):
        obj_type = res_type.replace('-', '_')
        obj_cls = self._db_client_mgr.get_resource_class(res_type)
        # read in refs from ifmap to determine which ones become inactive after update
        existing_metas = self._object_read_to_meta_index(res_type, ifmap_id)

        # remove properties that are no longer active
        props = obj_cls.prop_field_metas
        for prop, meta in props.items():
            if meta in existing_metas and new_obj_dict.get(prop) is None:
                self._delete_id_self_meta(ifmap_id, 'contrail:'+meta)

        # remove refs that are no longer active
        delete_list = []
        refs = dict((obj_cls.ref_field_metas[rf],
                     obj_cls.ref_field_types[rf][0])
                    for rf in obj_cls.ref_fields)
        #refs = {'virtual-network-qos-forwarding-class': 'qos-forwarding-class',
        #        'virtual-network-network-ipam': 'network-ipam',
        #        'virtual-network-network-policy': 'network-policy',
        #        'virtual-network-route-table': 'route-table'}
        for meta, to_name in refs.items():
            old_set = set([m['id'] for m in existing_metas.get(meta, [])])
            new_set = set()
            to_name_m = to_name.replace('-', '_')
            for ref in new_obj_dict.get(to_name_m+'_refs', []):
                to_imid = self.fq_name_to_ifmap_id(to_name, ref['to'])
                new_set.add(to_imid)

            for inact_ref in old_set - new_set:
                delete_list.append((inact_ref, 'contrail:'+meta))

        if delete_list:
            self._delete_id_pair_meta_list(ifmap_id, delete_list)

        (ok, result) = self._object_set(res_type, ifmap_id, existing_metas, new_obj_dict)
        return (ok, result)
    # end _object_update

    def _object_delete(self, res_type, obj_ids):
        obj_type = res_type.replace('-', '_')

        ifmap_id = obj_ids['imid']
        parent_imid = obj_ids.get('parent_imid')
        existing_metas = self._object_read_to_meta_index(res_type, ifmap_id)
        meta_list = []
        for meta_name, meta_infos in existing_metas.items():
            for meta_info in meta_infos:
                ref_imid = meta_info.get('id')
                if ref_imid is None:
                    continue
                meta_list.append((ref_imid, 'contrail:'+meta_name))

        if parent_imid:
            # Remove link from parent
            meta_list.append((parent_imid, None))

        if meta_list:
            self._delete_id_pair_meta_list(ifmap_id, meta_list)

        # Remove all property metadata associated with this ident
        self._delete_id_self_meta(ifmap_id, None)

        return (True, '')
    # end _object_delete

    def _init_conn(self):
        mapclient = client(("%s" % (self._ifmap_srv_ip),
                            "%s" % (self._ifmap_srv_port)),
                           self._username, self._password,
                           self._NAMESPACES, self._ssl_options)
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
            server_addrs = ["%s:%s" % (self._ifmap_srv_ip,
                                       self._ifmap_srv_port)])
        self._conn_state = ConnectionStatus.UP
        msg = 'IFMAP connection ESTABLISHED'
        self.config_log(msg, level=SandeshLevel.SYS_NOTICE)

        mapclient.set_session_id(newSessionResult(result).get_session_id())
        mapclient.set_publisher_id(newSessionResult(result).get_publisher_id())
    # end _init_conn

    def _get_api_server(self):
        return self._db_client_mgr._api_svr_mgr
    # end _get_api_server

    def _reset(self):
        # Cache of metas populated in ifmap server. Useful in update to find
        # what things to remove in ifmap server
        self._id_to_metas = {}
        self._queue = Queue(self._get_api_server()._args.ifmap_queue_size)
        if self._dequeue_greenlet is None:
            self._dequeue_greenlet = gevent.spawn(self._ifmap_dequeue_task)
    # end _reset


    def _publish_config_root(self):
        # config-root
        buf = cStringIO.StringIO()
        perms = Provision.defaults.perms
        perms.exportChildren(buf, level=1, pretty_print=False)
        id_perms_xml = buf.getvalue()
        buf.close()
        update = {}
        meta = Metadata(self._IPERMS_NAME, '',
                        {'ifmap-cardinality': 'singleValue'},
                        ns_prefix='contrail', elements=id_perms_xml)
        self._update_id_self_meta(update, meta)
        self._publish_update("contrail:config-root:root", update)
    # end _publish_config_root

    def config_log(self, msg, level):
        self._db_client_mgr.config_log(msg, level)
    # end config_log

    @ignore_exceptions
    def _generate_ifmap_trace(self, oper, body):
        req_id = get_trace_id()
        ifmap_trace = IfmapTrace(request_id=req_id)
        ifmap_trace.operation = oper
        ifmap_trace.body = body

        return ifmap_trace
    # end _generate_ifmap_trace

    def _publish_to_ifmap_enqueue(self, oper, oper_body, do_trace=True):
        # safety check, if we proceed ifmap-server reports error
        # asking for update|delete in publish
        if not oper_body:
            return
        self._queue.put((oper, oper_body, do_trace))
    # end _publish_to_ifmap_enqueue

    def _ifmap_dequeue_task(self):
        while True:
            try:
                self._publish_to_ifmap_dequeue()
            except Exception as e:
                tb = cfgm_common.utils.detailed_traceback()
                self.config_log(tb, level=SandeshLevel.SYS_ERR)

    def _publish_to_ifmap_dequeue(self):
        def _publish(requests, traces, publish_discovery=False):
            ok = True
            if requests:
                ok, msg = self._publish_to_ifmap(''.join(requests))
            for trace in traces:
                if ok:
                    trace_msg(trace, 'IfmapTraceBuf', self._sandesh)
                else:
                    trace_msg(trace, 'IfmapTraceBuf', self._sandesh,
                              error_msg=msg)
            if publish_discovery and ok:
                self._get_api_server().publish_ifmap_to_discovery()
        # end _publish

        while True:
            # block until there is data in the queue
            (oper, oper_body, do_trace) = self._queue.get()
            requests = []
            requests_len = 0
            traces = []
            while True:
                # drain the queue till empty or max message size
                # or change of oper because ifmap does not like
                # different operations in same message
                if oper == 'publish_discovery':
                    _publish(requests, traces, True)
                    break
                if do_trace:
                    trace = self._generate_ifmap_trace(oper, oper_body)
                    traces.append(trace)
                requests.append(oper_body)
                requests_len += len(oper_body)
                if (requests_len >
                    self._get_api_server()._args.ifmap_max_message_size):
                    _publish(requests, traces)
                    break
                old_oper = oper
                try:
                    (oper, oper_body, do_trace) = self._queue.get_nowait()
                    if oper != old_oper:
                        _publish(requests, traces)
                        requests = []
                        requests_len = 0
                        traces = []
                        continue
                except Empty:
                    _publish(requests, traces)
                    break
    # end _publish_to_ifmap_dequeue

    def _publish_to_ifmap(self, oper_body):
        try:
            not_published = True
            retry_count = 0
            resp_xml = None
            while not_published:
                sess_id = self._mapclient.get_session_id()
                req_xml = PublishRequest(sess_id, oper_body)
                resp_xml = self._mapclient.call('publish', req_xml)

                resp_doc = etree.parse(StringIO.StringIO(resp_xml))
                err_codes = resp_doc.xpath(
                    '/env:Envelope/env:Body/ifmap:response/errorResult/@errorCode',
                    namespaces=self._NAMESPACES)
                if err_codes:
                    if retry_count == 0:
                        log_str = 'Error publishing to ifmap, req: %s, resp: %s' \
                                  %(req_xml, resp_xml)
                        self.config_log(log_str, level=SandeshLevel.SYS_ERR)

                    retry_count = retry_count + 1
                    result = self._mapclient.call('newSession',
                                                  NewSessionRequest())
                    sess_id = newSessionResult(result).get_session_id()
                    pub_id = newSessionResult(result).get_publisher_id()
                    self._mapclient.set_session_id(sess_id)
                    self._mapclient.set_publisher_id(pub_id)
                else: # successful publish
                    not_published = False
                    break
            # end while not_published

            if retry_count:
                log_str = 'Success publishing to ifmap after %d tries' \
                          %(retry_count)
                self.config_log(log_str, level=SandeshLevel.SYS_ERR)

            return True, resp_xml
        except Exception as e:
            if (isinstance(e, socket.error) and
                self._conn_state != ConnectionStatus.DOWN):
                self._conn_state = ConnectionStatus.DOWN
                log_str = 'Connection to IFMAP down. Failed to publish %s' %(
                    oper_body)
                self.config_log(log_str, level=SandeshLevel.SYS_ERR)
                ConnectionState.update(
                    conn_type=ConnectionType.IFMAP,
                    name='IfMap', status=ConnectionStatus.DOWN, message='',
                    server_addrs=["%s:%s" % (self._ifmap_srv_ip,
                                             self._ifmap_srv_port)])

                self._reset()
                self._get_api_server().un_publish_ifmap_to_discovery()
                # this will block till connection is re-established
                self._init_conn()
                self._publish_config_root()
                self._db_client_mgr.db_resync()
                return False, log_str
            else:
                log_str = 'Failed to publish %s to ifmap: %s' %(oper_body,
                                                                str(e))
                self.config_log(log_str, level=SandeshLevel.SYS_ERR)
                return False, log_str
    # end _publish_to_ifmap

    def _build_request(self, id1_name, id2_name, meta_list, delete=False):
            request = ''
            id1 = unicode(Identity(name=id1_name, type="other",
                                   other_type="extended"))
            if id2_name != 'self':
                id2 = unicode(Identity(name=id2_name, type="other",
                                       other_type="extended"))
            else:
                id2 = None
            for m in meta_list:
                if delete:
                    filter = unicode(m) if m else None
                    op = PublishDeleteOperation(id1=id1, id2=id2,
                                                filter=filter)
                else:
                    op = PublishUpdateOperation(id1=id1, id2=id2,
                                                metadata=unicode(m),
                                                lifetime='forever')
                request += unicode(op)
            return request

    def _delete_id_self_meta(self, self_imid, meta_name):
        mapclient = self._mapclient

        del_str = self._build_request(self_imid, 'self', [meta_name], True)
        self._publish_to_ifmap_enqueue('delete', del_str)

        # del meta from cache and del id if this was last meta
        if meta_name:
            prop_name = meta_name.replace('contrail:', '')
            del self._id_to_metas[self_imid][prop_name]
            if not self._id_to_metas[self_imid]:
                del self._id_to_metas[self_imid]
        else:
            del self._id_to_metas[self_imid]
    # end _delete_id_self_meta

    def _delete_id_pair_meta_list(self, id1, meta_list):
        mapclient = self._mapclient
        del_str = ''
        for id2, metadata in meta_list:
            del_str += self._build_request(id1, id2, [metadata], True)

        self._publish_to_ifmap_enqueue('delete', del_str)

        # del meta,id2 from cache and del id if this was last meta
        def _id_to_metas_delete(id1, id2, meta_name):
            if id1 not in self._id_to_metas:
                return
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
            self._id_to_metas[id1][meta_name] = [
                m for m in self._id_to_metas[id1][meta_name] if m['id'] != id2]
        for id2, metadata in meta_list:
            if metadata:
                meta_name = metadata.replace('contrail:', '')
                # replace with remaining refs
                _id_to_metas_delete(id1, id2, meta_name)
                _id_to_metas_delete(id2, id1, meta_name)
            else: # no meta specified remove all links from id1 to id2
                for meta_name in self._id_to_metas.get(id1, {}).keys():
                    _id_to_metas_delete(id1, id2, meta_name)
                for meta_name in self._id_to_metas.get(id2, {}).keys():
                    _id_to_metas_delete(id2, id1, meta_name)
    # end _delete_id_pair_meta_list

    def _delete_id_pair_meta(self, id1, id2, metadata):
        self._delete_id_pair_meta_list(id1, [(id2, metadata)])
    # end _delete_id_pair_meta

    def _update_id_self_meta(self, update, meta):
        """ update: dictionary of the type
                update[<id> | 'self'] = list(metadata)
        """
        mlist = update.setdefault('self', [])
        mlist.append(meta)
    # end _update_id_self_meta

    def _update_id_pair_meta(self, update, to_id, meta):
        mlist = update.setdefault(to_id, [])
        mlist.append(meta)
     # end _update_id_pair_meta

    def _publish_update(self, self_imid, update):
        if self_imid not in self._id_to_metas:
            self._id_to_metas[self_imid] = {}

        mapclient = self._mapclient
        requests = []
        for id2 in update:
            metalist = update[id2]
            requests.append(self._build_request(self_imid, id2, metalist))

            # remember what we wrote for diffing during next update
            for m in metalist:
                meta_name = m._Metadata__name.replace('contrail:', '')
                if id2 == 'self':
                    self._id_to_metas[self_imid][meta_name] = [{'meta':m}]
                    continue
                if meta_name in self._id_to_metas[self_imid]:
                    for id_meta in self._id_to_metas[self_imid][meta_name]:
                        if id_meta['id'] == id2:
                            id_meta['meta'] = m
                            break
                    else:
                        self._id_to_metas[self_imid][meta_name].append({'meta':m,
                                                                        'id': id2})
                else:
                   self._id_to_metas[self_imid][meta_name] = [{'meta':m,
                                                               'id': id2}]

                if id2 not in self._id_to_metas:
                    self._id_to_metas[id2] = {}
                if meta_name in self._id_to_metas[id2]:
                    for id_meta in self._id_to_metas[id2][meta_name]:
                        if id_meta['id'] == self_imid:
                            id_meta['meta'] = m
                            break
                    else:
                        self._id_to_metas[id2][meta_name].append({'meta':m,
                                                                  'id': self_imid})
                else:
                   self._id_to_metas[id2][meta_name] = [{'meta':m,
                                                         'id': self_imid}]
        upd_str = ''.join(requests)
        self._publish_to_ifmap_enqueue('update', upd_str)
    # end _publish_update

    def _health_checker(self):
        while True:
            try:
                # do the healthcheck only if we are connected
                if self._conn_state == ConnectionStatus.DOWN:
                    continue
                meta = Metadata('display-name', '',
                        {'ifmap-cardinality': 'singleValue'},
                        ns_prefix='contrail', elements='')
                request_str = self._build_request('healthcheck', 'self', [meta])
                self._publish_to_ifmap_enqueue('update', request_str, do_trace=False)
            except Exception as e:
                log_str = 'Healthcheck to IFMAP failed: %s' %(str(e))
                self.config_log(log_str, level=SandeshLevel.SYS_ERR)
            finally:
                gevent.sleep(
                    self._get_api_server().get_ifmap_health_check_interval())
    # end _health_checker

    def fq_name_to_ifmap_id(self, obj_type, fq_name):
        return cfgm_common.imid.get_ifmap_id_from_fq_name(obj_type, fq_name)
    # end fq_name_to_ifmap_id

    def ifmap_id_to_fq_name(self, ifmap_id):
        return cfgm_common.imid.get_fq_name_from_ifmap_id(ifmap_id)
    # end ifmap_id_to_fq_name

# end class VncIfmapClient


class VncServerCassandraClient(VncCassandraClient):
    # Useragent datastore keyspace + tables (used by neutron plugin currently)
    _USERAGENT_KEYSPACE_NAME = 'useragent'
    _USERAGENT_KV_CF_NAME = 'useragent_keyval_table'

    @classmethod
    def get_db_info(cls):
        db_info = VncCassandraClient.get_db_info() + \
                  [(cls._USERAGENT_KEYSPACE_NAME, [cls._USERAGENT_KV_CF_NAME])]
        return db_info
    # end get_db_info

    def __init__(self, db_client_mgr, cass_srv_list, reset_config, db_prefix,
                      cassandra_credential):
        self._db_client_mgr = db_client_mgr
        keyspaces = {
            self._USERAGENT_KEYSPACE_NAME: [(self._USERAGENT_KV_CF_NAME, None)]
        }
        if reset_config:
            reset_config = [self._USERAGENT_KEYSPACE_NAME,
                            VncCassandraClient._UUID_KEYSPACE_NAME]
        else:
            reset_config = []
        super(VncServerCassandraClient, self).__init__(
            cass_srv_list, db_prefix, keyspaces, self.config_log,
            generate_url=db_client_mgr.generate_url,
            reset_config=reset_config,credential=cassandra_credential)
        self._useragent_kv_cf = self._cf_dict[self._USERAGENT_KV_CF_NAME]
    # end __init__

    def config_log(self, msg, level):
        self._db_client_mgr.config_log(msg, level)
    # end config_log

    def create(self, method_name, *args, **kwargs):
        method = getattr(self, '_cassandra_%s_create' % (method_name))
        return method(*args, **kwargs)
    # end create

    def update(self, method_name, *args, **kwargs):
        method = getattr(self, '_cassandra_%s_update' % (method_name))
        return method(*args, **kwargs)
    # end update

    def delete(self, method_name, *args, **kwargs):
        method = getattr(self, '_cassandra_%s_delete' % (method_name))
        return method(*args, **kwargs)
    # end delete

    def ref_update(self, obj_type, obj_uuid, ref_type, ref_uuid, ref_data, operation):
        bch = self._obj_uuid_cf.batch()
        if operation == 'ADD':
            self._create_ref(bch, obj_type, obj_uuid, ref_type, ref_uuid, ref_data)
        elif operation == 'DELETE':
            self._delete_ref(bch, obj_type, obj_uuid, ref_type, ref_uuid)
        else:
            pass
        self.update_last_modified(bch, obj_uuid)
        bch.send()
    # end ref_update

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

    def update_last_modified(self, bch, obj_uuid, id_perms=None):
        if id_perms is None:
            id_perms = json.loads(
                self._obj_uuid_cf.get(obj_uuid,
                                      ['prop:id_perms'])['prop:id_perms'])
        id_perms['last_modified'] = datetime.datetime.utcnow().isoformat()
        self._update_prop(bch, obj_uuid, 'id_perms', {'id_perms': id_perms})
    # end update_last_modified

    # Insert new perms. Called on startup when walking DB
    def update_perms2(self, obj_uuid):
        bch = self._obj_uuid_cf.batch()
        perms2 = copy.deepcopy(Provision.defaults.perms2)
        perms2_json = json.dumps(perms2, default=lambda o: dict((k, v)
                               for k, v in o.__dict__.iteritems()))
        perms2 = json.loads(perms2_json)
        self._update_prop(bch, obj_uuid, 'perms2', {'perms2': perms2})
        bch.send()

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

    # fetch perms2 for an object
    def uuid_to_obj_perms2(self, id):
        try:
            perms2_json = self._obj_uuid_cf.get(
                id, columns=['prop:perms2'])['prop:perms2']
            perms2 = json.loads(perms2_json)
        except pycassa.NotFoundException:
            raise NoIdError(id)
        return perms2
    # end uuid_to_obj_perms2

    def useragent_kv_store(self, key, value):
        columns = {'value': value}
        self._useragent_kv_cf.insert(key, columns)
    # end useragent_kv_store

    def useragent_kv_retrieve(self, key):
        if key:
            if isinstance(key, list):
                rows = self._useragent_kv_cf.multiget(key)
                return [rows[row].get('value') for row in rows]
            else:
                try:
                    columns = self._useragent_kv_cf.get(key)
                except pycassa.NotFoundException:
                    raise NoUserAgentKey
                return columns.get('value')
        else:  # no key specified, return entire contents
            kv_list = []
            for ua_key, ua_cols in self._useragent_kv_cf.get_range():
                kv_list.append({'key': ua_key, 'value': ua_cols.get('value')})
            return kv_list
    # end useragent_kv_retrieve

    def useragent_kv_delete(self, key):
        self._useragent_kv_cf.remove(key)
    # end useragent_kv_delete

    def walk(self, fn):
        walk_results = []
        obj_infos = [x for x in self._obj_uuid_cf.get_range(
                                          columns=['type', 'fq_name'],
                                          column_count=self._MAX_COL)]
        type_to_object = {}
        for obj_uuid, obj_col in obj_infos:
            try:
                obj_type = json.loads(obj_col['type'])
                obj_fq_name = json.loads(obj_col['fq_name'])
                # prep cache to avoid n/w round-trip in db.read for ref
                self.cache_uuid_to_fq_name_add(obj_uuid, obj_fq_name, obj_type)

                try:
                    type_to_object[obj_type].append(obj_uuid)
                except KeyError:
                    type_to_object[obj_type] = [obj_uuid]
            except Exception as e:
                self.config_log('Error in db walk read %s' %(str(e)),
                                level=SandeshLevel.SYS_ERR)
                continue

        for obj_type, uuid_list in type_to_object.items():
            try:
                self.config_log('Resync: obj_type %s len %s'
                                %(obj_type, len(uuid_list)),
                                level=SandeshLevel.SYS_INFO)
                result = fn(obj_type, uuid_list)
                if result:
                    walk_results.append(result)
            except Exception as e:
                self.config_log('Error in db walk invoke %s' %(str(e)),
                                level=SandeshLevel.SYS_ERR)
                continue

        return walk_results
    # end walk
# end class VncCassandraClient


class VncServerKombuClient(VncKombuClient):
    def __init__(self, db_client_mgr, rabbit_ip, rabbit_port, ifmap_db,
                 rabbit_user, rabbit_password, rabbit_vhost, rabbit_ha_mode):
        self._db_client_mgr = db_client_mgr
        self._sandesh = db_client_mgr._sandesh
        self._ifmap_db = ifmap_db
        listen_port = db_client_mgr.get_server_port()
        q_name = 'vnc_config.%s-%s' %(socket.gethostname(), listen_port)
        super(VncServerKombuClient, self).__init__(
            rabbit_ip, rabbit_port, rabbit_user, rabbit_password, rabbit_vhost,
            rabbit_ha_mode, q_name, self._dbe_subscribe_callback, self.config_log)

    # end __init__

    def prepare_to_consume(self):
        self._db_client_mgr.wait_for_resync_done()
    # prepare_to_consume

    def config_log(self, msg, level):
        self._db_client_mgr.config_log(msg, level)
    # end config_log

    def uuid_to_fq_name(self, uuid):
        self._db_client_mgr.uuid_to_fq_name(uuid)
    # end uuid_to_fq_name

    def dbe_uve_trace(self, oper, typ, uuid, body):
        self._db_client_mgr.dbe_uve_trace(oper, typ, uuid, body)
    # end uuid_to_fq_name

    def dbe_oper_publish_pending(self):
        return self.num_pending_messages()
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

    def _dbe_subscribe_callback(self, oper_info):
        self._db_client_mgr.wait_for_resync_done()
        try:
            msg = "Notification Message: %s" %(pformat(oper_info))
            self.config_log(msg, level=SandeshLevel.SYS_DEBUG)
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
            self.config_log(string_buf.getvalue(),
                level=SandeshLevel.SYS_ERR)
            trace_msg(trace, name='MessageBusNotifyTraceBuf',
                              sandesh=self._sandesh, error_msg=errmsg)
    #end _dbe_subscribe_callback

    def dbe_create_publish(self, obj_type, obj_ids, obj_dict):
        req_id = get_trace_id()
        oper_info = {'request-id': req_id,
                     'oper': 'CREATE',
                     'type': obj_type,
                     'obj_dict': obj_dict}
        oper_info.update(obj_ids)
        self.publish(oper_info)
    # end dbe_create_publish

    def _dbe_create_notification(self, obj_info):
        obj_dict = obj_info['obj_dict']

        self.dbe_uve_trace("CREATE", obj_info['type'], obj_info['uuid'], obj_dict)

        try:
            r_class = self._db_client_mgr.get_resource_class(obj_info['type'])
            if r_class:
                r_class.dbe_create_notification(obj_info, obj_dict)
        except Exception as e:
            err_msg = ("Failed in type specific dbe_create_notification " +
                       str(e))
            self.config_log(err_msg, level=SandeshLevel.SYS_ERR)
            raise
        finally:
            method_name = obj_info['type'].replace('-', '_')
            method = getattr(self._ifmap_db, "_ifmap_%s_create" % (method_name))
            (ok, result) = method(obj_info, obj_dict)
            if not ok:
                self.config_log(result, level=SandeshLevel.SYS_ERR)
                raise Exception(result)
    #end _dbe_create_notification

    def dbe_update_publish(self, obj_type, obj_ids):
        oper_info = {'oper': 'UPDATE', 'type': obj_type}
        oper_info.update(obj_ids)
        self.publish(oper_info)
    # end dbe_update_publish

    def _dbe_update_notification(self, obj_info):
        try:
            (ok, result) = self._db_client_mgr.dbe_read(obj_info['type'], obj_info)
        except NoIdError as e:
            # No error, we will hear a delete shortly
            return

        new_obj_dict = result

        self.dbe_uve_trace("UPDATE", obj_info['type'], obj_info['uuid'], new_obj_dict)

        try:
            r_class = self._db_client_mgr.get_resource_class(obj_info['type'])
            if r_class:
                r_class.dbe_update_notification(obj_info)
        except:
            msg = "Failed to invoke type specific dbe_update_notification"
            self.config_log(msg, level=SandeshLevel.SYS_ERR)
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
        self.publish(oper_info)
    # end dbe_delete_publish

    def _dbe_delete_notification(self, obj_info):
        obj_dict = obj_info['obj_dict']

        self.dbe_uve_trace("DELETE", obj_info['type'], obj_info['uuid'], None)

        db_client_mgr = self._db_client_mgr
        db_client_mgr._cassandra_db.cache_uuid_to_fq_name_del(obj_dict['uuid'])

        try:
            r_class = self._db_client_mgr.get_resource_class(obj_info['type'])
            if r_class:
                r_class.dbe_delete_notification(obj_info, obj_dict)
        except:
            msg = "Failed to invoke type specific dbe_delete_notification"
            self.config_log(msg, level=SandeshLevel.SYS_ERR)
            raise
        finally:
            method_name = obj_info['type'].replace('-', '_')
            method = getattr(self._ifmap_db, "_ifmap_%s_delete" % (method_name))
            (ok, ifmap_result) = method(obj_info)
            if not ok:
                self.config_log(ifmap_result, level=SandeshLevel.SYS_ERR)
                raise Exception(ifmap_result)
    #end _dbe_delete_notification

# end class VncKombuClient


class VncZkClient(object):
    _SUBNET_PATH = "/api-server/subnets"
    _FQ_NAME_TO_UUID_PATH = "/fq-name-to-uuid"
    _MAX_SUBNET_ADDR_ALLOC = 65535

    def __init__(self, instance_id, zk_server_ip, reset_config, db_prefix,
                 sandesh_hdl):
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

        self._sandesh = sandesh_hdl
        self._reconnect_zk_greenlet = None
        while True:
            try:
                self._zk_client = ZookeeperClient(client_name, zk_server_ip,
                                                  self._sandesh)
                # set the lost callback to always reconnect
                self._zk_client.set_lost_cb(self.reconnect_zk)
                break
            except gevent.event.Timeout as e:
                pass

        if reset_config:
            self._zk_client.delete_node(self._subnet_path, True);
            self._zk_client.delete_node(self._fq_name_to_uuid_path, True);
        self._subnet_allocators = {}
    # end __init__

    def _reconnect_zk(self):
        self._zk_client.connect()
        self._reconnect_zk_greenlet = None
    # end

    def reconnect_zk(self):
        if self._reconnect_zk_greenlet is None:
            self._reconnect_zk_greenlet = gevent.spawn(self._reconnect_zk)
    # end

    def create_subnet_allocator(self, subnet, subnet_alloc_list,
                                addr_from_start, should_persist,
                                start_subnet, size):
        # TODO handle subnet resizing change, ignore for now
        if subnet not in self._subnet_allocators:
            if addr_from_start is None:
                addr_from_start = False
            self._subnet_allocators[subnet] = IndexAllocator(
                self._zk_client, self._subnet_path+'/'+subnet+'/',
                size=size, start_idx=start_subnet, reverse=not addr_from_start,
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

    def subnet_set_in_use(self, subnet, addr):
        allocator = self._get_subnet_allocator(subnet)
        allocator.set_in_use(addr)
    # end subnet_set_in_use

    def subnet_reset_in_use(self, subnet, addr):
        allocator = self._get_subnet_allocator(subnet)
        allocator.reset_in_use(addr)
    # end subnet_reset_in_use

    def subnet_reserve_req(self, subnet, addr, value):
        allocator = self._get_subnet_allocator(subnet)
        return allocator.reserve(addr, value)
    # end subnet_reserve_req

    def subnet_alloc_count(self, subnet):
        allocator = self._get_subnet_allocator(subnet)
        return allocator.get_alloc_count()
    # end subnet_alloc_count

    def subnet_alloc_req(self, subnet, value=None):
        allocator = self._get_subnet_allocator(subnet)
        try:
            return allocator.alloc(value=value)
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
                 rabbit_servers, rabbit_port, rabbit_user, rabbit_password,
                 rabbit_vhost, rabbit_ha_mode, reset_config=False,
                 zk_server_ip=None, db_prefix='', cassandra_credential=None):

        self._api_svr_mgr = api_svr_mgr
        self._sandesh = api_svr_mgr._sandesh

        self._UVEMAP = {
            "virtual_network" : "ObjectVNTable",
            "virtual_machine" : "ObjectVMTable",
            "service_instance" : "ObjectSITable",
            "virtual_router" : "ObjectVRouter",
            "analytics_node" : "ObjectCollectorInfo",
            "database_node" : "ObjectDatabaseInfo",
            "config_node" : "ObjectConfigNode",
            "service_chain" : "ServiceChain",
            "physical_router" : "ObjectPRouter",
        }

        self._UVEGLOBAL = set([
            "virtual_router",
            "analytics_node",
            "database_node",
            "config_node",
            "physical_router"
        ])

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

        msg = "Connecting to ifmap on %s:%s as %s" \
              % (ifmap_srv_ip, ifmap_srv_port, uname)
        self.config_log(msg, level=SandeshLevel.SYS_NOTICE)

        self._ifmap_db = VncIfmapClient(
            self, ifmap_srv_ip, ifmap_srv_port, uname, passwd, ssl_options)

        msg = "Connecting to cassandra on %s" % (cass_srv_list,)
        self.config_log(msg, level=SandeshLevel.SYS_NOTICE)

        self._cassandra_db = VncServerCassandraClient(
            self, cass_srv_list, reset_config, db_prefix, cassandra_credential)

        msg = "Connecting to zookeeper on %s" % (zk_server_ip)
        self.config_log(msg, level=SandeshLevel.SYS_NOTICE)
        self._zk_db = VncZkClient(api_svr_mgr._args.worker_id, zk_server_ip,
                                  reset_config, db_prefix, self.config_log)

        self._msgbus = VncServerKombuClient(self, rabbit_servers,
                                            rabbit_port, self._ifmap_db,
                                            rabbit_user, rabbit_password,
                                            rabbit_vhost, rabbit_ha_mode)
    # end __init__

    def _update_default_quota(self):
        """ Read the default quotas from the configuration
        and update it in the project object if not already
        updated.
        """
        default_quota = QuotaHelper.default_quota

        proj_id = self.fq_name_to_uuid('project',
                                       ['default-domain', 'default-project'])
        try:
            (ok, result) = self.dbe_read('project', {'uuid':proj_id})
        except NoIdError as e:
            ok = False
            result = 'Project Not Found: %s' %(proj_id)
        if not ok:
            self.config_log("Updating default quota failed: %s." %(result),
                level=SandeshLevel.SYS_ERR)
            return

        proj_dict = result
        quota = QuotaType()

        proj_dict['quota'] = default_quota
        self.dbe_update('project', {'uuid':proj_id}, proj_dict)
    # end _update_default_quota

    def db_resync(self):
        # Read contents from cassandra and publish to ifmap
        mapclient = self._ifmap_db._mapclient
        start_time = datetime.datetime.utcnow()
        self._cassandra_db.walk(self._dbe_resync)
        self._ifmap_db._publish_to_ifmap_enqueue('publish_discovery', 1)
        self.config_log("Cassandra DB walk completed.",
            level=SandeshLevel.SYS_INFO)
        self._update_default_quota()
        end_time = datetime.datetime.utcnow()
        msg = "Time elapsed in syncing ifmap: %s" % (str(end_time - start_time))
        self.config_log(msg, level=SandeshLevel.SYS_DEBUG)
        self._db_resync_done.set()
    # end db_resync

    def wait_for_resync_done(self):
        self._db_resync_done.wait()
    # end wait_for_resync_done

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
            self._zk_db.create_fq_name_to_uuid_mapping(obj_type, fq_name,
                                                       str(id))

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
    # end match_uuid

    def update_subnet_uuid(self, vn_dict, do_update=False):
        vn_uuid = vn_dict.get('uuid')

        def _read_subnet_uuid(subnet):
            if vn_uuid is None:
                return None
            pfx = subnet['subnet']['ip_prefix']
            pfx_len = subnet['subnet']['ip_prefix_len']

            network = IPNetwork('%s/%s' % (pfx, pfx_len))
            subnet_key = '%s %s/%s' % (vn_uuid, str(network.ip), pfx_len)
            try:
                return self.useragent_kv_retrieve(subnet_key)
            except NoUserAgentKey:
                return None

        ipam_refs = vn_dict.get('network_ipam_refs', [])
        updated = False
        for ipam in ipam_refs:
            vnsn = ipam['attr']
            subnets = vnsn['ipam_subnets']
            for subnet in subnets:
                if subnet.get('subnet_uuid'):
                    continue

                subnet_uuid = _read_subnet_uuid(subnet) or str(uuid.uuid4())
                subnet['subnet_uuid'] = subnet_uuid
                if not updated:
                    updated = True

        if updated and do_update:
            self._cassandra_db._cassandra_virtual_network_update(vn_uuid,
                                                                 vn_dict)
    # end update_subnet_uuid

    def _dbe_resync(self, obj_type, obj_uuids):
        obj_class = cfgm_common.utils.obj_type_to_vnc_class(obj_type, __name__)
        obj_fields = list(obj_class.prop_fields) + list(obj_class.ref_fields)
        (ok, obj_dicts) = self._cassandra_db.read(
                               obj_type, obj_uuids, field_names=obj_fields)
        for obj_dict in obj_dicts:
            # give chance for zk heartbeat/ping
            gevent.sleep(0)

            try:
                obj_uuid = obj_dict['uuid']
                self.dbe_uve_trace("RESYNC", obj_type, obj_uuid, obj_dict)
                # TODO remove backward compat (use RT instead of VN->LR ref)
                if (obj_type == 'virtual_network' and
                    'logical_router_refs' in obj_dict):
                    for router in obj_dict['logical_router_refs']:
                        self._cassandra_db._delete_ref(None, obj_type, obj_uuid,
                                                       'logical_router',
                                                       router['uuid'])

                # create new perms if upgrading
                if obj_dict.get('perms2') is None:
                    self._cassandra_db.update_perms2(obj_uuid)

                if (obj_type == 'virtual_network' and
                    'network_ipam_refs' in obj_dict):
                    self.update_subnet_uuid(obj_dict, do_update=True)
            except Exception as e:
                self.config_object_error(
                    obj_dict.get('uuid'), None, obj_type,
                    'dbe_resync:cassandra_read', str(e))
                continue
            try:
                parent_type = obj_dict.get('parent_type', None)
                method = getattr(self._ifmap_db, "_ifmap_%s_alloc" % (obj_type))
                (ok, result) = method(parent_type, obj_dict['fq_name'])
                (my_imid, parent_imid) = result
            except Exception as e:
                self.config_object_error(
                    obj_uuid, None, obj_type, 'dbe_resync:ifmap_alloc', str(e))
                continue

            try:
                obj_ids = {'uuid': obj_uuid, 'imid': my_imid,
                           'parent_imid': parent_imid}
                method = getattr(self._ifmap_db, "_ifmap_%s_create" % (obj_type))
                (ok, result) = method(obj_ids, obj_dict)
            except Exception as e:
                self.config_object_error(
                    obj_uuid, None, obj_type, 'dbe_resync:ifmap_create', str(e))
                continue
        # end for all objects
    # end _dbe_resync


    def _dbe_check(self, obj_type, obj_uuids):
        for obj_uuid in obj_uuids:
            try:
                (ok, obj_dict) = self._cassandra_db.read(obj_type, [obj_uuid])
            except Exception as e:
                return {'uuid': obj_uuid, 'type': obj_type, 'error': str(e)}
     # end _dbe_check

    def _dbe_read(self, obj_type, obj_uuids):
        results = []
        for obj_uuid in obj_uuids:
            try:
                (ok, obj_dict) = self._cassandra_db.read(obj_type, [obj_uuid])
                result_dict = obj_dict[0]
                result_dict['type'] = obj_type
                result_dict['uuid'] = obj_uuid
                results.append(result_dict)
            except Exception as e:
                self.config_object_error(
                    obj_uuid, None, obj_type, '_dbe_read:cassandra_read', str(e))
                continue

        return results
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
                ok = self.set_uuid(obj_type, obj_dict,
                                   uuid.UUID(uuid_requested), False)
            else:
                (ok, obj_uuid) = self._alloc_set_uuid(obj_type, obj_dict)
        except ResourceExistsError as e:
            return (False, (409, str(e)))

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

    def dbe_uve_trace(self, oper, typ, uuid, obj_dict):
        oo = {}
        oo['uuid'] = uuid
        oo['name'] = self.uuid_to_fq_name(uuid)
        oo['value'] = obj_dict
        oo['type'] = typ

        req_id = get_trace_id()
        db_trace = DBRequestTrace(request_id=req_id)
        db_trace.operation = oper
        db_trace.body = "name=" + str(oo['name']) + " type=" + typ + " value=" +  json.dumps(obj_dict)
        trace_msg(db_trace, 'DBUVERequestTraceBuf', self._sandesh)

        attr_contents = None
        emap = {}
        if oo['value']:
            for ck,cv in oo['value'].iteritems():
                emap[ck] = json.dumps(cv)

        utype = oo['type']
        urawkey = ':'.join(oo['name'])
        ukey = None
        utab = None
        if utype in self._UVEMAP:
            utab = self._UVEMAP[utype]
            if utype in self._UVEGLOBAL:
                ukey = urawkey.split(":",1)[1]
            else:
                ukey = urawkey
        elif utype == 'bgp_router':
            utab = "ObjectBgpRouter"
            ukey = urawkey.rsplit(":",1)[1]
        else:
            return

        if oo['value']:
            cc = ContrailConfig(name=ukey, elements=emap)
        else:
            cc = ContrailConfig(name=ukey, elements={}, deleted=True)
        cfg_msg = ContrailConfigTrace(data=cc, table=utab,
                                      sandesh=self._sandesh)
        cfg_msg.send(sandesh=self._sandesh)
    # end dbe_uve_trace

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

    # create/update indexes if object is shared
    def build_shared_index(oper):
        def wrapper1(func):
            def wrapper2(self, obj_type, obj_ids, obj_dict):

                # don't build sharing indexes if operation (create/update) failed
                (ok, result) = func(self, obj_type, obj_ids, obj_dict)
                if not ok:
                    return (ok, result)

                """
                fetch current sharing information to see what might have changed
                Note that perms2 field may be missing if we are upgrading from earlier release
                and this is called during synching of database on startup.
                """
                obj_uuid = obj_ids['uuid']
                try:
                    cur_perms2 = self.uuid_to_obj_perms2(obj_uuid)
                except Exception as e:
                    msg = 'RBAC: missing perms2 for obj %s, type %s' % (obj_uuid, obj_type)
                    self.config_log(msg, level=SandeshLevel.SYS_NOTICE)
                    return (ok, result)

                try:
                    new_perms2 = obj_dict['perms2']
                    share_perms = new_perms2['share']
                    global_access = new_perms2['global_access']
                except Exception as e:
                    msg = 'RBAC: missing perms2 in dict obj %s, type %s' % (obj_uuid, obj_type)
                    self.config_log(msg, level=SandeshLevel.SYS_NOTICE)
                    return (ok, result)

                # change in global access?
                if cur_perms2['global_access'] != global_access:
                    if global_access:
                        self._cassandra_db.set_shared(obj_type, obj_uuid, rwx = global_access)
                    else:
                        self._cassandra_db.del_shared(obj_type, obj_uuid)

                # change in shared list? Construct temporary sets to compare
                cur_shared_list = set(item['tenant']+':'+str(item['tenant_access']) for item in cur_perms2['share'])
                new_shared_list = set(item['tenant']+':'+str(item['tenant_access']) for item in new_perms2['share'])
                if cur_shared_list == new_shared_list:
                    return (ok, result)

                # delete sharing if no longer in shared list
                for share_info in cur_shared_list - new_shared_list:
                    share_info = share_info.split(":")
                    self._cassandra_db.del_shared(obj_type, obj_uuid,
                        share_id = share_info[0], share_type = 'tenant')

                # share this object with specified tenants
                for share_info in new_shared_list - cur_shared_list:
                    share_info = share_info.split(":")
                    self._cassandra_db.set_shared(obj_type, obj_uuid,
                        share_id = share_info[0], share_type = 'tenant', rwx = int(share_info[1]))

                return (ok, result)
            return wrapper2
        return wrapper1

    @dbe_trace('create')
    @build_shared_index('create')
    def dbe_create(self, obj_type, obj_ids, obj_dict):
        method_name = obj_type.replace('-', '_')
        (ok, result) = self._cassandra_db.create(method_name, obj_ids, obj_dict)

        # publish to ifmap via msgbus
        self._msgbus.dbe_create_publish(obj_type, obj_ids, obj_dict)

        return (ok, result)
    # end dbe_create

    # input id is ifmap-id + uuid
    def dbe_read(self, obj_type, obj_ids, obj_fields=None):
        method_name = obj_type.replace('-', '_')
        try:
            (ok, cassandra_result) = self._cassandra_db.read(method_name,
                                                             [obj_ids['uuid']],
                                                             obj_fields)
        except NoIdError as e:
            # if NoIdError is for obj itself (as opposed to say for parent
            # or ref), let caller decide if this can be handled gracefully
            # by re-raising
            if e._unknown_id == obj_ids['uuid']:
                raise

            return (False, str(e))

        return (ok, cassandra_result[0])
    # end dbe_read

    def dbe_count_children(self, obj_type, obj_id, child_type):
        method_name = obj_type.replace('-', '_')
        try:
            (ok, cassandra_result) = self._cassandra_db.count_children(
                method_name, obj_id, child_type)
        except NoIdError as e:
            return (False, str(e))

        return (ok, cassandra_result)
    # end dbe_count_children

    def dbe_read_multi(self, obj_type, obj_ids_list, obj_fields=None):
        if not obj_ids_list:
            return (True, [])

        method_name = obj_type.replace('-', '_')
        try:
            (ok, cassandra_result) = self._cassandra_db.read(
                method_name, [obj_id['uuid'] for obj_id in obj_ids_list],
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
    @build_shared_index('update')
    def dbe_update(self, obj_type, obj_ids, new_obj_dict):
        method_name = obj_type.replace('-', '_')
        (ok, cassandra_result) = self._cassandra_db.update(
            method_name, obj_ids['uuid'], new_obj_dict)

        # publish to ifmap via message bus (rabbitmq)
        self._msgbus.dbe_update_publish(obj_type, obj_ids)

        return (ok, cassandra_result)
    # end dbe_update

    def dbe_list(self, obj_type, parent_uuids=None, back_ref_uuids=None,
                 obj_uuids=None, count=False, filters=None,
                 paginate_start=None, paginate_count=None):
        method_name = obj_type.replace('-', '_')
        (ok, cassandra_result) = self._cassandra_db.list(
                 method_name, parent_uuids=parent_uuids,
                 back_ref_uuids=back_ref_uuids, obj_uuids=obj_uuids,
                 count=count, filters=filters)
        return (ok, cassandra_result)
    # end dbe_list

    @dbe_trace('delete')
    def dbe_delete(self, obj_type, obj_ids, obj_dict):
        method_name = obj_type.replace('-', '_')
        (ok, cassandra_result) = self._cassandra_db.delete(method_name,
                                                           obj_ids['uuid'])

        # publish to ifmap via message bus (rabbitmq)
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

    def subnet_set_in_use(self, subnet, addr):
        return self._zk_db.subnet_set_in_use(subnet, addr)
    # end subnet_set_in_use

    def subnet_reset_in_use(self, subnet, addr):
        return self._zk_db.subnet_reset_in_use(subnet, addr)
    #end subnet_reset_in_use

    def subnet_alloc_count(self, subnet):
        return self._zk_db.subnet_alloc_count(subnet)
    # end subnet_alloc_count

    def subnet_alloc_req(self, subnet, value=None):
        return self._zk_db.subnet_alloc_req(subnet, value)
    # end subnet_alloc_req

    def subnet_reserve_req(self, subnet, addr=None, value=None):
        return self._zk_db.subnet_reserve_req(subnet, addr, value)
    # end subnet_reserve_req

    def subnet_free_req(self, subnet, addr):
        return self._zk_db.subnet_free_req(subnet, addr)
    # end subnet_free_req

    def subnet_create_allocator(self, subnet, subnet_alloc_list,
                                addr_from_start, should_persist,
                                start_subnet, size):
        return self._zk_db.create_subnet_allocator(subnet,
                               subnet_alloc_list, addr_from_start,
                               should_persist, start_subnet, size)
    # end subnet_create_allocator

    def subnet_delete_allocator(self, subnet):
        return self._zk_db.delete_subnet_allocator(subnet)
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

    def ref_update(self, obj_type, obj_uuid, ref_type, ref_uuid, ref_data,
                   operation):
        self._cassandra_db.ref_update(obj_type, obj_uuid, ref_type, ref_uuid,
                                      ref_data, operation)
        self._msgbus.dbe_update_publish(obj_type.replace('_', '-'),
    # ref_update
                                        {'uuid':obj_uuid})
    def uuid_to_obj_perms2(self, obj_uuid):
        return self._cassandra_db.uuid_to_obj_perms2(obj_uuid)
    # end uuid_to_obj_perms2


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

    def config_log(self, msg, level):
        self._api_svr_mgr.config_log(msg, level)
    # end config_log

    def get_server_port(self):
        return self._api_svr_mgr.get_server_port()
    # end get_server_port

    # return all objects shared with us (tenant)
    # useful for collections
    def get_shared_objects(self, obj_type, obj_uuid):
        shared = []
        # specifically shared with us
        l1 = self._cassandra_db.get_shared(obj_type, share_id = obj_uuid, share_type = 'tenant')
        if l1:
            shared.extend(l1)

        # globally shared
        l2 = self._cassandra_db.get_shared(obj_type)
        if l2:
            shared.extend(l2)

        return shared
    # end get_shared_objects

# end class VncDbClient
