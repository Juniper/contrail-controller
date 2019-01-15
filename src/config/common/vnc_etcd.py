#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import datetime
import json
import uuid
from functools import wraps

import etcd3
from etcd3.client import KVMetadata
import time
import utils
from cfgm_common.exceptions import DatabaseUnavailableError, NoIdError, VncError
from etcd3.exceptions import ConnectionFailedError
from six.moves import queue
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

from cfgm_common import vnc_greenlets
from cfgm_common.vnc_amqp import VncAmqpHandle

from vnc_api import vnc_api

def etcd_args(args):
    vnc_db = {
        'host': args.etcd_server,
        'port': args.etcd_port,
        'prefix': args.etcd_prefix,
    }
    if args.etcd_user and args.etcd_password:
        credentials = {'user': args.etcd_user,
                       'password': args.etcd_password}

        if args.ssl_enabled:
            credentials['ca_cert'] = args.etcd_ssl_ca_cert
            credentials['cert_key'] = args.etcd_ssl_keyfile
            credentials['cert_cert'] = args.etcd_ssl_certfile

        vnc_db['credentials'] = credentials

    return vnc_db

def _handle_conn_error(func):
    @wraps(func)
    def wrapper(self, *args, **kwargs):
        start_time = datetime.datetime.now()
        try:
            return func(self, *args, **kwargs)
        except ConnectionFailedError as exc:
            if self._logger:
                msg = 'etcd connection down. Exception in {}'.format(str(func))
                self._logger(msg, level=SandeshLevel.SYS_ERR)
            raise DatabaseUnavailableError(
                'Error, %s: %s' % (str(exc), utils.detailed_traceback()))
        finally:
            if self.log_response_time:
                end_time = datetime.datetime.now()
                self.log_response_time(end_time - start_time)
    return wrapper


def _gen_request_id():
    return "req-%s" % uuid.uuid4()


class VncEtcdClient(object):
    """Base etcd client."""
    def __init__(self, host, port, credentials):
        self._host = host
        self._port = port
        self._credentials = credentials
        self._conn_state = ConnectionStatus.INIT

        self._client = self._new_client()

    def _new_client(self):
        kwargs = {'host': self._host, 'port': self._port}

        if self._credentials:
            kwargs.update(self._credentials)

        client = etcd3.client(timeout=5, **kwargs)
        self._update_sandesh_status(ConnectionStatus.UP)

        return client

    def _update_sandesh_status(self, status, msg=''):
        self._conn_state = status
        ConnectionState.update(conn_type=ConnType.DATABASE,
                               name='etcd', status=status, message=msg,
                               server_addrs=["{}:{}".format(self._host, self._port)])


class VncEtcdWatchHandle(VncAmqpHandle):
    """Apply callback on etcd event.

    Wraps VncAmqpHandle overwriting client creation and event callback.
    etcd events are translated to format which VncAmqpHandle expects
    i.e. rabbit msg format.
    """
    def __init__(self, sandesh, logger, db_cls, reaction_map,
                 notifier_cfg, host_ip, trace_file=None, timer_obj=None):
        self._etcd_cfg = notifier_cfg
        super(VncEtcdWatchHandle, self).__init__(
            sandesh, logger, db_cls, reaction_map, q_name_prefix=None,
            rabbitmq_cfg=None, host_ip=host_ip, trace_file=trace_file, timer_obj=timer_obj)

    def establish(self):
        self._vnc_etcd_watcher = VncEtcdWatchClient(
                self._etcd_cfg['host'], self._etcd_cfg['port'],
                self._etcd_cfg.get('credentials'), self._etcd_cfg['prefix'],
                self._etcd_callback_wrapper, self.logger.log)

    def _etcd_callback_wrapper(self, event):
        self.logger.debug("Got etcd event: {} {}".format(event.key, event.value))
        common_msg = self._parse_event(event)
        self._vnc_subscribe_callback(common_msg)

    def _parse_event(self, event):
        obj_type, uuid_ = event.key.split("/")[-2:]
        obj_dict = json.loads(event.value)
        oper = self._get_oper(event)
        msg = {"type": obj_type, "uuid": uuid_, "oper": oper}
        if oper == "CREATE":
            msg["obj_dict"] = obj_dict
        msg["request_id"] = _gen_request_id()
        msg["fq_name"] = obj_dict["fq_name"]

        return msg

    @staticmethod
    def _get_oper(event):
        if isinstance(event, etcd3.events.PutEvent):
            if KVMetadata(event._event.kv, None).version == 1:
                return "CREATE"
            return "UPDATE"
        return "DELETE"

    def close(self):
        self._vnc_etcd_watcher.shutdown()

    def greenlets(self):
        return self._vnc_etcd_watcher.greenlets()


