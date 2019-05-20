#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import gevent
import gevent.queue
import gevent.pywsgi
import gevent.monkey
gevent.monkey.patch_all()
import os
import sys
import logging
import json
from pprint import pprint
import functools
import socket
import time
import errno
import re
import copy
from cStringIO import StringIO
import uuid
import six
import contextlib
try:
    from collections import OrderedDict, defaultdict
except ImportError:
    from ordereddict import OrderedDict, defaultdict
import pycassa
import Queue
from collections import deque
from collections import namedtuple
import kombu
import kazoo
from kazoo.client import KazooState
from copy import deepcopy
from datetime import datetime
from pycassa.util import *
from vnc_api import vnc_api
from novaclient import exceptions as nc_exc

from cfgm_common.exceptions import ResourceExistsError, OverQuota

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

class FakeWSGIHandler(gevent.pywsgi.WSGIHandler):
    logger = logging.getLogger('FakeWSGIHandler')
    logger.addHandler(logging.FileHandler('api_server.log'))
    def __init__(self, socket, address, server):
        super(FakeWSGIHandler, self).__init__(socket, address, server)
        #server.log = open('api_server.log', 'a')
        class LoggerWriter(object):
            def write(self, message):
                FakeWSGIHandler.logger.log(logging.INFO, message)
        server.log = LoggerWriter()

class FakeSystemManager(object):
    _keyspaces = {}

    def __init__(*args, **kwargs):
        pass

    def create_keyspace(self, name, *args, **kwargs):
        if name not in self._keyspaces:
            self._keyspaces[name] = {}

    def list_keyspaces(self):
        return self._keyspaces.keys()

    def get_keyspace_properties(self, ks_name):
        return {'strategy_options': {'replication_factor': '1'}}

    def get_keyspace_column_families(self, keyspace):
        return self._keyspaces[keyspace]

    def create_column_family(self, keyspace, name, *args, **kwargs):
        self._keyspaces[keyspace][name] = {}

    def drop_keyspace(self, ks_name):
        try:
            del self._keyspaces[name]
        except KeyError:
            pass

    @classmethod
    @contextlib.contextmanager
    def patch_keyspace(cls, ks_name, ks_val=None):
        try:
            orig_ks_val = cls._keyspaces[ks_name]
            orig_existed = True
        except KeyError:
            orig_existed = False

        try:
            cls._keyspaces[ks_name] = ks_val
            yield
        finally:
            if orig_existed:
                cls._keyspaces[ks_name] = orig_ks_val
            else:
                del cls._keyspaces[ks_name]
    #end patch_keyspace
# end class FakeSystemManager


class CassandraCFs(object):
    _all_cfs = {}

    @classmethod
    def add_cf(cls, keyspace, cf_name, cf):
        CassandraCFs._all_cfs[keyspace + '_' + cf_name] = cf
    # end add_cf

    @classmethod
    def get_cf(cls, keyspace, cf_name):
        return CassandraCFs._all_cfs[keyspace + '_' + cf_name]
    # end get_cf

    @classmethod
    def reset(cls, cf_list=None):
        if cf_list:
            for name in cf_list:
                if name in cls._all_cfs:
                    del cls._all_cfs[name]
            return
        cls._all_cfs = {}
# end CassandraCFs

class FakeConnectionPool(object):

    def __init__(*args, **kwargs):
        self = args[0]
        if "keyspace" in kwargs:
            self.keyspace = kwargs['keyspace']
        else:
            self.keyspace = args[2]
    # end __init__
# end FakeConnectionPool

