#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import gevent
gevent.monkey.patch_all()

import datetime
import json
import uuid
import signal

import utils

from vnc_amqp import VncAmqpHandle

import etcd3
from etcd3.client import KVMetadata
from vnc_api import vnc_api
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from cfgm_common import vnc_greenlets
try:
    from gevent.lock import Semaphore
except ImportError:
    # older versions of gevent
    from gevent.coros import Semaphore


class VncEtcdClient(object):
    def __init__(self, host, port, credential, ssl_enabled, ca_certs):
        self._host = host
        self._port = port
        self._credential = credential
        self._ssl_enabled = ssl_enabled
        self._ca_certs = ca_certs
        self._conn_state = ConnectionStatus.INIT

        self._client = self._new_client()

    def _new_client(self):
        kwargs = {'host': self._host, 'port': self._port}

        if self._credential:
            kwargs['user'] = self._credential['username']
            kwargs['password'] = self._credential['password']

        if self._ssl_enabled:
            kwargs['ca_cert'] = self._credential['ca_cert']
            kwargs['cert_key'] = self._credential['ca_key']

        client = etcd3.client(timeout=5, **kwargs)
        ConnectionState.update(conn_type=ConnType.DATABASE, name='etcd',
                               status=ConnectionStatus.UP, message='',
                               server_addrs='{}:{}'.format(self._host,
                                                           self._port))
        self._conn_state = ConnectionStatus.UP
        return client


def gen_request_id():
    return "req-%s" % uuid.uuid4()


class VncEtcdWatchHandle(VncAmqpHandle):
    def __init__(self, sandesh, logger, db_cls, reaction_map, q_name_prefix,
                 notifier_cfg, trace_file=None, timer_obj=None):
        self._etcd_cfg = notifier_cfg
        super(VncEtcdWatchHandle, self).__init__(sandesh, logger, db_cls, reaction_map, q_name_prefix, rabbitmq_cfg=None, trace_file=trace_file, timer_obj=timer_obj)

    def establish(self):
        self._vnc_etcd_watcher = VncEtcdWatchClient(
                self._etcd_cfg['servers'], self._etcd_cfg['port'],
                self._etcd_cfg['user'], self._etcd_cfg['password'],
                self._etcd_cfg['vhost'], self._etcd_cfg['ha_mode'],
                self.etcd_path_prefix, self._etcd_callback_wrapper,
                self.logger.log)

    def _etcd_callback_wrapper(self, event):
        common_msg = self._parse_event(event)
        self._vnc_subscribe_callback(common_msg)

    def _parse_event(self, event):
        obj_dict = json.loads(event.value)
        msg = {k:obj_dict[k] for k in ("type", "uuid", "fq_name")}
        oper = self._get_oper(event)
        if oper == "CREATE":
            msg["obj_dict"] = obj_dict
        msg["oper"] = oper
        msg["request_id"] = gen_request_id()

        return msg

    @staticmethod
    def _get_oper(event):
        e = event._event
        oper_name = e.EventType.DESCRIPTOR.values_by_number[e.type].name
        if oper_name == "PUT":
            if KVMetadata(e.kv, None).version == 1:
                return "CREATE"
            return "UPDATE"
        return "DELETE"

    def close(self):
        self._vnc_etcd_watcher.shutdown()


