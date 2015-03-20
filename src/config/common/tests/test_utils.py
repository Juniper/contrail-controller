#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import gevent
import gevent.queue
import gevent.wsgi
import os
import sys
import logging
import pdb
import json
from pprint import pprint
import functools
import socket
import time
import errno
import re
import copy
from lxml import etree
from xml.sax.saxutils import escape, unescape
try:
    from collections import OrderedDict
except ImportError:
    from ordereddict import OrderedDict
import pycassa
import Queue
from collections import deque
import kombu
import kazoo
from kazoo.client import KazooState
from copy import deepcopy
from datetime import datetime
from pycassa.util import *
from vnc_api import vnc_api
from novaclient import exceptions as nc_exc

from cfgm_common.exceptions import ResourceExistsError

def stub(*args, **kwargs):
    pass

class FakeApiConfigLog(object):
    _all_logs = []
    send = stub
    def __init__(self, *args, **kwargs):
        FakeApiConfigLog._all_logs.append(kwargs['api_log'])

    @classmethod
    def _print(cls):
        for log in cls._all_logs:
            x = copy.deepcopy(log.__dict__)
            #body = x.pop('body')
            #pprint(json.loads(body))
            pprint(x)
            print "\n"
# class FakeApiConfigLog

class FakeWSGIHandler(gevent.wsgi.WSGIHandler):
    logger = logging.getLogger('FakeWSGIHandler')
    logger.addHandler(logging.FileHandler('api_server.log'))
    def __init__(self, socket, address, server):
        super(FakeWSGIHandler, self).__init__(socket, address, server)
        #server.log = open('api_server.log', 'a')
        class LoggerWriter(object):
            def write(self, message):
                FakeWSGIHandler.logger.log(logging.INFO, message)
        server.log = LoggerWriter()

class CassandraCFs(object):
    _all_cfs = {}

    @classmethod
    def add_cf(cls, name, cf):
        CassandraCFs._all_cfs[name] = cf
    # end add_cf

    @classmethod
    def get_cf(cls, name):
        return CassandraCFs._all_cfs[name]
    # end get_cf

    @classmethod
    def reset(cls):
        cls._all_cfs = {}
# end CassandraCFs

class FakeCF(object):

    def __init__(*args, **kwargs):
        self = args[0]
        self._name = args[3]
        try:
            old_cf = CassandraCFs.get_cf(self._name)
            self._rows = old_cf._rows
        except KeyError:
            self._rows = OrderedDict({})
        self.column_validators = {}
        CassandraCFs.add_cf(self._name, self)
    # end __init__

    def get_range(self, *args, **kwargs):
        for key in self._rows:
            yield (key, self.get(key))
    # end get_range

    def _column_within_range(self, column_name, column_start, column_finish):
        if column_start and column_name < column_start:
            return False
        if column_finish and column_name > column_finish:
            return False

        return True
    # end _column_within_range

    def get(
        self, key, columns=None, column_start=None, column_finish=None,
            column_count=0, include_timestamp=False):
        if not key in self._rows:
            raise pycassa.NotFoundException

        if columns:
            col_dict = {}
            for col_name in columns:
                col_value = self._rows[key][col_name][0]
                if include_timestamp:
                    col_tstamp = self._rows[key][col_name][1]
                    col_dict[col_name] = (col_value, col_tstamp)
                else:
                    col_dict[col_name] = col_value
        else:
            col_dict = {}
            for col_name in self._rows[key].keys():
                if not self._column_within_range(col_name,
                                    column_start, column_finish):
                    continue

                col_value = self._rows[key][col_name][0]
                if include_timestamp:
                    col_tstamp = self._rows[key][col_name][1]
                    col_dict[col_name] = (col_value, col_tstamp)
                else:
                    col_dict[col_name] = col_value

        return col_dict
    # end get

    def multiget(
        self, keys, columns=None, column_start=None, column_finish=None,
            column_count=0, include_timestamp=False):
        result = {}
        for key in keys:
            try:
                result[key] = {}
                for col_name in self._rows[key]:
                    if not self._column_within_range(col_name,
                                        column_start, column_finish):
                        continue
                    result[key][col_name] = copy.deepcopy(self._rows[key][col_name])
            except KeyError:
                pass

        return result
    # end multiget

    def insert(self, key, col_dict):
        if key not in self._rows:
            self._rows[key] = {}

        tstamp = datetime.now()
        for col_name in col_dict.keys():
            self._rows[key][col_name] = (col_dict[col_name], tstamp)

    # end insert

    def remove(self, key, columns=None):
        try:
            if columns:
                # for each entry in col_name delete each that element
                for col_name in columns:
                    del self._rows[key][col_name]
            else:
                    del self._rows[key]
        except KeyError:
            # pycassa remove ignores non-existing keys
            pass
    # end remove

    def xget(self, key, column_start=None, column_finish=None,
             include_timestamp=False):
        col_names = []
        if key in self._rows:
            col_names = self._rows[key].keys()

        for col_name in col_names:
            if not self._column_within_range(col_name,
                                column_start, column_finish):
                continue

            col_value = self._rows[key][col_name][0]
            if include_timestamp:
                col_tstamp = self._rows[key][col_name][1]
                yield (col_name, (col_value, col_tstamp))
            else:
                yield (col_name, col_value)

    # end xget

    def batch(self):
        return self
    # end batch

    def send(self):
        pass
    # end send