class PatchContext(object):
    def __init__(self, cf):
        self.cf = cf
        self.patches = [] # stack of patches
    # end __init__

    def patch(self, patch_list):
        cf = self.cf
        for patch_type, patch_info in patch_list:
            patched = {}
            if patch_type == 'row':
                patched['type'] = 'row'
                row_key, new_columns = patch_info
                patched['row_key'] = row_key
                if row_key in cf._rows:
                    patched['row_existed'] = True
                    patched['orig_cols'] = copy.deepcopy(cf._rows[row_key])
                    if new_columns is None:
                        # simulates absence of key in cf
                        del cf._rows[row_key]
                    else:
                        cf._rows[row_key] = new_columns
                else: # row didn't exist, create one
                    patched['row_existed'] = False
                    cf.insert(row_key, new_columns)
            elif patch_type == 'column':
                patched['type'] = 'column'
                row_key, col_name, col_val = patch_info
                patched['row_key'] = row_key
                patched['col_name'] = col_name
                if col_name in cf._rows[row_key]:
                    patched['col_existed'] = True
                    patched['orig_col_val'] = copy.deepcopy(
                        cf._rows[row_key][col_name])
                    if col_val is None:
                        # simulates absence of col
                        del cf._rows[row_key][col_name]
                    else:
                        cf.insert(row_key, {col_name: col_val})
                else: # column didn't exist, create one
                    patched['col_existed'] = False
                    cf.insert(row_key, {col_name: col_val})
            else:
                raise Exception(
                    "Unknown patch type %s in patching" %(patch_type))

            self.patches.append(patched)
    # end patch

    def unpatch(self):
        cf = self.cf
        for patched in reversed(self.patches):
            patch_type = patched['type']
            row_key = patched['row_key']
            if patch_type == 'row':
                if patched['row_existed']:
                    cf._rows[row_key] = patched['orig_cols']
                else:
                    del cf._rows[row_key]
            elif patch_type == 'column':
                col_name = patched['col_name']
                if patched['col_existed']:
                    cf._rows[row_key][col_name] = patched['orig_col_val']
                else:
                    del cf._rows[row_key][col_name]
            else:
                raise Exception(
                    "Unknown patch type %s in un-patching" %(patch_type))
    # end unpatch
# end PatchContext

class FakeCF(object):
    # 2 initializations for same CF get same contents
    _all_cf_rows = {}

    def __init__(*args, **kwargs):
        self = args[0]
        self._pool = args[2]
        self._name = args[3]
        self._ks_cf_name = '%s_%s' %(self._pool.keyspace, self._name)
        try:
            #old_cf = CassandraCFs.get_cf(self._pool.keyspace, self._name)
            #self._rows = old_cf._rows
            self._rows = self._all_cf_rows[self._ks_cf_name]
        except KeyError:
            self._all_cf_rows[self._ks_cf_name] = OrderedDict({})
            self._rows = self._all_cf_rows[self._ks_cf_name]

        self.column_validators = {}
        CassandraCFs.add_cf(self._pool.keyspace, self._name, self)
    # end __init__

    def get_range(self, *args, **kwargs):
        columns = kwargs.get('columns', None)
        column_start = kwargs.get('column_start', None)
        column_finish = kwargs.get('column_finish', None)
        column_count = kwargs.get('column_count', 0)
        include_timestamp = kwargs.get('include_timestamp', False)

        for key in self._rows:
            try:
                col_dict = self.get(key, columns, column_start, column_finish,
                                    column_count, include_timestamp)
                yield (key, col_dict)
            except pycassa.NotFoundException:
                pass
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

    def get(self, key, columns=None, column_start=None, column_finish=None,
            column_count=0, include_timestamp=False, include_ttl=False):
        if not isinstance(key, six.string_types):
            raise TypeError("A str or unicode value was expected, but %s "
                            "was received instead (%s)"
                            % (key.__class__.__name__, str(key)))
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

        if len(col_dict) == 0:
            raise pycassa.NotFoundException
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

        #tstamp = datetime.now()
        tstamp = (datetime.utcnow() - datetime(1970, 1, 1)).total_seconds()
        for col_name in col_dict.keys():
            self._rows[key][col_name] = (col_dict[col_name], tstamp, ttl)

    # end insert

    def remove(self, key, columns=None):
        try:
            if columns:
                # for each entry in col_name delete each that element
                for col_name in columns:
                    del self._rows[key][col_name]
            elif columns is None:
                del self._rows[key]
        except KeyError:
            # pycassa remove ignores non-existing keys
            pass
    # end remove

    def xget(self, key, column_start=None, column_finish=None,
             column_count=0, include_timestamp=False, include_ttl=False):
        try:
            col_dict = self.get(key,
                                column_start=column_start,
                                column_finish=column_finish,
                                column_count=column_count,
                                include_timestamp=include_timestamp,
                                include_ttl=include_ttl)
        except pycassa.NotFoundException:
            col_dict = {}

        for k, v in col_dict.items():
            yield (k, v)
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
    def patch_cf(self, new_contents=None):
        orig_contents = self._all_cf_rows[self._ks_cf_name]
        try:
            self._all_cf_rows[self._ks_cf_name] = new_contents
            yield
        finally:
            self._all_cf_rows[self._ks_cf_name] = orig_contents
    # end patch_cf

    @contextlib.contextmanager
    def patch_row(self, key, new_columns=None):
        ctx = PatchContext(self)
        try:
            ctx.patch([('row', (key, new_columns))])
            yield
        finally:
            ctx.unpatch()
    #end patch_row

    @contextlib.contextmanager
    def patch_column(self, key, col_name, col_val=None):
        ctx = PatchContext(self)
        try:
            ctx.patch([('column', (key, col_name, col_val))])
            yield
        finally:
            ctx.unpatch()
    # end patch_column

    @contextlib.contextmanager
    def patches(self, patch_list):
        ctx = PatchContext(self)
        try:
            ctx.patch(patch_list)
            yield
        finally:
            ctx.unpatch()
    # end patches
