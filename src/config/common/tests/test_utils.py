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
import uuid
import six
import contextlib
from lxml import etree
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
from cfgm_common.imid import escape, unescape

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
        columns = kwargs.get('columns', None)
        column_start = kwargs.get('column_start', None)
        column_finish = kwargs.get('column_finish', None)
        column_count = kwargs.get('column_count', 0)
        include_timestamp = kwargs.get('include_timestamp', False)

        for key in self._rows:
            yield (key, self.get(key, columns, column_start, column_finish,
                     column_count, include_timestamp))
    # end get_range

    def _column_within_range(self, column_name, column_start, column_finish):
        if type(column_start) is tuple:
            for i in range(len(column_start), len(column_name)):
                column_start = column_start + (column_name[i],)
        if type(column_finish) is tuple:
            for i in range(len(column_finish), len(column_name)):
                column_finish = column_finish + (column_name[i],)

        if column_start and column_name < column_start:
            return False
        if column_finish and column_name > column_finish:
            return False

        return True
    # end _column_within_range

    def get(
        self, key, columns=None, column_start=None, column_finish=None,
            column_count=0, include_timestamp=False, include_ttl=False):
        if not key in self._rows:
            raise pycassa.NotFoundException

        if columns:
            col_dict = {}
            for col_name in columns:
                try:
                    col_value = self._rows[key][col_name][0]
                except KeyError:
                    if len(columns) > 1:
                        continue
                    else:
                        raise pycassa.NotFoundException
                if include_timestamp or include_ttl:
                    ret = (col_value,)
                    if include_timestamp:
                        col_tstamp = self._rows[key][col_name][1]
                        ret += (col_tstamp,)
                    if include_ttl:
                        col_ttl = self._rows[key][col_name][2]
                        ret += (col_ttl,)
                    col_dict[col_name] = ret
                else:
                    col_dict[col_name] = col_value
        else:
            col_dict = {}
            for col_name in self._rows[key].keys():
                if not self._column_within_range(col_name,
                                    column_start, column_finish):
                    continue

                col_value = self._rows[key][col_name][0]
                if include_timestamp or include_ttl:
                    ret = (col_value,)
                    if include_timestamp:
                        col_tstamp = self._rows[key][col_name][1]
                        ret += (col_tstamp,)
                    if include_ttl:
                        col_ttl = self._rows[key][col_name][2]
                        ret += (col_ttl,)
                    col_dict[col_name] = ret
                else:
                    col_dict[col_name] = col_value

        sorted_col_dict = OrderedDict(
            (k, col_dict[k]) for k in sorted(col_dict))
        return sorted_col_dict
    # end get

    def multiget(
        self, keys, columns=None, column_start=None, column_finish=None,
            column_count=0, include_timestamp=False):
        result = {}
        for key in keys:
            try:
                col_dict = self.get(key, columns, column_start, column_finish,
                                    column_count, include_timestamp)
                result[key] = col_dict
            except pycassa.NotFoundException:
                pass

        return result
    # end multiget

    def insert(self, key, col_dict, ttl=None):
        if key not in self._rows:
            self._rows[key] = {}

        tstamp = datetime.now()
        for col_name in col_dict.keys():
            self._rows[key][col_name] = (col_dict[col_name], tstamp, ttl)

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
             include_timestamp=False, include_ttl=False):
        col_names = []
        if key in self._rows:
            col_names = self._rows[key].keys()

        for col_name in col_names:
            if not self._column_within_range(col_name,
                                column_start, column_finish):
                continue

            col_value = self._rows[key][col_name][0]
            if include_timestamp or include_ttl:
                ret = (col_value,)
                if include_timestamp:
                    col_tstamp = self._rows[key][col_name][1]
                    ret += (col_tstamp,)
                if include_ttl:
                    col_ttl = self._rows[key][col_name][2]
                    ret += (col_ttl,)
                yield (col_name, ret)
            else:
                yield (col_name, col_value)

    # end xget

    def get_count(self, key, column_start=None, column_finish=None):
        col_names = []
        if key in self._rows:
            col_names = self._rows[key].keys()

        counter = 0
        for col_name in col_names:
            if self._column_within_range(col_name,
                                column_start, column_finish):
                counter += 1

        return counter
    # end get_count

    def batch(self):
        return self
    # end batch

    def send(self):
        pass
    # end send

    @contextlib.contextmanager
    def patch_row(self, key, new_columns=None):
        if key in self._rows:
            row_existed = True
            orig_cols = self._rows[key]
            if new_columns is None:
                # simulates absence of key in cf
                del self._rows[key]
            else:
                self._rows[key] = new_columns
        else: # row didn't exist, create one
            row_existed = False
            self.insert(key, new_columns)

        try:
            yield
        finally:
            if row_existed:
                self._rows[key] = orig_cols
            else:
                del self._rows[key]
    #end patch_row
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
        to_name = None
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
        return subscribe_result, (from_name, to_name)
    # end _update_publish

    @classmethod
    def _delete_publish(cls, del_root):
        from_name = escape(del_root[0].attrib['name'])
        to_name = None
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
            meta_name = re.sub(
                "{.*}", "contrail:", link_info['meta'][0].tag)
            if 'other' in link_info:
                other_name = escape(link_info['other'].attrib['name'])
                rev_link_key = '%s %s' % (meta_name, from_name)
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

        return subscribe_result, (from_name, to_name)
    # end _delete_publish

    @staticmethod
    def call(client, method, body):
        cls = FakeIfmapClient
        if method == 'publish':
            pub_env = cls._PUBLISH_ENVELOPE % {
                'body': body._PublishRequest__operations}
            pub_env = pub_env.encode('utf-8')
            env_root = etree.fromstring(pub_env)
            poll_result = etree.Element('pollResult')
            _items = []
            for pub_root in env_root[0]:
                #            pub_root = env_root[0][0]
                if pub_root.tag == 'update':
                    subscribe_result, info = cls._update_publish(pub_root)
                    _items.append(('update', subscribe_result, info))
                elif pub_root.tag == 'delete':
                    subscribe_result, info = cls._delete_publish(pub_root)
                    _items.append(('delete', subscribe_result, info))
                else:
                    raise Exception(
                        "Unknown ifmap publish: %s"
                        % (etree.tostring(pub_root)))
                poll_result.append(subscribe_result)

            for sl in cls._subscribe_lists:
                if sl is not None:
                    sl.put(poll_result)

            for (oper, result, info) in _items:
                if oper == 'update':
                    search_result = deepcopy(result)
                    search_result.tag = 'searchResult'
                    cls._published_messages.append((search_result, info))
                else:
                    cls._published_messages = [
                        (r,i) for (r, i) in cls._published_messages
                        if (i != info and i != info [::-1])]
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
            poll_result = etree.Element('pollResult')
            for result_item,_ in cls._published_messages:
                poll_result.append(result_item)
            subscriber_queue.put(poll_result)
            cls._subscribe_lists[session_id] = subscriber_queue
            result = etree.Element('subscribeReceived')
            result_env = cls._RSP_ENVELOPE % {'result': etree.tostring(result)}
            return result_env
        else:
            print method
    # end call

    @staticmethod
    def call_async_result(client, method, body):
        return FakeIfmapClient.call(client, method, body)

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
        class ConnectionException(Exception): pass
        class ChannelException(Exception): pass

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
            return (self.ConnectionException, )

        @property
        def channel_errors(self):
            return (self.ChannelException, )
    # end class Connection

    class Consumer(object):
        def __init__(self, *args, **kwargs):
            self.queues = kwargs['queues']
            self.callbacks = kwargs['callbacks']
        # end __init__

        def consume(self):
            while True:
                msg = self.queues.get()
                try:
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
    _ext_objs = []
    class FakeExtObj(object):
        def __init__(self, entry_pt, cls, *args, **kwargs):
            self.entry_pt = entry_pt
            self.obj = cls(*args, **kwargs)
            self.name = repr(cls)

    def __init__(self, child, ep_name, **kwargs):
        if ep_name not in self._entry_pt_to_classes:
            return

        classes = self._entry_pt_to_classes[ep_name]
        self._ep_name = ep_name
        for cls in classes or []:
            ext_obj = FakeExtensionManager.FakeExtObj(
                ep_name, cls, **kwargs) 
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

    @classmethod
    def get_extension_objects(cls, entry_pt):
        return [e.obj for e in cls._ext_objs if e.entry_pt == entry_pt]

    @classmethod
    def reset(cls):
        cls._ext_objs = []
