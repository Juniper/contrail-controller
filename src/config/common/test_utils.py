#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import gevent
import os
import sys
import pdb
from pprint import pprint
import functools
import socket
import time
import errno
import re
import copy
from lxml import etree
try:
    from collections import OrderedDict
except ImportError:
    from ordereddict import OrderedDict
import pycassa
import Queue
import kombu
from copy import deepcopy
from datetime import datetime
from pycassa.util import *
from vnc_api import *
from novaclient import exceptions as nc_exc


def stub(*args, **kwargs):
    pass


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

# end CassandraCFs


class FakeCF(object):

    def __init__(*args, **kwargs):
        self = args[0]
        self._name = args[3]
        self._rows = OrderedDict({})
        self.column_validators = {}
        CassandraCFs.add_cf(self._name, self)
    # end __init__

    def get_range(self, *args, **kwargs):
        for key in self._rows:
            yield (key, self.get(key))
    # end get_range

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
                if column_start and column_start not in col_name:
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
                result[key] = copy.deepcopy(self._rows[key])
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
            if column_start and column_start not in col_name:
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
        def find(ram):
            return None
    # end class flavors

    class images:

        @staticmethod
        def find(name):
            return None
    # end class images

    class servers:

        @staticmethod
        def create(name, image, flavor, nics):
            vm = vnc_api.VirtualMachine(name)
            FakeNovaClient.vnc_lib.virtual_machine_create(vm)
            for network in nics:
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
    # end class servers

    @staticmethod
    def delete_vm(vm):
        for if_ref in vm.get_virtual_machine_interfaces():
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
    _subscribe_lists = [Queue.Queue()]
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

    @staticmethod
    def initialize(*args, **kwargs):
        pass
    # end initialize

    @classmethod
    def _update_publish(cls, upd_root):
        subscribe_item = etree.Element('resultItem')
        subscribe_item.extend(deepcopy(upd_root))
        from_name = upd_root[0].attrib['name']
        if not from_name in cls._graph:
            cls._graph[from_name] = {'ident': upd_root[0], 'links': {}}

        if len(upd_root) == 2:
            meta_name = re.sub("{.*}", "contrail:", upd_root[1][0].tag)
            link_key = meta_name
            link_info = {'meta': upd_root[1]}
            cls._graph[from_name]['links'][link_key] = link_info
        elif len(upd_root) == 3:
            meta_name = re.sub("{.*}", "contrail:", upd_root[2][0].tag)
            to_name = upd_root[1].attrib['name']
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
        from_name = del_root[0].attrib['name']
        if 'filter' in del_root.attrib:
            meta_name = del_root.attrib['filter']
            if len(del_root) == 1:
                link_key = meta_name
            elif len(del_root) == 2:
                to_name = del_root[1].attrib['name']
                link_key = '%s %s' % (meta_name, to_name)
            else:
                raise Exception("Unknown ifmap delete: %s" %
                                (etree.tostring(del_root)))

            link_keys = [link_key]

        else:  # delete all metadata on this ident or between pair of idents
            if len(del_root) == 1:
                link_keys = cls._graph[from_name]['links'].keys()
            elif len(del_root) == 2:
                to_name = del_root[1].attrib['name']
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
            subscribe_item.append(deepcopy(link_info['meta']))
            subscribe_result.append(subscribe_item)
            if 'other' in link_info:
                other_name = link_info['other'].attrib['name']
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
            cls._subscribe_lists[session_id] = cls._subscribe_lists[0]
            result = etree.Element('subscribeReceived')
            result_env = cls._RSP_ENVELOPE % {'result': etree.tostring(result)}
            return result_env
        else:
            print method
    # end call

# end class FakeIfmapClient


class FakeKombu(object):
    _queues = {}

    class Exchange(object):
        def __init__(self, *args, **kwargs):
            pass
        # end __init__
    # end Exchange

    class Queue(object):
        _msg_obj_list = []

        class Message(object):
            def __init__(self, msg_dict):
                self.payload = msg_dict
            # end __init__

            def ack(self, *args, **kwargs):
                pass
            # end ack

        # end class Message

        def __init__(self, entity, q_name, q_exchange):
            self._name = q_name
            self._exchange = q_exchange
            FakeKombu._queues[q_name] = self
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
            self._msg_obj_list.append(msg_obj)
        # end put

        def dq_head(self):
            return self._msg_obj_list.pop(0)
        # end dq_head
    # end class Queue

    class Connection(object):
        class SimpleQueue(object):
            def __init__(self, q_obj):
                self._q_obj = q_obj
                self._q_add_event = gevent.event.Event()
            # end __init__

            def put(self, *args, **kwargs):
                self._q_obj.put(*args, **kwargs)
                self._q_add_event.set()
            # end put

            def get(self, *args, **kwargs):
                self._q_add_event.wait()
                return self._q_obj.dq_head()
            # end get

            def __enter__(self):
                return self
            # end __enter__

            def __exit__(self, *args, **kwargs):
                pass
            # end __exit__
        # end class SimpleQueue

        def __init__(self, *args, **kwargs):
            pass
        # end __init__

        def channel(self):
            pass
        # end channel
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
        del self._values[path]
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
        self._values[path] = value
    # end create_node

    def delete_node(self, path, recursive=False):
        if not recursive:
            del self._values[path]
        else:
            for path_key in self._values.keys():
                if path in path_key:
                    del self._values[path_key]
    # end delete_node
# end Class ZookeeperClientMock