# end class FakeCF


class FakeNovaClient(object):

    vnc_lib = None

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
        for if_ref in (vm.get_virtual_machine_interfaces() or
                       vm.get_virtual_machine_interface_back_refs() or []):
            intf = FakeNovaClient.vnc_lib.virtual_machine_interface_read(
                id=if_ref['uuid'])
            for ip_ref in intf.get_instance_ip_back_refs() or []:
                FakeNovaClient.vnc_lib.instance_ip_delete(id=ip_ref['uuid'])
            FakeNovaClient.vnc_lib.virtual_machine_interface_delete(
                id=if_ref['uuid'])
        FakeNovaClient.vnc_lib.virtual_machine_delete(id=vm.uuid)
    # end delete_vm

    @classmethod
    def reset(cls):
        cls.vnc_lib = None
    # end vnc_lib
# end class FakeNovaClient


class FakeKombu(object):
    _exchange = defaultdict(dict)

    @classmethod
    def is_empty(cls, vhost, qname):
        _vhost = ''.join(vhost)
        for name, q in FakeKombu._exchange[_vhost].items():
            if name.startswith(qname) and q.qsize() > 0:
                return False
        return True
    # end is_empty

    @classmethod
    def new_queues(self, vhost, q_name, q_gevent_obj):
        FakeKombu._exchange[vhost][q_name] = q_gevent_obj
    # end new_queues

    class Exchange(object):
        def __init__(self, *args, **kwargs):
            self.exchange = args[1]
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
        # end __init__

        def __call__(self, *args):
            class BoundQueue(object):

                def __init__(self, parent):
                    self.parent_q = parent

                def delete(self):
                    self.parent_q.clear()
                # end delete
            return BoundQueue(self)
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

        def clear(self):
            try:
                while True:
                    self._sync_q.get_nowait()
            except Queue.Empty:
                pass
            self._sync_q = gevent.queue.Queue()
    # end class Queue

    class FakeChannel(object):
        def __init__(self, vhost):
            self.vhost = vhost
        # end __init__
    # end class Channel

    class Connection(object):
        class ConnectionException(Exception): pass
        class ChannelException(Exception): pass

        def __init__(self, *args, **kwargs):
            self._default_channel = None
            self.args = args
            self.vhost = args[1]
        # end __init__

        def channel(self):
            chan = FakeKombu.FakeChannel(self.vhost)
            return chan
        # end channel

        @property
        def default_channel(self):
            if self._default_channel is None:
                self._default_channel = self.channel()
            return self._default_channel

        def clone(self, **kwargs):
            return self.__class__(*self.args, **kwargs)
        # end clone

        def close(self):
            pass
        # end close

        def ensure_connection(self, *args):
            pass
        # end ensure_connection

        def connect(self):
            pass
        # end connection

        def _info(self):
            pass
        # end _info

        def drain_events(self, **kwargs):
            gevent.sleep(0.001)
        # end drain_events

        def as_uri(self, *args):
            return repr(self.args)
        # end as_uri

        def __enter__(self):
            return self
        # end __enter__

        def __exit__(self, *args):
            self.close()
        # end __exit__

        @property
        def connection_errors(self):
            return (self.ConnectionException, )

        @property
        def channel_errors(self):
            return (self.ChannelException, )

        @property
        def connected(self):
            return True

        def heartbeat_check(self):
            gevent.sleep(0.001)
    # end class Connection

    class Consumer(object):
        def __init__(self, *args, **kwargs):
            queues = kwargs['queues']
            self.queue = queues[0] if isinstance(queues, list) else queues
            self.callbacks = kwargs['callbacks']
            self.vhost = ''.join(args[1].vhost)
            FakeKombu._exchange[self.vhost][self.queue._name] \
                                            = self.queue._sync_q
        # end __init__

        def consume(self):
            if not self.queue:
                return
            while True:
                msg = self.queue.get()
                try:
                    for c in self.callbacks:
                        c(msg.payload, msg)
                except Exception:
                    pass
        # end consume

        def close(self):
            if self.queue:
                self.queue.clear()
                self.queue = None
        # end close

        def __enter__(self):
            self.consume()
            return self
        # end __enter__

        def __exit__(self, exc_type, exc_val, exc_tb):
            pass
        # end __exit__

    # end class Consumer

    class Producer(object):
        def __init__(self, *args, **kwargs):
            self.exchange = kwargs['exchange']
            self.vhost = ''.join(args[1].vhost)
        # end __init__

        def publish(self, payload):
            for q in FakeKombu._exchange[self.vhost].values():
                msg_obj = FakeKombu.Queue.Message(payload)
                q.put(msg_obj, None)
        #end publish

        def close(self):
            for q in FakeKombu._exchange[self.vhost].values():
                while True:
                    try:
                        q.get_nowait()
                    except Queue.Empty:
                        break
        # end close

    # end class Producer
    @classmethod
    def reset(cls, vhost):
        _vhost = ''.join(vhost)
        for name, gevent_q in cls._exchange[_vhost].items():
            del FakeKombu._exchange[_vhost][name]
        cls._exchange[_vhost].clear()
        cls._exchange = defaultdict(dict)
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

        self.request_uri = conf.get('auth_url')
        if not self.request_uri:
            auth_protocol = conf['auth_protocol']
            auth_host = conf['auth_host']
            auth_port = conf['auth_port']
            self.request_uri = '%s://%s:%s' % (auth_protocol, auth_host, auth_port)
        self.delay_auth_decision = conf.get('delay_auth_decision', False)
        self.auth_uri = self.request_uri
        # print 'FakeAuthProtocol init: auth-uri %s, conf %s' % (self.auth_uri, self.conf)

    def get_admin_token(self):
        # token format admin-name, tenat-name, role
        token_dict = {
            'X-User': self.conf['admin_user'],
            'X-User-Name': self.conf['admin_user'],
            'X-Project-Name': self.conf['admin_tenant_name'],
            'X-Domain-Name' : 'default-domain',
            'X-Role': 'cloud-admin',
        }
        rval = json.dumps(token_dict)
        # print '%%%% generated admin token %s %%%%' % rval
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

    # simulate keystone token
    def _fake_keystone_token(self, token_info):
        rval = json.loads(token_info)
        rval['token'] = {};
        rval['access'] = {}; rval['access']['user'] = {};
        rval['access']['user']['roles'] = [{'name': rval['X-Role']}]
        rval['token']['roles'] = [{'name': rval['X-Role']}]
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
            # print '%%%%%% user token %s %%%%% ' % user_token
            pass
        elif self.delay_auth_decision:
            self._add_headers(env, {'X-Identity-Status': 'Invalid'})
            return self.app(env, start_response)
        else:
            # print 'Missing token or Unable to authenticate token'
            return self._reject_request(env, start_response)

        token_info = self._validate_user_token(user_token, env)
        env['keystone.token_info'] = self._fake_keystone_token(token_info)
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
    @property
    def version(self):
        return 'v2'

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

        def delete_tenant(self, id):
            del self._tenants[id]

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

    def user_role(self, username, tenant_id):
        return self.roles.get_user_role(username, tenant_id)