# end class FakeExtensionManager

class MiniResp(object):
    def __init__(self, error_message, env, headers=[]):
        # The HEAD method is unique: it must never return a body, even if
        # it reports an error (RFC-2616 clause 9.4). We relieve callers
        # from varying the error responses depending on the method.
        if env['REQUEST_METHOD'] == 'HEAD':
            self.body = ['']
        else:
            self.body = [error_message]
        self.headers = list(headers)
        self.headers.append(('Content-type', 'text/plain'))

"""
Fake Keystone Middleware.
Used in API server request pipeline to validate user token
Typically first app in the pipeline
"""
class FakeAuthProtocol(object):
    def __init__(self, app, conf, *args, **kwargs):
        self.app = app
        self.conf = conf
 
        auth_protocol = conf['auth_protocol']
        auth_host = conf['auth_host']
        auth_port = conf['auth_port']
        self.request_uri = '%s://%s:%s' % (auth_protocol, auth_host, auth_port)
        self.auth_uri = self.request_uri
        # print 'FakeAuthProtocol init: auth-uri %s, conf %s' % (self.auth_uri, self.conf)

    def get_admin_token(self):
        # token format admin-name, tenat-name, role
        token_dict = {
            'X-User': self.conf['admin_user'],
            'X-User-Name': self.conf['admin_user'],
            'X-Project-Name': self.conf['admin_tenant_name'],
            'X-Domain-Name' : 'default-domain',
            'X-Role': 'admin',
        }
        rval = json.dumps(token_dict)
        # print '**** generated admin token %s ****' % rval
        return rval

    def _header_to_env_var(self, key):
        return 'HTTP_%s' % key.replace('-', '_').upper()

    def _get_header(self, env, key, default=None):
        """Get http header from environment."""
        env_key = self._header_to_env_var(key)
        return env.get(env_key, default)

    def _add_headers(self, env, headers):
        """Add http headers to environment."""
        for (k, v) in six.iteritems(headers):
            env_key = self._header_to_env_var(k)
            env[env_key] = v

    def _validate_user_token(self, user_token, env, retry=True):
        return user_token

    def _build_user_headers(self, token_info):
        """Convert token object into headers."""
        """
        rval = {
            'X-Identity-Status': 'Confirmed',
            'X-Domain-Id': domain_id,
            'X-Domain-Name': domain_name,
            'X-Project-Id': project_id,
            'X-Project-Name': project_name,
            'X-Project-Domain-Id': project_domain_id,
            'X-Project-Domain-Name': project_domain_name,
            'X-User-Id': user_id,
            'X-User-Name': user_name,
            'X-User-Domain-Id': user_domain_id,
            'X-User-Domain-Name': user_domain_name,
            'X-Role': roles,
            # Deprecated
            'X-User': user_name,
            'X-Tenant-Id': project_id,
            'X-Tenant-Name': project_name,
            'X-Tenant': project_name,
            'X-Role': roles,
        }
        """
        rval = json.loads(token_info)
        return rval

    def _reject_request(self, env, start_response):
        """Redirect client to auth server.

        :param env: wsgi request environment
        :param start_response: wsgi response callback
        :returns HTTPUnauthorized http response

        """
        headers = [('WWW-Authenticate', 'Keystone uri=\'%s\'' % self.auth_uri)]
        resp = MiniResp('Authentication required', env, headers)
        start_response('401 Unauthorized', resp.headers)
        return resp.body

    def __call__(self, env, start_response):
        """Handle incoming request.

        Authenticate send downstream on success. Reject request if
        we can't authenticate.

        """
        # print 'FakeAuthProtocol: Authenticating user token'
        user_token = self._get_header(env, 'X-Auth-Token')
        if user_token:
            # print '****** user token %s ***** ' % user_token
            pass
        else:
            # print 'Missing token or Unable to authenticate token'
            return self._reject_request(env, start_response)

        token_info = self._validate_user_token(user_token, env)
        # env['keystone.token_info'] = token_info
        user_headers = self._build_user_headers(token_info)
        self._add_headers(env, user_headers)
        return self.app(env, start_response)

