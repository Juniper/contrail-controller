#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from gevent import monkey; monkey.patch_all()

import gevent
from gevent.queue import Queue, Empty
from lxml import etree
import socket
import signal
import cStringIO
import StringIO
import time

from pprint import pformat
from cfgm_common import ignore_exceptions
from cfgm_common import jsonutils as json
from cfgm_common.ifmap.client import client
from cfgm_common.ifmap.id import Identity
from cfgm_common.ifmap.metadata import Metadata
from cfgm_common.ifmap.operations import PublishDeleteOperation
from cfgm_common.ifmap.operations import PublishUpdateOperation
from cfgm_common.ifmap.request import EndSessionRequest
from cfgm_common.ifmap.request import NewSessionRequest
from cfgm_common.ifmap.request import PublishRequest
from cfgm_common.ifmap.response import newSessionResult
from cfgm_common.imid import escape
from cfgm_common.imid import ifmap_wipe
from cfgm_common.imid import get_ifmap_id_from_fq_name
from cfgm_common.imid import entity_is_present
from cfgm_common.utils import detailed_traceback
from cfgm_common.utils import str_to_class
from cfgm_common import vnc_greenlets
from gen.resource_xsd import *
from provision_defaults import Provision
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh.traces.ttypes import IfmapTrace


@ignore_exceptions
def trace_msg(trace_objs=[], trace_name='', sandesh_hdl=None, error_msg=None):
    for trace_obj in trace_objs:
        if error_msg:
            trace_obj.error = error_msg
        trace_obj.trace_msg(name=trace_name, sandesh=sandesh_hdl)
# end trace_msg


def build_idperms_ifmap_obj(prop_field, values):
    prop_xml = u'<uuid><uuid-mslong>'
    prop_xml += unicode(json.dumps(values[u'uuid'][u'uuid_mslong']))
    prop_xml += u'</uuid-mslong><uuid-lslong>'
    prop_xml += unicode(json.dumps(values[u'uuid'][u'uuid_lslong']))
    prop_xml += u'</uuid-lslong></uuid><enable>'
    prop_xml += unicode(json.dumps(values[u'enable']))
    prop_xml += u'</enable>'
    return prop_xml