class VncEtcdWatchClient(VncEtcdClient):
    """Handle etcd watch greenlet.

    Similar to VncKombuClient but for etcd.
    Does not support etcd writes by design.
    """
    def __init__(self, host, port, credentials, prefix, subscribe_cb, logger):
        self._prefix = prefix
        self.subscribe_cb = subscribe_cb
        self._logger = logger
        self._cancel = None
        self._consumer = None

        super(VncEtcdWatchClient, self).__init__(host, port, credentials)
        self._start(self._prefix)

    def _reconnect(self):
        msg = "etcd connection DOWN"
        self._logger(msg, level=SandeshLevel.SYS_NOTICE)
        self._update_sandesh_status(ConnectionStatus.DOWN)

        if self._cancel is not None:
            self._cancel()
            self._cancel = None
            self._consumer = None

        self._client = self._new_client()

        msg = 'etcd connection ESTABLISHED %s' % repr(self._client)
        self._logger(msg, level=SandeshLevel.SYS_NOTICE)

        self._consumer, self._cancel = self._client.watch_prefix(self._prefix)

    def _connection_watch(self, connected):
        if not connected:
            self._reconnect()

        while True:
            try:
                self.subscribe_cb(next(self._consumer))
            except Exception as e:
                msg = "Error in etcd watch greenlet: {}".format(e)
                self._logger(msg, level=SandeshLevel.SYS_ERR)
                self._reconnect()

    def _connection_watch_forever(self):
        connected = True
        while True:
            try:
                self._connection_watch(connected)
            except Exception as e:
                msg = "Error in etcd reconnect: {}".format(e)
                self._logger(msg, level=SandeshLevel.SYS_ERR)
                connected = False

    def _start(self, client_name):
        self._reconnect()

        self._connection_monitor_greenlet = vnc_greenlets.VncGreenlet(
            'etcd_ ' + client_name + '_ConnMon',
            self._connection_watch_forever)

    def greenlets(self):
        ret = [self._connection_monitor_greenlet]
        return ret

    def shutdown(self):
        self._connection_monitor_greenlet.kill()
        if self._consumer:
            self._cancel()