# end class FakeKeystoneClient

fake_keystone_client = FakeKeystoneClient()
def get_keystone_client(*args, **kwargs):
    return fake_keystone_client


#
# Find two consecutive free ports such that even port is greater than odd port
# Return the even port and socket locked to the odd port
#
def get_free_port(allocated_sockets):
    single_port_list = []
    tmp_sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    while (1):
        tmp_sock1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            tmp_sock1.bind(('', 0))
        except:
            #Bail out as all ports exhausted.
            for tmp_sock in single_port_list:
                tmp_sock.close()
            raise
            return None, 0

        free_port1 = tmp_sock1.getsockname()[1]
        if free_port1 % 2:
            #We have odd port, check whether next even port free
            try:
                tmp_sock2.bind(('', free_port1 + 1))
            except:
                single_port_list.append(tmp_sock1)
                continue
        else:
            #We have even port, check whether next odd port free
            try:
                tmp_sock2.bind(('', free_port1 - 1))
            except:
                single_port_list.append(tmp_sock1)
                continue
        free_port2 = tmp_sock2.getsockname()[1]
        break

    #we have found our twin ports, release the singles now
    for tmp_sock in single_port_list:
        tmp_sock.close()

    #keep the odd port locked and return free port
    if free_port1 % 2:
        odd_port, odd_sock = free_port1, tmp_sock1
        even_port, even_sock = free_port2, tmp_sock2
    else:
        odd_port, odd_sock = free_port2, tmp_sock2
        even_port, even_sock = free_port1, tmp_sock1

    even_sock.close()
    allocated_sockets.append(odd_sock)
    return even_port