# end class FakeCF


class FakeNovaClient(object):

    @staticmethod
    def initialize(*args, **kwargs):
        return FakeNovaClient

    class flavors:

        @staticmethod
        def find(*args, **kwargs):
            return 1
    # end class flavors

    class images:

        @staticmethod
        def find(name):
            return 1
    # end class images

    class servers:

        @staticmethod
        def create(name, image, flavor, nics, *args, **kwargs):
            vm = vnc_api.VirtualMachine(name)
            FakeNovaClient.vnc_lib.virtual_machine_create(vm)
            for network in nics:
                if 'nic-id' in network:
                    vn = FakeNovaClient.vnc_lib.virtual_network_read(
                        id=network['net-id'])
                    vmi = vnc_api.VirtualMachineInterface(vn.name, parent_obj=vm)
                    vmi.set_virtual_network(vn)
                    FakeNovaClient.vnc_lib.virtual_machine_interface_create(vmi)

                    ip_address = FakeNovaClient.vnc_lib.virtual_network_ip_alloc(
                        vn, count=1)[0]
                    ip_obj = vnc_api.InstanceIp(ip_address, ip_address)
                    ip_obj.add_virtual_network(vn)
                    ip_obj.add_virtual_machine_interface(vmi)
                    FakeNovaClient.vnc_lib.instance_ip_create(ip_obj)
                elif 'port-id' in network:
                    vmi = FakeNovaClient.vnc_lib.virtual_machine_interface_read(id=network['port-id'])
                    vmi.add_virtual_machine(vm)
                    FakeNovaClient.vnc_lib.virtual_machine_interface_update(vmi)
            # end for network
            vm.id = vm.uuid
            vm.delete = FakeNovaClient.delete_vm.__get__(
                vm, vnc_api.VirtualMachine)
            vm.get = stub
            return vm
        # end create

        @staticmethod
        def find(id):
            try:
                vm = FakeNovaClient.vnc_lib.virtual_machine_read(id=id)
            except vnc_api.NoIdError:
                raise nc_exc.NotFound(404, "")
            vm.delete = FakeNovaClient.delete_vm.__get__(
                vm, vnc_api.VirtualMachine)
            vm.status = 'OK'
            return vm
        # end find
        get = find
    # end class servers

    @staticmethod
    def delete_vm(vm):
        for if_ref in vm.get_virtual_machine_interfaces() or vm.get_virtual_machine_interface_back_refs():
            intf = FakeNovaClient.vnc_lib.virtual_machine_interface_read(
                id=if_ref['uuid'])
            for ip_ref in intf.get_instance_ip_back_refs() or []:
                FakeNovaClient.vnc_lib.instance_ip_delete(id=ip_ref['uuid'])
            FakeNovaClient.vnc_lib.virtual_machine_interface_delete(
                id=if_ref['uuid'])
        FakeNovaClient.vnc_lib.virtual_machine_delete(id=vm.uuid)
    # end delete_vm
# end class FakeNovaClient