class VncIfmapClient(object):

    # * Not all properties in an object needs to be published
    #   to IfMap.
    # * In some properties, not all fields are relevant
    #   to be publised to IfMap.
    # If the property is not relevant at all, define the property
    # with None. If it is partially relevant, then define the fn.
    # which would handcraft the generated xml for the object.
    IFMAP_PUBLISH_SKIP_LIST = {
        # Format - <prop_field> : None | <Handler_fn>
        u"perms2" : None,
        u"id_perms" : build_idperms_ifmap_obj
    }

    def handler(self, signum, frame):
        file = open("/tmp/api-server-ifmap-cache.txt", "w")
        file.write(pformat(self._id_to_metas))
        file.close()

    def __init__(self, db_client_mgr, ifmap_srv_ip, ifmap_srv_port,
                 uname, passwd, ssl_options):
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

        ConnectionState.update(conn_type = ConnType.IFMAP,
            name = 'IfMap', status = ConnectionStatus.INIT, message = '',
            server_addrs = ["%s:%s" % (ifmap_srv_ip, ifmap_srv_port)])
        self._conn_state = ConnectionStatus.INIT
        self._is_ifmap_up = False
        self._queue = Queue(self._get_api_server()._args.ifmap_queue_size)

        self.reset()

        # Set the signal handler
        signal.signal(signal.SIGUSR2, self.handler)

        self._init_conn()
        self._publish_config_root()
        self._health_checker_greenlet =\
               vnc_greenlets.VncGreenlet('VNC IfMap Health Checker',
                                         self._health_checker)
    # end __init__

    @classmethod
    def object_alloc(cls, obj_class, parent_res_type, fq_name):
        res_type = obj_class.resource_type
        my_fqn = ':'.join(fq_name)
        parent_fqn = ':'.join(fq_name[:-1])

        my_imid = 'contrail:%s:%s' %(res_type, my_fqn)
        if parent_fqn:
            if parent_res_type is None:
                err_msg = "Parent: %s type is none for: %s" % (parent_fqn,
                                                               my_fqn)
                return False, (409, err_msg)
            parent_imid = 'contrail:' + parent_res_type + ':' + parent_fqn
        else: # parent is config-root
            parent_imid = 'contrail:config-root:root'

        # Normalize/escape special chars
        my_imid = escape(my_imid)
        parent_imid = escape(parent_imid)

        return True, (my_imid, parent_imid)
    # end object_alloc

    def object_set(self, obj_class, my_imid, existing_metas, obj_dict):
        update = {}

        # Properties Meta
        for prop_field in obj_class.prop_fields:
            field = obj_dict.get(prop_field)
            if field is None:
                continue
            # construct object of xsd-type and get its xml repr
            # e.g. virtual_network_properties
            prop_field_types = obj_class.prop_field_types[prop_field]
            is_simple = not prop_field_types['is_complex']
            prop_type = prop_field_types['xsd_type']
            # e.g. virtual-network-properties
            prop_meta = obj_class.prop_field_metas[prop_field]

            if prop_field in VncIfmapClient.IFMAP_PUBLISH_SKIP_LIST:
                # Field not relevant, skip publishing to IfMap
                if not VncIfmapClient.IFMAP_PUBLISH_SKIP_LIST[prop_field]:
                    continue
                # Call the handler fn to generate the relevant fields.
                if callable(VncIfmapClient.IFMAP_PUBLISH_SKIP_LIST[prop_field]):
                    prop_xml = VncIfmapClient.IFMAP_PUBLISH_SKIP_LIST[prop_field](
                                        prop_field, field)
                    meta = Metadata(prop_meta, '',
                        {'ifmap-cardinality':'singleValue'}, ns_prefix='contrail',
                        elements=prop_xml)
                else:
                    log_str = '%s is marked for partial publish\
                               to Ifmap but handler not defined' %(
                                prop_field)
                    self.config_log(log_str, level=SandeshLevel.SYS_DEBUG)
                    continue
            elif is_simple:
                norm_str = escape(str(field))
                meta = Metadata(prop_meta, norm_str,
                       {'ifmap-cardinality':'singleValue'}, ns_prefix = 'contrail')
            else: # complex type
                prop_cls = str_to_class(prop_type, __name__)
                buf = cStringIO.StringIO()
                # perms might be inserted at server as obj.
                # obj construction diff from dict construction.
                if isinstance(field, dict):
                    prop_cls(**field).exportChildren(
                        buf, level=1, name_=prop_meta, pretty_print=False)
                elif isinstance(field, list):
                    for elem in field:
                        if isinstance(elem, dict):
                            prop_cls(**elem).exportChildren(
                                buf, level=1, name_=prop_meta, pretty_print=False)
                        else:
                            elem.exportChildren(
                                buf, level=1, name_=prop_meta, pretty_print=False)
                else: # object
                    field.exportChildren(
                        buf, level=1, name_=prop_meta, pretty_print=False)
                prop_xml = buf.getvalue()
                buf.close()
                meta = Metadata(prop_meta, '',
                    {'ifmap-cardinality':'singleValue'}, ns_prefix='contrail',
                    elements=prop_xml)

            # If obj is new (existing metas is none) or
            # if obj does not have this prop_meta (or)
            # or if the prop_meta is different from what we have currently,
            # then update
            if (not existing_metas or
                not prop_meta in existing_metas or
                    ('' in existing_metas[prop_meta] and
                    str(meta) != str(existing_metas[prop_meta]['']))):
                self._update_id_self_meta(update, meta)
        # end for all property types

        # References Meta
        for ref_field in obj_class.ref_fields:
            refs = obj_dict.get(ref_field)
            if not refs:
                continue
            for ref in refs:
                ref_fq_name = ref['to']
                ref_fld_types_list = list(
                    obj_class.ref_field_types[ref_field])
                ref_res_type = ref_fld_types_list[0]
                ref_link_type = ref_fld_types_list[1]
                ref_meta = obj_class.ref_field_metas[ref_field]
                ref_imid = get_ifmap_id_from_fq_name(ref_res_type, ref_fq_name)
                ref_data = ref.get('attr')
                if ref_data:
                    buf = cStringIO.StringIO()
                    attr_cls = str_to_class(ref_link_type, __name__)
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
    # end object_set

    def object_create(self, obj_ids, obj_dict):
        obj_type = obj_ids['type']
        obj_class = self._db_client_mgr.get_resource_class(obj_type)
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

        (ok, result) = self.object_set(obj_class, obj_ids['imid'], None, obj_dict)
        return (ok, result)
    # end object_create

    def _object_read_to_meta_index(self, ifmap_id):
        # metas is a dict where key is meta-name and val is list of dict of
        # form [{'meta':meta}, {'id':id1, 'meta':meta}, {'id':id2, 'meta':meta}]
        metas = {}
        if ifmap_id in self._id_to_metas:
            metas = self._id_to_metas[ifmap_id].copy()
        return metas
    # end _object_read_to_meta_index

    def object_update(self, obj_cls, new_obj_dict):
        ifmap_id = get_ifmap_id_from_fq_name(obj_cls.resource_type,
                                             new_obj_dict['fq_name'])
        # read in refs from ifmap to determine which ones become inactive after update
        existing_metas = self._object_read_to_meta_index(ifmap_id)

        if not existing_metas:
            # UPDATE notify queued before CREATE notify, Skip publish to IFMAP.
            return (True, '')

        # remove properties that are no longer active
        props = obj_cls.prop_field_metas
        for prop, meta in props.items():
            if meta in existing_metas and new_obj_dict.get(prop) is None:
                self._delete_id_self_meta(ifmap_id, meta)

        # remove refs that are no longer active
        delete_list = []
        refs = dict((obj_cls.ref_field_metas[rf],
                     obj_cls.ref_field_types[rf][0])
                    for rf in obj_cls.ref_fields)
        #refs = {'virtual-network-qos-forwarding-class': 'qos-forwarding-class',
        #        'virtual-network-network-ipam': 'network-ipam',
        #        'virtual-network-network-policy': 'network-policy',
        #        'virtual-network-route-table': 'route-table'}
        for meta, ref_res_type in refs.items():
            old_set = set(existing_metas.get(meta, {}).keys())
            new_set = set()
            ref_obj_type = self._db_client_mgr.get_resource_class(
                ref_res_type).object_type
            for ref in new_obj_dict.get(ref_obj_type+'_refs', []):
                to_imid = get_ifmap_id_from_fq_name(ref_res_type, ref['to'])
                new_set.add(to_imid)

            for inact_ref in old_set - new_set:
                delete_list.append((inact_ref, meta))

        if delete_list:
            self._delete_id_pair_meta_list(ifmap_id, delete_list)

        (ok, result) = self.object_set(obj_cls, ifmap_id, existing_metas, new_obj_dict)
        return (ok, result)
    # end object_update

    def object_delete(self, obj_ids):
        ifmap_id = obj_ids['imid']
        parent_imid = obj_ids.get('parent_imid')
        existing_metas = self._object_read_to_meta_index(ifmap_id)
        meta_list = []
        for meta_name, meta_infos in existing_metas.items():
            # Delete all refs/links in the object.
            # Refs are identified when the key is a non-empty string.
            meta_list.extend(
                [(k, meta_name)
                  for k in meta_infos if k != ''])

        if parent_imid:
            # Remove link from parent
            meta_list.append((parent_imid, None))

        if meta_list:
            self._delete_id_pair_meta_list(ifmap_id, meta_list)

        # Remove all property metadata associated with this ident
        self._delete_id_self_meta(ifmap_id, None)

        return (True, '')
    # end object_delete

    def _init_conn(self):
        self._mapclient = client(("%s" % (self._ifmap_srv_ip),
                                  "%s" % (self._ifmap_srv_port)),
                                 self._username, self._password,
                                 self._NAMESPACES, self._ssl_options)

        connected = False
        while not connected:
            try:
                resp_xml = self._mapclient.call('newSession', NewSessionRequest())
            except socket.error as e:
                msg = 'Failed to establish IF-MAP connection: %s' % str(e)
                self.config_log(msg, level=SandeshLevel.SYS_WARN)
                time.sleep(3)
                continue

            resp_doc = etree.parse(StringIO.StringIO(resp_xml))
            err_codes = resp_doc.xpath(
                '/env:Envelope/env:Body/ifmap:response/errorResult/@errorCode',
                namespaces=self._NAMESPACES)
            if not err_codes:
                connected = True
            else:
                msg = "Failed to establish IF-MAP connection: %s" % err_codes
                self.config_log(msg, level=SandeshLevel.SYS_WARN)
                session_id = self._mapclient.get_session_id()
                try:
                    self._mapclient.call('endSession',
                                         EndSessionRequest(session_id))
                except socket.error as e:
                    msg = "Failed to end the IF-MAP session %s: %s" %\
                          (session_id, str(e))
                    self.config_log(msg, level=SandeshLevel.SYS_WARN)
                time.sleep(3)

        ConnectionState.update(conn_type = ConnType.IFMAP,
            name = 'IfMap', status = ConnectionStatus.UP, message = '',
            server_addrs = ["%s:%s" % (self._ifmap_srv_ip,
                                       self._ifmap_srv_port)])
        self._conn_state = ConnectionStatus.UP
        msg = 'IFMAP connection ESTABLISHED'
        self.config_log(msg, level=SandeshLevel.SYS_NOTICE)

        self._mapclient.set_session_id(
            newSessionResult(resp_xml).get_session_id())
        self._mapclient.set_publisher_id(
            newSessionResult(resp_xml).get_publisher_id())
    # end _init_conn

    def _get_api_server(self):
        return self._db_client_mgr._api_svr_mgr
    # end _get_api_server

    def reset(self):
        self._id_to_metas = {}
        while not self._queue.empty():
            self._queue.get_nowait()

        if (self._dequeue_greenlet is not None and
                gevent.getcurrent() != self._dequeue_greenlet):
            self._dequeue_greenlet.kill()
        self._dequeue_greenlet =\
              vnc_greenlets.VncGreenlet("VNC IfMap Dequeue",
                                        self._ifmap_dequeue_task)

    # end reset

    def _publish_config_root(self):
        # Remove all resident data
        result = ifmap_wipe(self._mapclient)
        if result is None:
            msg = "Cannot purge the IF-MAP server before publishing root graph"
            self.config_log(msg, level=SandeshLevel.SYS_WARN)
        # Build default config-root
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
                tb = detailed_traceback()
                self.config_log(tb, level=SandeshLevel.SYS_ERR)

    def _publish_to_ifmap_dequeue(self):
        def _publish(requests, traces, publish_discovery=False):
            if not requests:
                return
            ok = False
            # Continue to trying publish requests until the queue is full.
            # When queue is full, ifmap is totally resync from db
            while not ok:
                ok, err_msg = self._publish_to_ifmap(''.join(requests))
                if ok:
                    trace_msg(traces, 'IfmapTraceBuf', self._sandesh)
                else:
                    trace_msg(traces, 'IfmapTraceBuf', self._sandesh,
                              error_msg=err_msg)
                if publish_discovery and ok:
                    self._get_api_server().publish_ifmap_to_discovery()
                    self._is_ifmap_up = True
                if not ok:
                    msg = ("%s. IF-MAP sending queue size: %d/%d" %
                           (err_msg, self._queue.qsize(),
                            self._get_api_server()._args.ifmap_queue_size))
                    self.config_log(msg, level=SandeshLevel.SYS_WARN)
                    gevent.sleep(1)
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

                    ConnectionState.update(
                        conn_type = ConnType.IFMAP,
                        name = 'IfMap',
                        status = ConnectionStatus.INIT,
                        message = 'Session lost, renew it',
                        server_addrs = ["%s:%s" % (self._ifmap_srv_ip,
                                                   self._ifmap_srv_port)])
                    self._conn_state = ConnectionStatus.INIT
                    self._is_ifmap_up = False
                    retry_count = retry_count + 1
                    self._init_conn()

                    if self._ifmap_restarted():
                        msg = "IF-MAP servers restarted, re-populate it"
                        self.config_log(msg, level=SandeshLevel.SYS_ERR)

                        self.reset()
                        self._get_api_server().publish_ifmap_to_discovery(
                            'down', msg)

                        self._publish_config_root()
                        self._db_client_mgr.db_resync()
                        self._publish_to_ifmap_enqueue('publish_discovery', 1)

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
            # Failed to publish the operation due to unknown error.
            # Probably a connection issue with the ifmap server.
            msg = "Failed to publish request %s: %s" % (oper_body, str(e))
            return False, msg
    # end _publish_to_ifmap

    def _build_request(self, id1_name, id2_name, meta_list, delete=False):
            request = ''
            id1 = str(Identity(name=id1_name, type="other",
                                   other_type="extended"))
            if id2_name != 'self':
                id2 = str(Identity(name=id2_name, type="other",
                                       other_type="extended"))
            else:
                id2 = None
            for m in meta_list:
                if delete:
                    _filter = str(m) if m else None
                    op = PublishDeleteOperation(id1=id1, id2=id2,
                                                filter=_filter)
                else:
                    x = str(m)
                    op = PublishUpdateOperation(id1=id1, id2=id2,
                                                metadata=x,
                                                lifetime='forever')
                request += str(op)
            return request

    def _delete_id_self_meta(self, self_imid, meta_name):
        contrail_metaname = 'contrail:' + meta_name if meta_name else None
        del_str = self._build_request(self_imid, 'self', [contrail_metaname],
                                      True)
        self._publish_to_ifmap_enqueue('delete', del_str)

        try:

            # del meta from cache and del id if this was last meta
            if meta_name:
                del self._id_to_metas[self_imid][meta_name]
                if not self._id_to_metas[self_imid]:
                    del self._id_to_metas[self_imid]
            else:
                del self._id_to_metas[self_imid]

        except KeyError:
            # Case of delete received for an id which we do not know about.
            # Could be a case of duplicate delete.
            # There is nothing for us to do here. Just log and proceed.
            msg = "Delete received for unknown imid(%s) meta_name(%s)." % \
                  (self_imid, meta_name)
            self.config_log(msg, level=SandeshLevel.SYS_DEBUG)

    # end _delete_id_self_meta

    def _delete_id_pair_meta_list(self, id1, meta_list):
        del_str = ''
        for id2, metadata in meta_list:
            contrail_metadata = 'contrail:' + metadata if metadata else None
            del_str += self._build_request(id1, id2, [contrail_metadata], True)

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
            if id2 in self._id_to_metas[id1][meta_name]:
                del self._id_to_metas[id1][meta_name][id2]
        #end _id_to_metas_delete

        for id2, metadata in meta_list:
            if metadata:
                # replace with remaining refs
                _id_to_metas_delete(id1, id2, metadata)
                _id_to_metas_delete(id2, id1, metadata)
            else: # no meta specified remove all links from id1 to id2
                for meta_name in self._id_to_metas.get(id1, {}).keys():
                    _id_to_metas_delete(id1, id2, meta_name)
                for meta_name in self._id_to_metas.get(id2, {}).keys():
                    _id_to_metas_delete(id2, id1, meta_name)
    # end _delete_id_pair_meta_list

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
        requests = []
        self_metas = self._id_to_metas.setdefault(self_imid, {})
        for id2, metalist in update.items():
            request = self._build_request(self_imid, id2, metalist)

            for m in metalist:
                meta_name = m._Metadata__name[9:]

                # Objects have two types of members - Props and refs/links.
                # Props are cached in id_to_metas as
                #        id_to_metas[self_imid][meta_name]['']
                #        (with empty string as key)

                # Links are cached in id_to_metas as
                #        id_to_metas[self_imid][meta_name][id2]
                #        id2 is used as a key

                if id2 == 'self':
                    self_metas[meta_name] = {'' : m}
                    continue

                if meta_name in self_metas:
                    # Update the link/ref
                    self_metas[meta_name][id2] = m
                else:
                    # Create a new link/ref
                    self_metas[meta_name] = {id2 : m}

                # Reverse linking from id2 to id1
                self._id_to_metas.setdefault(id2, {})

                if meta_name in self._id_to_metas[id2]:
                    self._id_to_metas[id2][meta_name][self_imid] = m
                else:
                    self._id_to_metas[id2][meta_name] = {self_imid : m}

            requests.append(request)

        upd_str = ''.join(requests)
        self._publish_to_ifmap_enqueue('update', upd_str)
    # end _publish_update

    def _ifmap_restarted(self):
        return not entity_is_present(self._mapclient, 'config-root', ['root'])

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

                # Confirm the existence of the following default global entities in IFMAP.
                search_list = [
                    ('global-system-config', ['default-global-system-config']),
                ]
                for type, fq_name in search_list:
                    if not entity_is_present(self._mapclient, type, fq_name):
                        raise Exception("%s not found in IFMAP DB" % ':'.join(fq_name))

                # If we had unpublished the IFMAP server to discovery server earlier
                # publish it back now since it is valid now.
                if not self._is_ifmap_up:
                    self._get_api_server().publish_ifmap_to_discovery('up', '')
                    self._is_ifmap_up = True
                    ConnectionState.update(conn_type = ConnType.IFMAP,
                                           name = 'IfMap',
                                           status = ConnectionStatus.UP,
                                           message = '',
                                           server_addrs = ["%s:%s" % (self._ifmap_srv_ip,
                                                                      self._ifmap_srv_port)])
            except Exception as e:
                log_str = 'IFMAP Healthcheck failed: %s' %(str(e))
                self.config_log(log_str, level=SandeshLevel.SYS_ERR)
                if self._is_ifmap_up:
                    self._get_api_server().publish_ifmap_to_discovery('down',
                                                   'IFMAP DB - Invalid state')
                    self._is_ifmap_up = False
                    ConnectionState.update(conn_type = ConnType.IFMAP,
                                           name = 'IfMap',
                                           status = ConnectionStatus.DOWN,
                                           message = 'Invalid IFMAP DB State',
                                           server_addrs = ["%s:%s" % (self._ifmap_srv_ip,
                                                                      self._ifmap_srv_port)])
            finally:
                gevent.sleep(
                    self._get_api_server().get_ifmap_health_check_interval())
    # end _health_checker
# end class VncIfmapClient
