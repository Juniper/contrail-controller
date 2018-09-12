#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import json
import datetime

import etcd3

try:
    from exceptions import NoIdError
except ImportError:
    class NoIdError(Exception):
        pass
from vnc_api import vnc_api
import utils


class VncEtcdClient(object):
    def __init__(self, host, port, prefix, logger, obj_cache_exclude_types,
                 credential, ssl_enabled, ca_certs):
        self._host = host
        self._port = port
        self._prefix = prefix
        self._logger = logger
        self._credential = credential
        self._ssl_enabled = ssl_enabled
        self._ca_certs = ca_certs

        self._client = self._new_client()
        self._obj_cache = EtcdCache(skip_keys=obj_cache_exclude_types)
        self._cache = EtcdCache(ttl=3600)  # 1 hour TTL

    def __getattr__(self, name):
        return getattr(self._client, name)

    def _new_client(self):
        kwargs = {'host': self._host, 'port': self._port}

        if self._credential:
            kwargs['user'] = self._credential['username']
            kwargs['password'] = self._credential['password']

        if self._ssl_enabled:
            kwargs['ca_cert'] = self._credential['ca_cert']
            kwargs['cert_key'] = self._credential['ca_key']

        return etcd3.client(**kwargs)

    def _key(self, obj_type, obj_id):
        return "{key_prefix}/{obj_id}".format(
            key_prefix=self._key_prefix(obj_type), obj_id=obj_id)

    def _key_prefix(self, obj_type):
        return "{prefix}/{obj_type}".format(prefix=self._prefix,
                                            obj_type=obj_type)

    # TODO: handle_exception decorator

    def object_read(self, obj_type, obj_uuids, field_names=None,
                    ret_readonly=False):
        """Read objects from etcd.

        :param obj_type: Resource type.
        :param obj_uuids: List of UUID to fetch.
        :param field_names: List of fields to return.
        :param ret_readonly: If True read resource from cache when possible.
        :return: List of resources.
        """
        results = []
        if not obj_uuids:
            return True, results

        for uuid in obj_uuids:
            key = self._key(obj_type, uuid)
            if ret_readonly is True and key in self._obj_cache:
                record = self._obj_cache[key]
                resource = record.resource
            else:
                with self._client.lock(key):
                    resource, kv_meta = self._client.get(key)

                if resource is None:
                    # There is no data in etcd, go to next uuid
                    continue

                resource = json.loads(resource)
                record = EtcdCache.Record(resource=resource, kv_meta=kv_meta)
                self._obj_cache[key] = record

            if field_names is None:
                results.append(resource)
            else:
                results.append({k: v for k, v in resource.items()
                                if k in field_names})
        if not results:
            raise NoIdError(obj_uuids[0])
        return True, results

    def object_list(self, obj_type, parent_uuids=None, back_ref_uuids=None,
                    obj_uuids=None, count=False, filters=None,
                    paginate_start=None, paginate_count=None):

        keys = {'children': "{}s".format(obj_type),
                'backrefs': "{}_backrefs".format(obj_type)}

        if hasattr(self, '_db_client_mgr'):
            obj_class = self._db_client_mgr.get_resource_class(obj_type)
        else:
            obj_class = getattr(vnc_api, utils.CamelCase(obj_type))

        filters = [f for f in filters if f in obj_class.prop_fields]
        ret_marker = None
        anchored_op = True
        results = []

        if parent_uuids:
            # get objects by uuids and get all its children of obj_type
            ok, parents = self.object_read(obj_type, parent_uuids,
                                           ret_readonly=True)
            results.extend(_filter_childes(keys['children'], parents,
                                           obj_uuids, filters=filters))
        if back_ref_uuids:
            # get objects by back_ref_uuids and get all its children of obj_type
            ok, backrefs = self.object_read(obj_type, back_ref_uuids,
                                            ret_readonly=True)
            results.extend(_filter_childes(keys['backrefs'], backrefs,
                                           obj_uuids, filters=filters))

        if not parent_uuids and not back_ref_uuids:
            anchored_op = False
            if obj_uuids:
                # take obj_uuids and apply filters
                for key in keys.values():
                    results.extend(_filter_childes(key, obj_uuids,
                                                   filters=filters))
            else:
                # grab all resources of obj_type
                resource, _ = self._client.get(self._prefix(obj_type))
                results = [r['uuid'] for r in resource]

        if count:
            return True, len(results), None

        # for anchored list with pagination,
        # prune from union of anchors and last uuid is marker
        if paginate_start and anchored_op:
            results, ret_marker = _paginate(results, paginate_count)

        return True, results, ret_marker

    def fq_name_to_uuid(self, obj_type, fq_name):
        # TODO: implement IMPORTANT (needs cache)
        raise NotImplementedError("vnc_etcd method not implemented")

    def uuid_to_fq_name(self, uuid):
        # TODO: implement IMPORTANT (needs cache)
        raise NotImplementedError("vnc_etcd method not implemented")


def _filter_childes(key, objs, uuids=None, filters=None):
    results = []
    for obj in objs:
        if key not in obj:
            continue

        if not filters:
            if uuids:
                results.extend(i['uuid'] for i in obj[key] if i['uuid'] in uuids)
            else:
                results.extend(i['uuid'] for i in obj[key])
        else:
            for item in obj[key]:
                if uuids and item['uuid'] not in uuids:
                    continue

                append = True
                for f_key, f_val in filters:
                    if f_key not in item:
                        append = False
                        break

                    if f_key in item and isinstance(item[f_key], dict):
                        for f in f_val:
                            try:
                                f_dict = json.loads(f)
                            except ValueError:
                                continue
                            if f_dict.viewitems() <= item[f_key].viewitems():
                                break
                        else:
                            append = True
                if append:
                    results.append(item['uuid'])
    return results


def _paginate(result, paginate_count):
    result = sorted(result, key=lambda fqn_uuid: fqn_uuid[1])
    if len(result) > paginate_count:
        result = result[:paginate_count]
    ret_marker = None if not result else result[-1][1]
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
        def __init__(self, ttl=None, resource=None, kv_meta=None):
            self.ttl = ttl
            self.resource = resource
            self.kv_meta = kv_meta

        def set_ttl(self, sec):
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