class FakeIfmapClient(object):
    # _graph is dict of ident_names where val for each key is
    # dict with keys 'ident' and 'links'
    # 'ident' has ident xml element
    # 'links' is a dict with keys of concat(<meta-name>' '<ident-name>')
    # and vals of dict with 'meta' which has meta xml element and
    #                       'other' which has other ident xml element
    # eg. cls._graph['contrail:network-ipam:default-domain:default-project:
    # ipam2'] =
    # 'ident': <Element identity at 0x2b3e280>,
    # 'links': {'contrail:id-perms': {'meta': <Element metadata at 0x2b3eb40>},
    #           'contrail:project-network-ipam
    #            contrail:project:default-domain:default-project':
    #               {'other': <Element identity at 0x2b3eaa0>,
    #                'meta': <Element metadata at 0x2b3ea50>},
    #                'contrail:virtual-network-network-ipam contrail:
    #                virtual-network:default-domain:default-project:vn2':
    #                {'other': <Element identity at 0x2b3ee10>,
    #                 'meta': <Element metadata at 0x2b3e410>}}}
    _graph = {}
    _published_messages = [] # all messages published so far
    _subscribe_lists = [] # list of all subscribers indexed by session-id
    _PUBLISH_ENVELOPE = \
        """<?xml version="1.0" encoding="UTF-8"?> """\
        """<env:Envelope xmlns:"""\
        """env="http://www.w3.org/2003/05/soap-envelope" xmlns:"""\
        """ifmap="http://www.trustedcomputinggroup.org/2010/IFMAP/2" """\
        """xmlns:contrail="http://www.contrailsystems.com/"""\
        """vnc_cfg.xsd" """\
        """xmlns:meta="http://www.trustedcomputinggroup.org"""\
        """/2010/IFMAP-METADATA/2"> """\
        """<env:Body> %(body)s </env:Body> </env:Envelope>"""

    _RSP_ENVELOPE = \
        """<?xml version="1.0" encoding="UTF-8" standalone="yes"?> """\
        """<env:Envelope xmlns:ifmap="http://www.trustedcomputinggroup.org"""\
        """/2010/IFMAP/2" """\
        """xmlns:env="http://www.w3.org/2003/05/soap-envelope" """\
        """xmlns:meta="http://www.trustedcomputinggroup.org"""\
        """/2010/IFMAP-METADATA/2" """\
        """xmlns:contrail="http://www.contrailsystems.com/vnc_cfg.xsd"> """\
        """<env:Body><ifmap:response> %(result)s """\
        """</ifmap:response></env:Body></env:Envelope>"""

    @classmethod
    def reset(cls):
        cls._graph = {}
        cls._published_messages = [] # all messages published so far
        cls._subscribe_lists = [] # list of all subscribers indexed by session-id
    # end reset

    @staticmethod
    def initialize(*args, **kwargs):
        pass
    # end initialize

    @classmethod
    def _update_publish(cls, upd_root):
        subscribe_item = etree.Element('resultItem')
        subscribe_item.extend(deepcopy(upd_root))
        from_name = escape(upd_root[0].attrib['name'])
        if not from_name in cls._graph:
            cls._graph[from_name] = {'ident': upd_root[0], 'links': {}}

        if len(upd_root) == 2:
            meta_name = re.sub("{.*}", "contrail:", upd_root[1][0].tag)
            link_key = meta_name
            link_info = {'meta': upd_root[1]}
            cls._graph[from_name]['links'][link_key] = link_info
        elif len(upd_root) == 3:
            meta_name = re.sub("{.*}", "contrail:", upd_root[2][0].tag)
            to_name = escape(upd_root[1].attrib['name'])
            link_key = '%s %s' % (meta_name, to_name)
            link_info = {'meta': upd_root[2], 'other': upd_root[1]}
            cls._graph[from_name]['links'][link_key] = link_info

            # reverse mapping only for strong refs
            # currently refs from same type to each other is weak ref
            from_type = from_name.split(':')[1]
            to_type = to_name.split(':')[1]
            if not to_name in cls._graph:
                cls._graph[to_name] = {'ident': upd_root[1], 'links': {}}
            link_key = '%s %s' % (meta_name, from_name)
            link_info = {'meta': upd_root[2], 'other': upd_root[0]}
            cls._graph[to_name]['links'][link_key] = link_info
        else:
            raise Exception("Unknown ifmap update: %s" %
                            (etree.tostring(upd_root)))

        subscribe_result = etree.Element('updateResult')
        subscribe_result.append(subscribe_item)
        return subscribe_result
    # end _update_publish

    @classmethod
    def _delete_publish(cls, del_root):
        from_name = escape(del_root[0].attrib['name'])
        if 'filter' in del_root.attrib:
            meta_name = del_root.attrib['filter']
            if len(del_root) == 1:
                link_key = meta_name
            elif len(del_root) == 2:
                to_name = escape(del_root[1].attrib['name'])
                link_key = '%s %s' % (meta_name, to_name)
            else:
                raise Exception("Unknown ifmap delete: %s" %
                                (etree.tostring(del_root)))

            link_keys = [link_key]

        else:  # delete all metadata on this ident or between pair of idents
            if len(del_root) == 1:
                link_keys = cls._graph[from_name]['links'].keys()
            elif len(del_root) == 2:
                to_name = escape(del_root[1].attrib['name'])
                link_keys = []
                if from_name in cls._graph:
                    all_link_keys = cls._graph[from_name]['links'].keys()
                    for link_key in all_link_keys:
                        link_info = cls._graph[from_name]['links'][link_key]
                        if 'other' in link_info:
                            if link_key.split()[1] == to_name:
                                link_keys.append(link_key)
            else:
                raise Exception("Unknown ifmap delete: %s" %
                                (etree.tostring(del_root)))

        subscribe_result = etree.Element('deleteResult')
        for link_key in link_keys:
            subscribe_item = etree.Element('resultItem')
            subscribe_item.extend(deepcopy(del_root))
            link_info = cls._graph[from_name]['links'][link_key]
            # generate id1, id2, meta for poll for the case where
            # del of ident for all metas requested but we have a
            # ref meta to another ident
            if len(del_root) == 1 and 'other' in link_info:
                to_ident_elem = link_info['other']
                subscribe_item.append(to_ident_elem)
            subscribe_item.append(deepcopy(link_info['meta']))
            subscribe_result.append(subscribe_item)
            if 'other' in link_info:
                other_name = escape(link_info['other'].attrib['name'])
                meta_name = re.sub(
                    "{.*}", "contrail:", link_info['meta'][0].tag)
                rev_link_key = '%s %s' % (meta_name, from_name)
                from_type = from_name.split(':')[1]
                other_type = other_name.split(':')[1]
                if other_name in cls._graph:
                    del cls._graph[other_name]['links'][rev_link_key]
                    if not cls._graph[other_name]['links']:
                        del cls._graph[other_name]
            del cls._graph[from_name]['links'][link_key]

        # delete ident if no links left
        if from_name in cls._graph and not cls._graph[from_name]['links']:
            del cls._graph[from_name]

        if len(subscribe_result) == 0:
            subscribe_item = etree.Element('resultItem')
            subscribe_item.extend(deepcopy(del_root))
            subscribe_result.append(subscribe_item)

        return subscribe_result
    # end _delete_publish

    @staticmethod
    def call(method, body):
        cls = FakeIfmapClient
        if method == 'publish':
            pub_env = cls._PUBLISH_ENVELOPE % {
                'body': body._PublishRequest__operations}
            pub_env = pub_env.encode('utf-8')
            env_root = etree.fromstring(pub_env)
            poll_result = etree.Element('pollResult')
            for pub_root in env_root[0]:
                #            pub_root = env_root[0][0]
                if pub_root.tag == 'update':
                    subscribe_result = cls._update_publish(pub_root)
                elif pub_root.tag == 'delete':
                    subscribe_result = cls._delete_publish(pub_root)
                else:
                    raise Exception(
                        "Unknown ifmap publish: %s"
                        % (etree.tostring(pub_root)))
                poll_result.append(subscribe_result)

            cls._published_messages.append(poll_result)
            for sl in cls._subscribe_lists:
                if sl is not None:
                    sl.put(poll_result)
            result = etree.Element('publishReceived')
            result_env = cls._RSP_ENVELOPE % {'result': etree.tostring(result)}
            return result_env
        elif method == 'search':
            # grab ident string; lookup graph with match meta and return
            srch_id_str = body._SearchRequest__identifier
            mch = re.match('<identity name="(.*)" type', srch_id_str)
            start_name = mch.group(1)
            match_links = body._SearchRequest__parameters['match-links']

            all_link_keys = set()
            for match_link in match_links.split(' or '):
                link_keys = set(
                    [link_key for link_key in cls._graph[start_name]
                     ['links'].keys()
                     if re.match(match_link, link_key)])
                all_link_keys |= link_keys

            result_items = []
            for link_key in all_link_keys:
                r_item = etree.Element('resultItem')
                link_info = cls._graph[start_name]['links'][link_key]
                if 'other' in link_info:
                    r_item.append(cls._graph[start_name]['ident'])
                    r_item.append(link_info['other'])
                    r_item.append(link_info['meta'])
                else:
                    r_item.append(cls._graph[start_name]['ident'])
                    r_item.append(link_info['meta'])

                result_items.append(copy.deepcopy(r_item))

            search_result = etree.Element('searchResult')
            search_result.extend(result_items)
            search_str = etree.tostring(search_result)
            search_env = cls._RSP_ENVELOPE % {'result': search_str}

            return search_env

            #ifmap_cf = CassandraCFs.get_cf('ifmap_id_table')
            #srch_uuid = ifmap_cf.get(ifmap_id)['uuid']
            #uuid_cf = CassandraCFs.get_cf('uuid_table')
            #obj_json = uuid_cf.get(srch_uuid)['obj_json']
        elif method == 'poll':
            session_id = int(body._PollRequest__session_id)
            item = cls._subscribe_lists[session_id].get(True)
            poll_str = etree.tostring(item)
            poll_env = cls._RSP_ENVELOPE % {'result': poll_str}
            return poll_env
        elif method == 'newSession':
            result = etree.Element('newSessionResult')
            result.set("session-id", str(len(cls._subscribe_lists)))
            result.set("ifmap-publisher-id", "111")
            result.set("max-poll-result-size", "7500000")
            result_env = cls._RSP_ENVELOPE % {'result': etree.tostring(result)}
            cls._subscribe_lists.append(None)
            return result_env
        elif method == 'subscribe':
            session_id = int(body._SubscribeRequest__session_id)
            subscriber_queue = Queue.Queue()
            for msg in cls._published_messages:
                subscriber_queue.put(msg)
            cls._subscribe_lists[session_id] = subscriber_queue
            result = etree.Element('subscribeReceived')
            result_env = cls._RSP_ENVELOPE % {'result': etree.tostring(result)}
            return result_env
        else:
            print method
    # end call

    @staticmethod
    def call_async_result(method, body):
        return FakeIfmapClient.call(method, body)