class VncEtcdWatchClient(VncEtcdClient):
    def __init__(self, host, port, credential, ssl_enabled, ca_certs, prefix, subscribe_cb, logger, heartbeat_seconds=0):
        self.key_prefix = prefix
        self.subscribe_cb = subscribe_cb
        self._logger = logger

        self._conn_lock = Semaphore()
        self._heartbeat_seconds = heartbeat_seconds
        self._cancel = None

        super(VncEtcdWatchClient, self).__init__(host, port, credential, ssl_enabled, ca_certs)

        gevent.signal(signal.SIGTERM, self.sigterm_handler)
        self._start(self.key_prefix)

    def _update_sandesh_status(self, status, msg=''):
        ConnectionState.update(conn_type=ConnType.DATABASE,
                               name='etcd', status=status, message=msg,
                               server_addrs=["{}:{}".format(self.host, self._port)])

    def publish(self, message):
        raise NotImplementedError

    def sigterm_handler(self):
        self.shutdown()
        exit()

    def num_pending_messages(self):
        raise NotImplementedError
        # return self._publish_queue.qsize()

    # def prepare_to_consume(self):
    #     # override this method
    #     return

    def _reconnect(self):
        if self._conn_lock.locked():
            # either connection-monitor or publisher should have taken
            # the lock. The one who acquired the lock would re-establish
            # the connection and releases the lock, so the other one can
            # just wait on the lock, till it gets released
            self._conn_lock.wait()
            if self._conn_state == ConnectionStatus.UP:
                return

        with self._conn_lock:
            msg = "etcd connection DOWN"
            self._logger(msg, level=SandeshLevel.SYS_NOTICE)
            self._update_sandesh_status(ConnectionStatus.DOWN)
            self._conn_state = ConnectionStatus.DOWN

            self._client.close()
            if self._cancel is not None:
                self._cancel()
                self._cancel = None
                self._consumer = None

            self._client = self._new_client()

            self._update_sandesh_status(ConnectionStatus.UP)
            self._conn_state = ConnectionStatus.UP
            msg = 'etcd connection ESTABLISHED %s' % repr(self._client)
            self._logger(msg, level=SandeshLevel.SYS_NOTICE)

            self._consumer, self._cancel = self._client.watch_prefix(self.key_prefix)


    def _connection_watch(self, connected):
        if not connected:
            self._reconnect()

        while True:
            try:
                self.subscribe_cb(self._consumer.next())
                # self._conn.drain_events()
            except: # self._conn.connection_errors + self._conn.channel_errors as e:
                self._reconnect()

    def _connection_watch_forever(self):
        connected = True
        while True:
            try:
                self._connection_watch(connected)
            except Exception as e:
                msg = 'Error in etcd drainer greenlet: %s' % (str(e))
                self._logger(msg, level=SandeshLevel.SYS_ERR)
                # avoid 'reconnect()' here as that itself might cause exception
                connected = False

    # end _connection_watch_forever

    def _connection_heartbeat(self):
        while True:
            try:
                if self._conn.connected:
                    self._conn.heartbeat_check()
            except Exception as e:
                msg = 'Error in etcd heartbeat greenlet: %s' % (str(e))
                self._logger(msg, level=SandeshLevel.SYS_ERR)
            finally:
                gevent.sleep(float(self._heartbeat_seconds / 2))

    def _start(self, client_name):
        self._reconnect()

        self._connection_monitor_greenlet = vnc_greenlets.VncGreenlet(
            'etcd_ ' + client_name + '_ConnMon',
            self._connection_watch_forever)
        if self._heartbeat_seconds:
            self._connection_heartbeat_greenlet = vnc_greenlets.VncGreenlet(
                'etcd_ ' + client_name + '_ConnHeartBeat',
                self._connection_heartbeat)
        else:
            self._connection_heartbeat_greenlet = None

    def greenlets(self):
        ret = [self._connection_monitor_greenlet]
        if self._connection_heartbeat_greenlet:
            ret.append(self._connection_heartbeat_greenlet)
        return ret

    def shutdown(self):
        self._connection_monitor_greenlet.kill()
        if self._connection_heartbeat_greenlet:
            self._connection_heartbeat_greenlet.kill()
        if self._consumer:
            self._cancel()
        self._client.close()

    def reset(self):
        # self._publish_queue = Queue()
        raise NotImplementedError

    # _SSL_PROTOCOLS = {
    #     "tlsv1": ssl.PROTOCOL_TLSv1,
    #     "sslv23": ssl.PROTOCOL_SSLv23
    # }
    #
    # @classmethod
    # def validate_ssl_version(cls, version):
    #     version = version.lower()
    #     try:
    #         return cls._SSL_PROTOCOLS[version]
    #     except KeyError:
    #         raise RuntimeError('Invalid SSL version: {}'.format(version))
    #
    # def _fetch_ssl_params(self, **kwargs):
    #     if strtobool(str(kwargs.get('rabbit_use_ssl', False))):
    #         ssl_params = dict()
    #         ssl_version = kwargs.get('kombu_ssl_version', '')
    #         keyfile = kwargs.get('kombu_ssl_keyfile', '')
    #         certfile = kwargs.get('kombu_ssl_certfile', '')
    #         ca_certs = kwargs.get('kombu_ssl_ca_certs', '')
    #         if ssl_version:
    #             ssl_params.update({'ssl_version':
    #                                    self.validate_ssl_version(ssl_version)})
    #         if keyfile:
    #             ssl_params.update({'keyfile': keyfile})
    #         if certfile:
    #             ssl_params.update({'certfile': certfile})
    #         if ca_certs:
    #             ssl_params.update({'ca_certs': ca_certs})
    #             ssl_params.update({'cert_reqs': ssl.CERT_REQUIRED})
    #         return ssl_params or True
    #     return False