# end get_free_port

def block_till_port_listened(server_ip, server_port, retries=30):
    tries = 0
    while tries < retries:
        try:
            s = socket.create_connection((server_ip, server_port))
            s.close()
            return
        except Exception as err:
            if err.errno == errno.ECONNREFUSED:
                tries += 1
                print("port %s not up, retrying in 2 secs, %d tries remaining"
                      % (server_port, retries-tries))
                gevent.sleep(2)
    raise Exception("port %s not up after %d retries" % (server_port, retries))
# end block_till_port_listened

def Fake_uuid_to_time(time_uuid_in_db):
    ts = time.mktime(time_uuid_in_db.timetuple())
    return ts
# end of Fake_uuid_to_time


class ZnodeStat(namedtuple('ZnodeStat', 'ctime')):
    pass


def zk_scrub_path(path):
    # remove trailing slashes if not root
    if len(path) == 1:
        return path
    return path.rstrip('/')
# end zk_scrub_path


class FakeKazooClient(object):
    _values = {}

    class Election(object):
        _locks = {}

        def __init__(self, path, identifier):
            self.path = zk_scrub_path(path)
            if self.path not in self._locks:
                self._locks[self.path] = gevent.lock.Semaphore()

        def run(self, cb, *args, **kwargs):
            self._locks[self.path].acquire()
            try:
                cb(*args, **kwargs)
            finally:
                self._locks[self.path].release()

    class Lock(object):
        _locks = {}

        def __init__(self, path, identifier):
            self._path = zk_scrub_path(path)
            if self._path not in self._locks:
                self._locks[self._path] = (
                    gevent.lock.Semaphore(),  # write lock
                    gevent.lock.Semaphore(),  # read lock
                    identifier,
                )

        def acquire(self, blocking=True, timeout=None):
            w_lock, _, _ = self._locks[self._path]
            return w_lock.acquire(blocking, timeout)

        def release(self):
            w_lock, _, _ = self._locks[self._path]
            w_lock.release()

        def contenders(self):
            w_lock, _, contender = self._locks[self._path]
            return [contender] if w_lock.locked() else []

        def destroy(self):
            self._locks.pop(self._path, None)

        def __enter__(self):
            self.acquire()

        def __exit__(self, exc_type, exc_value, traceback):
            self.release()
            self.destroy()

    class ReadLock(Lock):
        def acquire(self, blocking=True, timeout=None):
            w_lock, r_lock, _ = self._locks[self._path]
            if w_lock.acquire(blocking, timeout):
                w_lock.release()
                r_lock.acquire(False)
                return True
            return False

        def release(self):
            _, r_lock, _ = self._locks[self._path]
            r_lock.release()

    class WriteLock(Lock):
        def acquire(self, blocking=True, timeout=None):
            w_lock, r_lock, _ = self._locks[self._path]
            if r_lock.acquire(blocking, timeout):
                r_lock.release()
                # we should substract time already passed in the read acquire
                # to the timout before we tried acquire the write lock
                return w_lock.acquire(blocking, timeout)
            return False

        def release(self):
            w_lock, _, _ = self._locks[self._path]
            w_lock.release()

    def __init__(self, *args, **kwargs):
        self.add_listener = stub
        self.start = stub
        self.state = KazooState.CONNECTED
    # end __init__

    @classmethod
    def reset(cls):
        cls._values = {}
    # end reset

    def command(self, cmd):
        if cmd == 'stat':
            return 'Mode:standalone\nNode count:%s\n' %(len(self._values.keys()))
    # end command

    def stop(*args, **kwargs):
        pass
    # end stop

    def create(self, path, value='', *args, **kwargs):
        scrubbed_path = zk_scrub_path(path)
        if scrubbed_path in self._values:
            raise ResourceExistsError(
                path, str(self._values[scrubbed_path][0]), 'zookeeper')
        self._values[scrubbed_path] = (value, ZnodeStat(time.time()*1000))
    # end create

    def create_node(self, path, value='', *args, **kwargs):
        scrubbed_path = zk_scrub_path(path)
        if scrubbed_path in self._values:
            raise ResourceExistsError(
                path, str(self._values[scrubbed_path][0]), 'zookeeper')
        self._values[scrubbed_path] = (value, ZnodeStat(time.time()*1000))
    # end create

    def read_node(self, path):
        try:
            return self._values[zk_scrub_path(path)]
        except KeyError:
            raise kazoo.exceptions.NoNodeError()
    # end get

    def get(self, path):
        try:
            return self._values[zk_scrub_path(path)]
        except KeyError:
            raise kazoo.exceptions.NoNodeError()
    # end get

    def set(self, path, value):
        scrubbed_path = zk_scrub_path(path)
        if scrubbed_path not in self._values:
            raise kazoo.exceptions.NoNodeError()
        self._values[scrubbed_path] = (value, ZnodeStat(time.time()*1000))

    def get_children(self, path):
        if not path:
            return []

        children = set()
        scrubbed_path = zk_scrub_path(path)
        for node in self._values:
            if node.startswith(scrubbed_path):
                # return non-leading '/' in name
                child_node = node[len(scrubbed_path):]
                if not child_node:
                    continue
                if child_node[0] == '/':
                    child_node = child_node[1:]
                children.add(child_node.split('/')[0])
        return list(children)
    # end get_children

    def exists(self, path):
        scrubbed_path = zk_scrub_path(path)
        if scrubbed_path in self._values:
            return self._values[scrubbed_path]
        return None
    # end exists

    def delete_node(self, path, recursive=False):
        scrubbed_path = zk_scrub_path(path)
        if not recursive:
            try:
                del self._values[scrubbed_path]
            except KeyError:
                raise kazoo.exceptions.NoNodeError()
        else:
            for path_key in self._values.keys():
                if scrubbed_path in path_key:
                    del self._values[path_key]
    # end delete

    def delete(self, path, recursive=False):
        scrubbed_path = zk_scrub_path(path)
        if not recursive:
            try:
                del self._values[scrubbed_path]
            except KeyError:
                raise kazoo.exceptions.NoNodeError()
        else:
            for path_key in self._values.keys():
                if scrubbed_path in path_key:
                    del self._values[path_key]
    # end delete

    @contextlib.contextmanager
    def patch_path(self, path, new_values=None, recursive=True):
        # if recursive is False, new_values is value at path
        # if recursive is True, new_values is dict((path,path-val))

        scrubbed_path = zk_scrub_path(path)
        orig_nodes = {}
        paths_to_patch = []
        # collect path(s) to patch...
        for node in self._values.keys():
            if recursive: # simulate wipe of node with path and descendants
                if node.startswith(scrubbed_path):
                    paths_to_patch.append(node)
            else: # only one path
                if node == scrubbed_path:
                    paths_to_patch = [node]
                    break

        # ...and patch it
        for path in paths_to_patch:
            orig_nodes[path] = self._values[path]
            if recursive:
                if new_values and path in new_values:
                    self._values[path] = new_values[path]
                else:
                    del self._values[path]
            else: # only one path
                if new_values is None:
                    del self._values[path]
                else:
                    self._values[path] = new_values
                break

        try:
            yield
        finally:
            for node in orig_nodes:
                self._values[node] = orig_nodes[node]
    #end patch_path
# end class FakeKazooClient

def fake_zk_counter_init(self, client, path, default=0, *args, **kwargs):
        self.client = client
        self.path = path
        self.default = default
        self.default_type = type(default)
        self._ensured_path = False
        self._value = default

@property
def fake_zk_counter_value(self):
        return self._value

def fake_zk_counter_change(self, value):
        data = int(self._value + value)
        if data > self.max_count:
            raise OverQuota()
        else:
            self._value = data
        return self

def fake_zk_counter_ensure_node(self):
        self._ensured_path = True

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
        #if path in self._values:
            #raise ResourceExistsError(
            #    path, str(self._values[path][0], 'zookeeper'))
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