# end class FakeIfmapClient


class FakeKombu(object):
    _queues = {}

    class Exchange(object):
        def __init__(self, *args, **kwargs):
            pass

        def _new_queue(self, q_name, q_obj):
            FakeKombu._queues[q_name] = q_obj
        # end __init__
    # end Exchange

    class Queue(object):

        class Message(object):
            def __init__(self, msg_dict):
                self.payload = msg_dict
            # end __init__

            def ack(self, *args, **kwargs):
                pass
            # end ack

        # end class Message

        def __init__(self, entity, q_name, q_exchange, **kwargs):
            self._sync_q = gevent.queue.Queue()
            self._name = q_name
            self._exchange = q_exchange
            self._exchange._new_queue(q_name, self._sync_q)
        # end __init__

        def __call__(self, *args):
            class BoundQueue(object):
                def delete(self):
                    pass
                # end delete
            return BoundQueue()
        # end __call__

        def put(self, msg_dict, serializer):
            msg_obj = self.Message(msg_dict)
            self._sync_q.put(copy.deepcopy(msg_obj))
        # end put

        def get(self):
            rv = self._sync_q.get()
            # In real systems, rabbitmq is little slow, hence add some wait to mimic
            gevent.sleep(0.001)
            return rv
        # end get

    # end class Queue

    class Connection(object):
        def __init__(self, *args, **kwargs):
            pass
        # end __init__

        def channel(self):
            pass
        # end channel

        def close(self):
            pass
        # end close

        def ensure_connection(self):
            pass
        # end ensure_connection

        def connect(self):
            pass
        # end connection

        def _info(self):
            pass
        # end _info

        def drain_events(self):
            gevent.sleep(0.001)
        # end drain_events

        @property
        def connection_errors(self):
            return (Exception,)

        @property
        def channel_errors(self):
            return (Exception, )
    # end class Connection

    class Consumer(object):
        def __init__(self, *args, **kwargs):
            self.queues = kwargs['queues']
            self.callbacks = kwargs['callbacks']
        # end __init__

        def consume(self):
            while True:
                try:
                    msg = self.queues.get()
                    for c in self.callbacks:
                        c(msg.payload, msg)
                except Exception:
                    pass
        # end consume

        def close(self):
            pass
        # end close

    # end class Consumer

    class Producer(object):
        def __init__(self, *args, **kwargs):
            self.exchange = kwargs['exchange']
        # end __init__

        def publish(self, payload):
            for q in FakeKombu._queues.values():
                msg_obj = FakeKombu.Queue.Message(payload)
                q.put(msg_obj, None)
        #end publish

        def close(self):
            pass
        # end close

    # end class Producer

    @classmethod
    def reset(cls):
        cls._queues = {}