class VncEtcd(VncEtcdClient):
    """Database interface for etcd client."""
    def __init__(self, host, port, prefix, logger=None,
                 obj_cache_exclude_types=None, log_response_time=None,
                 timeout=5, credentials=None):
        super(VncEtcd, self).__init__(host, port, credentials)
        self._prefix = prefix
        self._logger = logger
        self.log_response_time = log_response_time
        self.timeout = timeout

        # cache for object_read, object_list and object_all
        self._obj_cache = EtcdCache(skip_keys=obj_cache_exclude_types)
        # general purposes cache
        self._cache = EtcdCache(ttl=3600)  # 1 hour TTL

    def __getattr__(self, name):
        return getattr(self._client, name)

    def _key_prefix(self, obj_type):
        """Resources prefix for etcd.

        :param (str) obj_type: Type of resource
        :return: (str) full prefix ie: "/contrail/virtual_network"
        """
        return "{prefix}/{type}/".format(prefix=self._prefix, type=obj_type)

    def _key_obj(self, obj_type, obj_id):
        """Resource key with resource prefix.

        :param (str) obj_type: Type of resource
        :param (str) obj_id: uuid of object
        :return: (str) full key ie: "/contrail/virtual_network/aaa-bbb-ccc"
        """
        key_prefix = self._key_prefix(obj_type)
        return "{prefix}{id}".format(prefix=key_prefix, id=obj_id)

    def _key_path(self, key):
        """Combine and return self._prefix with given key.

        :param (str) key: key of an
        :return: (str) full key ie: "/contrail/virtual_network/aaa-bbb-ccc"
        """
        return "{prefix}/{key}".format(prefix=self._prefix, key=key)

    @_handle_conn_error
    def object_read(self, obj_type, obj_uuids, field_names=None,
                    ret_readonly=False):
        """Read objects from etcd.

        :param (str) obj_type: Resource type.
        :param (List[str]) obj_uuids: List of UUID to fetch.
        :param (List[str]) field_names: List of fields to return.
        :param (boolean) ret_readonly: If True read resource
                                       from cache when possible.
        :return: (List[Dict]) List of resources.
        """
        results = []
        if not obj_uuids:
            return True, results

        for uuid in obj_uuids:
            key = self._key_obj(obj_type, uuid)
            if ret_readonly is True and key in self._obj_cache:
                record = self._obj_cache[key]
                resource = record.resource
            else:
                resource = self._read_object(key, self.timeout)
                if resource is None:
                    # There is no data in etcd, go to next uuid
                    continue

                resource = self._patch_refs_to(json.loads(resource))
                record = EtcdCache.Record(resource=resource)
                self._obj_cache[key] = record

            if field_names is None:
                results.append(resource)
            else:
                results.append({k: v for k, v in resource.items()
                                if k in field_names})

        if not results:
            raise NoIdError(obj_uuids[0])
        return True, results

    @_handle_conn_error
    def object_all(self, obj_type):
        """Get all objects for given type.

        :param (str) obj_type: Type of resource object
        :return: (gen) Generator with dict resources
        """
        key = self._key_prefix(obj_type)
        response = self._client.get_prefix(key)
        if response:
            return (self._patch_refs_to(json.loads(resp[0])) for resp in response)
        else:
            return []

    @_handle_conn_error
    def object_list(self, obj_type, parent_uuids=None, back_ref_uuids=None,
                    obj_uuids=None, count=False, filters=None,
                    paginate_start=None, paginate_count=None):
        """Get list of objects uuid.

        For given parent_uuids find all uuids of children with type 'obj_type'
        and for given back_ref_uuids find all backrefs type of obj_type.

        :param (str) obj_type: Type of resource object
        :param (List[str]) parent_uuids: List of parents uuid
        :param (List[str]) back_ref_uuids: List of backrefs uuid
        :param (List[str]) obj_uuids: If exists is used as filter.
                           Only obj which are in this list will be returned.
        :param (boolean) count: If true, return length of resources
        :param (dict) filters: Filters to apply on results
        :param (str) paginate_start: Last uuid from previous fetch
        :param (int) paginate_count: The maximum number of uuids to fetch
        :return: (Tuple[boolean, List[str], str]) Tuple with success state,
                 list of uuids and last uuid in current fetch
        """
        # prepare filters
        if hasattr(self, '_db_client_mgr'):
            obj_class = self._db_client_mgr.get_resource_class(obj_type)
        else:
            obj_class = getattr(vnc_api, utils.CamelCase(obj_type))
        if filters is not None:
            filters = [f for f in filters if f in obj_class.prop_fields]
        # end prepare filters

        # map obj_type to field in etcd record
        backref_field = {'children': "{}s".format(obj_type),
                         'backrefs': "{}_backrefs".format(obj_type)}

        results = []  # list of dicts from etcd
        for field, uuids in (('children', parent_uuids),
                             ('backrefs', back_ref_uuids)):
            if uuids:
                _, parents = self.object_read(obj_type, uuids,
                                              ret_readonly=True)
                backrefs = self._get_backrefs(parents, obj_type,
                                              backref_field[field])
                filtered = self._filter(backrefs, obj_uuids, filters=filters)
                results.extend(filtered)

        anchored_op = True
        if not parent_uuids and not back_ref_uuids:
            anchored_op = False
            if obj_uuids:
                # take obj_uuids and apply filters
                _, parents = self.object_read(obj_type, obj_uuids,
                                              ret_readonly=True)
                for field in backref_field.keys():
                    backrefs = self._get_backrefs(parents, obj_type,
                                                  backref_field[field])
                    filtered = self._filter(backrefs, filters=filters)
                    results.extend(filtered)
            else:
                # grab all resources of obj_type
                results.extend(self.object_all(obj_type))

        if count:
            return True, len(results), None

        ret_marker = None  # last item for pagination
        if paginate_start and anchored_op:
            results, ret_marker = self._paginate(results, paginate_start,
                                                 paginate_count)

        return True, [(True, r['uuid']) for r in results], ret_marker

    def fq_name_to_uuid(self, obj_type, fq_name):
        """Get uuid from fully qualified name.

        :param (str) obj_type: Type of resource
        :param (List[str]) fq_name:
        :return: (str) uuid
        """
        fq_name_str = utils.encode_string(':'.join(fq_name))
        prefix = self._key_prefix(obj_type)

        key = "{}{}".format(prefix, fq_name_str)
        if key in self._cache:
            record = self._cache[key]
        else:
            response = self._client.get_prefix(prefix)
            record = None
            for data, _ in response:
                obj = json.loads(data)
                obj_fq_name = utils.encode_string(':'.join(obj['fq_name']))
                if obj_fq_name == fq_name_str:
                    if record is not None:
                        msg = 'Multi match {} for {}'
                        raise VncError(msg.format(fq_name_str, obj_type))
                    record = EtcdCache.Record(resource=obj)
            if record is None:
                raise NoIdError('{} {}'.format(obj_type, fq_name_str))
            self._cache[key] = record

        return record.resource['uuid']

    def uuid_to_fq_name(self, uuid):
        """Get fully qualified name of object with given uuid.

        :param (str) uuid:
        :return: (List[str]) fq_name
        """
        if uuid in self._cache:
            record = self._cache[uuid]
        else:
            response = self._client.get_prefix(self._prefix)
            for data, _ in response:
                obj = json.loads(data)
                if obj['uuid'] == uuid:
                    record = EtcdCache.Record(resource=obj)
                    break
            else:
                raise NoIdError(uuid)
            self._cache[uuid] = record

        return record.resource['fq_name']

    def cache_uuid_to_fq_name_add(self, id, fq_name, obj_type):
        """vnc_etcd handle Cache in different way than vnc_cassandra.
        This method is just for compatibility.
        """
        pass

    def _read_object(self, key, timeout=0):
        """Try to read object from etcd.
        If it is not present there, wait for it for some time.
        """
        if timeout is 0:
            resource, _ = self._client.get(key)
            return resource

        event_queue = queue.Queue()
        def callback(event):
            event_queue.put(event)
        watch_id = self._client.add_watch_callback(key, callback, None)
        resource = None
        try:
            resource, _ = self._client.get(key)
            if resource is None:
                ev = event_queue.get(timeout)
                resource = ev.value
        except queue.Empty:
            pass
        finally:
            self._client.cancel_watch(watch_id)

        return resource

    def _patch_refs_to(self, obj):
        """Add missing key "to" for every ref in object.
        This is required for backward compatibility.

        :param (dict) obj: Vanilla object from etcd
        :return: (dict) Obj with patched 'to' key
        """
        for key in obj.keys():
            if key.endswith('_refs'):
                for ref in obj[key]:
                    if 'to' not in ref:
                        ref['to'] = self.uuid_to_fq_name(ref['uuid'])
        return obj

    def _get_backrefs(self, parents, obj_type, field):
        """Fetch all backrefs from given field.

        :param (List[Dict]) parents: List of objects
        :param (str) obj_type: Type of resource
        :param (str) field: Field with child resources
        :return: (gen) Generator of dicts with fetched objects
        """
        for parent in parents:
            if field in parent:
                for element in parent[field]:
                    _, backref = self.object_read(obj_type, [element['uuid']],
                                                  ret_readonly=True)
                    yield backref

    def _filter(self, objs, obj_uuids=None, filters=None):
        """Filter objects by applying filters.

        Object will pass the test if fully match the filter.

        :param (List[Dict]) objs: Objects to filter
        :param (List[str]) obj_uuids: If exists is used as filter.
                           Only obj which are in this list will be returned.
        :param (Dict) filters: Dictionary with lists of filters
                               under each field.
        :return: (List[Dict]) List of objects which passed filters
        """
        for obj in objs:
            if obj_uuids and obj['uuid'] not in obj_uuids:
                continue

            if not filters:
                yield obj
            else:
                yield_obj = True
                for k, v in filters.items():
                    # search for the first mismatch
                    if k not in obj:
                        yield_obj = False
                        break
                    elif k in obj and isinstance(obj[k], dict):
                        for f in v:
                            try:
                                f_dict = json.loads(f)
                            except ValueError:
                                continue
                            if f_dict.viewitems() ^ obj[k].viewitems():
                                yield_obj = False
                                break

                    if yield_obj is False:
                        break

                if yield_obj:
                    yield obj

    def _paginate(self, objs, paginate_start, paginate_count):
        """Paginate object list.

        :param (List[Dict]) objs: List of objects to paginate
        :param (str) paginate_start: Last uuid from previous page
        :param (int) paginate_count: How much to show on page
        :return: (Tuple[List[Dict], str]) Current page of objects and last uuid
        """
        result = list(sorted(objs, key=lambda x: x['id_perms']['created']))
        try:
            # result holds list of objects, but we need to find
            # index of first item by uuid
            idx_start = [x['uuid'] for x in result].index(paginate_start)
        except ValueError:
            idx_start = 0
        result = result[idx_start:]

        if len(objs) > paginate_count:
            result = result[:paginate_count]
        ret_marker = None if not result else result[-1]['uuid']
        return result, ret_marker

    @_handle_conn_error
    def list_kv(self, path):
        """List all objects with prefix consisting of self._prefix
        (usually '/contrail') and `path` (a parameter).

        :param (str) path: etcd path/key to be listed
        :return: (gen) generator with dict resources
        """
        key = self._key_prefix(path)
        return self._client.get_prefix(key)

    @_handle_conn_error
    def put_kv(self, key, value):
        """Save a value under given key in etcd.

        :param key (str): key (there might be slashes, so key could look like
                          some kind of path)
        :param value (Any): data to store, typically a string (object
                            serialized with jsonpickle.encode)
        """
        prefixed_key = self._key_path(key)
        self._client.put(prefixed_key, value)

    @_handle_conn_error
    def delete_kv(self, key):
        """Delete a single key in etcd.

        :param key (str): key (there might be slashes, so key could look like
                          some kind of path)
        """
        prefixed_key = self._key_path(key)
        self._client.delete(prefixed_key)

    @_handle_conn_error
    def delete_path(self, path):
        """Delete a range of keys with a prefix in etcd.

        :param path (str): prefix of entries to be removed
        """
        prefixed_path = self._key_path(path)
        self._client.delete_prefix(prefixed_path)