class VncEtcd(VncEtcdClient):
    def __init__(self, host, port, prefix, logger=None,
                 obj_cache_exclude_types=None, log_response_time=None,
                 credential=None, ssl_enabled=False, ca_certs=None):
        super(VncEtcd, self).__init__(host, port, credential,
                                      ssl_enabled, ca_certs)
        self._prefix = prefix
        self._logger = logger

        self._obj_cache = EtcdCache(skip_keys=obj_cache_exclude_types)
        self._cache = EtcdCache(ttl=3600)  # 1 hour TTL
        self.log_response_time = log_response_time

    def __getattr__(self, name):
        return getattr(self._client, name)

    def _key_prefix(self, obj_type):
        """Resources prefix for etcd.

        :param (str) obj_type: Type of resource
        :return: (str) full prefix ie: "/contrail/virtual_network"
        """
        return "{prefix}/{type}".format(prefix=self._prefix, type=obj_type)

    def _key_obj(self, obj_type, obj_id):
        """Resource key with resource prefix.

        :param (str) obj_type: Type of resource
        :param (str) obj_id: uuid of object
        :return: (str) full key ie: "/contrail/virtual_network/aaa-bbb-ccc"
        """
        key_prefix = self._key_prefix(obj_type)
        return "{prefix}/{id}".format(prefix=key_prefix, id=obj_id)

    # TODO: handle_exception decorator

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
                resource, kv_meta = self._client.get(key)
                if resource is None:
                    # There is no data in etcd, go to next uuid
                    continue

                resource = json.loads(resource)
                record = EtcdCache.Record(resource=resource,
                                          kv_meta=kv_meta)
                self._obj_cache[key] = record

            if field_names is None:
                results.append(resource)
            else:
                results.append({k: v for k, v in resource.items()
                                if k in field_names})

        if not results:
            raise NoIdError(obj_uuids[0])
        return True, results

    def object_all(self, obj_type):
        """Get all objects for given type.

        :param (str) obj_type: Type of resource object
        :return: (gen) Generator with dict resources
        """
        key = self._key_prefix(obj_type)
        response = self._client.get_prefix(key)
        return (json.loads(resp[0]) for resp in response)

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
        # TODO: implement IMPORTANT (needs cache)
        raise NotImplementedError("vnc_etcd method not implemented")

    def uuid_to_fq_name(self, uuid):
        # TODO: implement IMPORTANT (needs cache)
        raise NotImplementedError("vnc_etcd method not implemented")

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


class EtcdCache(object):
    """etcd cache container with TTL records.

    :param ttl (int): Time To Live of records expressed in seconds.
    :param skip_keys: List of object types which should not be cached.
    :example:

        # create cache instance
        cache = EtcdCache(ttl=500, skip_keys=['tag_type'])

        # write record to cache
        key = '/contrail/virtual_network/5ee45236-c435-4006-b4df-ba3442c8a3ec'
        resource, kv_meta = etcd_client.get(key)
        record = EtcdCache.Record(resource=resource, kv_meta=kv_meta)

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
        :param (etcd3.KVMetadata) kv_meta: Meta data from etcd3 client
        """

        def __init__(self, ttl=None, resource=None, kv_meta=None):
            self.ttl = ttl
            self.resource = resource
            self.kv_meta = kv_meta

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