# end class FakeKombu

class FakeRedis(object):
    class Pubsub(object):
        def __init__(self, *args, **kwargs):
            self._event = gevent.event.Event()
        # end __init__

        def subscribe(self, *args, **kwargs):
            pass
        # end subscribe

        def listen(self, *args, **kwargs):
            self._event.wait()
        # end listen
    # end FakePubsub

    def __init__(self, *args, **kwargs):
        self._kv_store = {}
    # end __init__

    def pubsub(self, *args, **kwargs):
        return FakeRedis.Pubsub()
    # end pubsub

    def publish(self, *args, **kwargs):
        pass
    # end publish

    def set(self, key, value):
        self._kv_store[key] = deepcopy(value)
    # end set

    def get(self, key):
        return deepcopy(self._kv_store[key])
    # end get

    def delete(self, keys):
        for key in keys:
            try:
                del self._kv_store[key]
            except KeyError:
                pass
    # end delete

    def setnx(self, key, value):
        self.set(key, deepcopy(value))
        return True
    # end setnx

    def hexists(self, key, hkey):
        if key in self._kv_store:
            if hkey in self._kv_store[key]:
                return True

        return False
    # end hexists

    def hset(self, key, hkey, value):
        if key not in self._kv_store:
            self._kv_store[key] = {}

        self._kv_store[key][hkey] = deepcopy(value)
    # end hset

    def hget(self, key, hkey):
        if key not in self._kv_store:
            return json.dumps(None)
        if hkey not in self._kv_store[key]:
            return json.dumps(None)

        return deepcopy(self._kv_store[key][hkey])
    # end hget

    def hgetall(self, key):
        return deepcopy(self._kv_store[key])
    # end hgetall

    def hdel(self, key, hkey):
        del self._kv_store[key][hkey]
    # end hdel