class EtcdCache(object):
    """etcd cache container with TTL records.

    :param ttl (int): Time To Live of records expressed in seconds.
    :param skip_keys: List of object types which should not be cached.
    :example:

        # create cache instance
        cache = EtcdCache(ttl=500, skip_keys=['tag_type'])

        # write record to cache
        key = '/contrail/virtual_network/5ee45236-c435-4006-b4df-ba3442c8a3ec'
        resource, _ = etcd_client.get(key)
        record = EtcdCache.Record(resource=resource)

        cache[key] = record

        # read record from cache
        if key in cache:
            record = cache[key]
            print(record.resource, record.ttl)
    """
    DEFAULT_TTL = 300  # seconds

    class Record(object):
        """Record interface to use in cache.

        :param (int) ttl: Time to live in seconds
        :param (Any) resource: data to store, typically a dictionary
        """

        def __init__(self, ttl=None, resource=None):
            self.ttl = ttl
            self.resource = resource

        def set_ttl(self, sec):
            """Set Time To Live in record.

            Will use TTL from cache container. Can be used to variate
            TTL for every record individually.

            :param (int) sec: How long in seconds this record stays valid
            """
            self.ttl = datetime.datetime.now() + datetime.timedelta(seconds=sec)

    def __init__(self, ttl=None, skip_keys=None):
        self._data = {}
        self._skip_keys = skip_keys if skip_keys is not None else []
        if ttl and isinstance(ttl, int):
            self._ttl = ttl
        else:
            self._ttl = self.DEFAULT_TTL

    def __contains__(self, key):
        """Attempt check if key exists in cache.

        Will try to revoke key if ttl expired, and then return True
        if key is valid and still exist in cache, otherwise will return False.
        """
        self.revoke_ttl(key)
        return key in self._data

    def __getitem__(self, key):
        """Read record from cache.

        Throws KeyError if key doesn't exist.
        """
        self.revoke_ttl(key)
        return self._data[key]

    def __setitem__(self, key, record):
        """Write new record or replace existing one.

        Write method expects record to be instance of Record.
        If record type is in skip_keys will not be write to cache.
        """
        if not isinstance(record, self.Record):
            msg = "{} expect record to be instance of {}".format(
                self.__class__.__name__, self.Record.__name__)
            raise TypeError(msg)

        for skip in self._skip_keys:
            if skip in key:
                return

        record.set_ttl(self._ttl)
        self._data[key] = record

    def __delitem__(self, key):
        """Revoke key if exists in cache."""
        if key in self._data:
            del self._data[key]

    def revoke_ttl(self, key):
        """Revoke key if TTL expired."""
        if key in self._data:
            now = datetime.datetime.now()
            record = self._data[key]
            if record.ttl < now:
                del self[key]