fake_keystone_auth_protocol = None
def get_keystone_auth_protocol(*args, **kwargs):
    global fake_keystone_auth_protocol
    if not fake_keystone_auth_protocol:
        fake_keystone_auth_protocol = FakeAuthProtocol(*args[1:], **kwargs)

    return fake_keystone_auth_protocol
#end get_keystone_auth_protocol

class FakeKeystoneClient(object):
    class Domains(object):
        _domains = {}
        def add_domain(self, id, name):
            self.id = id
            self.name = name
            self._domains[id] = self

        def create(self, name, id=None):
            self.name = name
            self.id = str(id or uuid.uuid4())
            self._domains[id] = self

        def list(self):
            return self._domains.values()

        def get(self, id):
            return self._domains[str(uuid.UUID(id))]

    class Tenants(object):
        _tenants = {}
        def add_tenant(self, id, name):
            self.id = id
            self.name = name
            self._tenants[id] = self

        def create(self, name, id=None):
            self.name = name
            self.id = str(id or uuid.uuid4())
            self._tenants[id] = self

        def list(self):
            return self._tenants.values()

        def get(self, id):
            return self._tenants[str(uuid.UUID(id))]

    class Users(object):
        _users = {}
        def create(self, name, password, foo, tenant_id):
            self.name = name
            self.password = password
            self.tenant_id = tenant_id
            self._users[name] = self

        def list(self):
            return self._users.values()

        def get(self, name):
            for x in self._users.values():
                if x.name == name:
                    return x
            return None

    class Roles(object):
        _roles = {}
        _roles_map = {}
        def create(self, name):
            self.name = name
            self._roles[name] = self

        def list(self):
            return self._roles.values()

        def get_user_role(self, username, tenant_id):
            return self._roles_map[username][tenant_id]

        def add_user_role(self, uobj, robj, tobj):
            if uobj.name not in self._roles_map:
                self._roles_map[uobj.name] = {}
            self._roles_map[uobj.name][tobj.name] = robj.name

    def __init__(self, *args, **kwargs):
        self.tenants = FakeKeystoneClient.Tenants()
        self.domains = FakeKeystoneClient.Domains()
        self.users = FakeKeystoneClient.Users()
        self.roles = FakeKeystoneClient.Roles()
        pass

    def user_role(self, username, tenant_id):
        return self.roles.get_user_role(username, tenant_id)
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