# end FakeRedis

class FakeExtensionManager(object):
    _entry_pt_to_classes = {}
    class FakeExtObj(object):
        def __init__(self, cls, *args, **kwargs):
            self.obj = cls(*args, **kwargs)
            self.name = repr(cls)

    def __init__(self, child, ep_name, **kwargs):
        if ep_name not in self._entry_pt_to_classes:
            return

        classes = self._entry_pt_to_classes[ep_name]
        self._ep_name = ep_name
        self._ext_objs = []
        for cls in classes:
            ext_obj = FakeExtensionManager.FakeExtObj(cls, **kwargs) 
            self._ext_objs.append(ext_obj)
    # end __init__

    def names(self):
        return [e.name for e in self._ext_objs]

    def map(self, cb):
        for ext_obj in self._ext_objs:
            cb(ext_obj)

    def map_method(self, method_name, *args, **kwargs):
        for ext_obj in self._ext_objs:
            method = getattr(ext_obj.obj, method_name, None)
            if not method:
                continue
            method(*args, **kwargs)
# end class FakeExtensionManager


class FakeKeystoneClient(object):
    class Tenants(object):
        _tenants = {}
        def add_tenant(self, id, name):
            self.id = id
            self.name = name
            self._tenants[id] = self

        def list(self):
            return self._tenants.values()

        def get(self, id):
            return self._tenants[str(uuid.UUID(id))]

    def __init__(self, *args, **kwargs):
        self.tenants = FakeKeystoneClient.Tenants()
        pass

