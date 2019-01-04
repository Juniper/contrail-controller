# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
Schema transformer DB for etcd to allocate ids and store internal data
"""

from cfgm_common import vnc_object_db, vnc_etcd

class RTHandler(object):
    def get_range(self):
        #TODO: Implement for etcd
        return []

class SchemaTransformerEtcd(vnc_object_db.VncObjectEtcdClient):

    _ETCD_SERVICE_CHAIN_PATH = "schema_transformer/service_chain"

    def __init__(self, args, logger):
        self._etcd_args = vnc_etcd.etcd_args(args)
        self._logger = logger
        self._rt_cf = RTHandler()
        self._logger.log("VncObjectEtcdClient arguments: {}".format(self._etcd_args))
        super(SchemaTransformerEtcd, self).__init__(logger=self._logger.log, **self._etcd_args)

    def _patch_encapsulate_value(self, obj):
        """Encaplsulate each obj in dictionary with single element having
        'value' key and `obj` value.
        This is required for backward compatibility.

        :param (obj) obj: Vanilla object from etcd
        :return: (dict) Obj with patched 'value' key
        """
        return {'value': obj}

    def _path_key(self, key):
        """Combine and return self._prefix with given key.

        :param (str) key: key of an
        :return: (str) full key ie: "/contrail/virtual_network/aaa-bbb-ccc"
        """
        return "%s/%s" % (self._ETCD_SERVICE_CHAIN_PATH, key)

    def allocate_service_chain_vlan(self, service_vm, service_chain):
        # Since vlan tag 0 is not valid, return 1
        return 1

    def free_service_chain_vlan(self, service_vm, service_chain):
        # TODO
        pass

    def get_route_target(self, ri_fq_name):
        # TODO
        return 0

    def alloc_route_target(self, ri_fq_name, zk_only=False):
        # TODO
        return 0

    def free_route_target(self, ri_fq_name):
        # TODO
        pass

    def get_service_chain_ip(self, sc_name):
        # TODO
        return 0

    def add_service_chain_ip(self, sc_name, ip_dict):
        # TODO
        pass

    def remove_service_chain_ip(self, sc_name):
        # TODO
        pass

    def list_service_chain_uuid(self):
        """List all objects with 'service_chain' object type.

        :return: (gen) Generator of fetched objects
        """
        response = self._object_db.list_kv(self._ETCD_SERVICE_CHAIN_PATH)
        return (self._patch_encapsulate_value(value) for key, value in response)

    def add_service_chain_uuid(self, key, value):
        """Store a value with a key created from combining prefix,
        _ETCD_SERVICE_CHAIN_PATH and key in etcd.

        :param key (str): identifier (UUID)
        :param value (Any): data to store, typically a string (object
                            serialized with jsonpickle.encode)
        """
        key_path = self._path_key(key)
        self._object_db.put_kv(key_path, value)

    def remove_service_chain_uuid(self, key):
        """Remove a etcd entry with key created from combining prefix,
        _ETCD_SERVICE_CHAIN_PATH and key.

        :param key (str): identifier (UUID)
        """
        key_path = self._path_key(key)
        self._object_db.delete_kv(key_path)

    def get_bgpaas_port(self, port):
        # TODO (but maybe this method is not used at all, so there might not be any reason implementing it)
        pass

    def alloc_bgpaas_port(self, name):
        # TODO
        return 0

    def free_bgpaas_port(self, port):
        # TODO
        pass