class ZnodeStat(object):
    def __init__(self, ctime):
        self.ctime = ctime
# end ZnodeStat

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
            raise ResourceExistsError(
                path, str(self._values[path][0]), 'zookeeper')
        self._values[path] = (value, ZnodeStat(time.time()*1000))
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
        self._values[path + zk_val] = (value, ZnodeStat(time.time()*1000))
        return zk_val
    # end alloc_from_str

    def delete(self, path):
        try:
            del self._values[path]
        except KeyError:
            raise kazoo.exceptions.NoNodeError()
    # end delete

    def read(self, path, include_timestamp=False):
        try:
            if include_timestamp:
                return self._values[path]
            return self._values[path][0]
        except Exception as err:
            raise pycassa.NotFoundException
    # end read

    def get_children(self, path):
        return []
    # end get_children

    def read_node(self, path, include_timestamp=False):
        try:
            return self.read(path, include_timestamp)
        except pycassa.NotFoundException:
            return None
    # end read_node

    def create_node(self, path, value=''):
        if path in self._values:
            raise ResourceExistsError(
                path, str(self._values[path][0], 'zookeeper'))
        self._values[path] = (value, ZnodeStat(time.time()*1000))
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


class FakeVncApiStatsLog(object):
    _all_logs = []
    send = stub
    def __init__(self, *args, **kwargs):
        FakeVncApiStatsLog._all_logs.append(kwargs['api_stats'])

    @classmethod
    def _print(cls):
        for log in cls._all_logs:
            x = copy.deepcopy(log.__dict__)
            #body = x.pop('body')
            #pprint(json.loads(body))
            pprint(x)
            print "\n"
# class FakeVncApiStatsLog