# end class FakeKeystoneClient

fake_keystone_client = FakeKeystoneClient()
def get_keystone_client(*args, **kwargs):
    return fake_keystone_client



def get_free_port():
    tmp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    tmp_sock.bind(('', 0))
    free_port = tmp_sock.getsockname()[1]
    tmp_sock.close()

    return free_port
# end get_free_port


def block_till_port_listened(server_ip, server_port):
    svr_running = False
    while not svr_running:
        try:
            s = socket.create_connection((server_ip, server_port))
            s.close()
            svr_running = True
        except Exception as err:
            if err.errno == errno.ECONNREFUSED:
                print "port %s not up, retrying in 2 secs" % (server_port)
                gevent.sleep(2)
# end block_till_port_listened


def Fake_uuid_to_time(time_uuid_in_db):
    ts = time.mktime(time_uuid_in_db.timetuple())
    return ts
# end of Fake_uuid_to_time

class FakeKazooClient(object):
    class Election(object):
        __init__ = stub
        def run(self, cb, func, *args, **kwargs):
            cb(func, *args, **kwargs)

    def __init__(self, *args, **kwargs):
        self.add_listener = stub
        self.start = stub
        self._values = {}
        self.state = KazooState.CONNECTED
    # end __init__

    def create(self, path, value='', *args, **kwargs):
        if path in self._values:
            raise ResourceExistsError(path, str(self._values[path]))
        self._values[path] = value
    # end create

    def get(self, path):
        return self._values[path]
    # end get

    def delete(self, path, recursive=False):
        if not recursive:
            try:
                del self._values[path]
            except KeyError:
                raise kazoo.exceptions.NoNodeError()
        else:
            for path_key in self._values.keys():
                if path in path_key:
                    del self._values[path_key]
    # end delete

class ZookeeperClientMock(object):

    def __init__(self, *args, **kwargs):
        self._count = 0
        self._values = {}
    # end __init__

    def is_connected(self):
        return True

    def alloc_from(self, path, max_id):
        self._count = self._count + 1
        return self._count
    # end alloc_from

    def alloc_from_str(self, path, value=''):
        self._count = self._count + 1
        zk_val = "%(#)010d" % {'#': self._count}
        self._values[path + zk_val] = value
        return zk_val
    # end alloc_from_str

    def delete(self, path):
        try:
            del self._values[path]
        except KeyError:
            raise kazoo.exceptions.NoNodeError()
    # end delete

    def read(self, path):
        try:
            return self._values[path]
        except Exception as err:
            raise pycassa.NotFoundException
    # end read

    def get_children(self, path):
        return []
    # end get_children

    def read_node(self, path):
        try:
            return self.read(path)
        except pycassa.NotFoundException:
            return None
    # end read_node

    def create_node(self, path, value=''):
        if path in self._values:
            raise ResourceExistsError(path, str(self._values[path]))
        self._values[path] = value
    # end create_node

    def delete_node(self, path, recursive=False):
        if not recursive:
            try:
                del self._values[path]
            except KeyError:
                raise kazoo.exceptions.NoNodeError()
        else:
            for path_key in self._values.keys():
                if path in path_key:
                    del self._values[path_key]
    # end delete_node

    def master_election(self, path, pid, func, *args, **kwargs):
        func(*args, **kwargs)
    # end master_election

# end Class ZookeeperClientMock

  
class FakeNetconfManager(object):
    def __init__(self, *args, **kwargs):
        self.configs = []

    def __enter__(self):
        return self
    __exit__ = stub

    def edit_config(self, target, config, test_option, default_operation):
        self.configs.append(config)

    commit = stub
# end FakeNetconfManager

netconf_managers = {}
def fake_netconf_connect(host, *args, **kwargs):
    return netconf_managers.setdefault(host, FakeNetconfManager(args, kwargs))
