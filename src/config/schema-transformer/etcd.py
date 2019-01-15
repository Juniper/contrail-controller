# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
Schema transformer DB for etcd to allocate ids and store internal data
"""

import re

from cfgm_common import vnc_object_db, vnc_etcd
from vnc_api.exceptions import RefsExistError


class RTHandler(object):
    def get_range(self):
        # TODO: Implement for etcd
        return []


class SchemaTransformerEtcd(vnc_object_db.VncObjectEtcdClient):

    _ETCD_SCHEMA_TRANSFORMER_PREFIX = "schema_transformer"
    _BGPAAS_PORT_ALLOC_POOL = "bgpaas_port"
    _ETCD_SERVICE_CHAIN_UUID_PATH = "service_chain_uuid"
    _ETCD_SERVICE_CHAIN_IP_PATH = "service_chain_ip"

    def __init__(self, args, vnc_lib, logger):
        self._args = args
        self._etcd_args = vnc_etcd.etcd_args(args)
        self._logger = logger
        self._vnc_lib = vnc_lib
        self._rt_cf = RTHandler()
        logger_log = None
        if self._logger:
            self._logger.log(
                "VncObjectEtcdClient arguments: {}".format(self._etcd_args))
            logger_log = self._logger.log
        self._init_bgpaas_ports_index_allocator()
        super(SchemaTransformerEtcd, self).__init__(
            logger=logger_log, **self._etcd_args)

    def _init_bgpaas_ports_index_allocator(self):
        bgpaas_port_start = self._args.bgpaas_port_start
        bgpaas_port_end = self._args.bgpaas_port_end
        global_system_config = self._vnc_lib.global_system_config_read(
            fq_name=['default-global-system-config'])
        cfg_bgpaas_ports = global_system_config.get_bgpaas_parameters()
        if cfg_bgpaas_ports:
            bgpaas_port_start = cfg_bgpaas_ports.get_port_start()
            bgpaas_port_end = cfg_bgpaas_ports.get_port_end()
        try:
            self._vnc_lib.create_int_pool(
                self._BGPAAS_PORT_ALLOC_POOL, bgpaas_port_start, bgpaas_port_end)
        except RefsExistError:
            # int pool already allocated
            pass

    def _patch_encapsulate_value(self, obj):
        """Encaplsulate each obj in dictionary with single element having
        'value' key and `obj` value.
        This is required for backward compatibility.

        :param (obj) obj: Vanilla object from etcd
        :return: (dict) Obj with patched 'value' key
        """
        return {'value': obj}

    def _strip_sc_name(self, path, sc_name):
        """Strip prefix from etcd key."""
        pattern = r"^.*\/" + \
            re.escape(self._ETCD_SCHEMA_TRANSFORMER_PREFIX) + \
            r"\/[^/]+\/" + re.escape(sc_name) + r"\/"
        return re.sub(pattern, '', path)

    def _path_key(self, resource, key):
        """Combine and return self._prefix with given key.

        :param (str) key: key of an
        :param (str) resource: resourece prefix
        :return: (str) full key ie: "schema_transformer/service_chain_uuid/13eb9327-f40e-4ef1-8020-1c36af1b4b70"
        """
        return "%s/%s/%s" % (self._ETCD_SCHEMA_TRANSFORMER_PREFIX, resource, key)

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
        """List all service chain ip objects for sc_name.

        :param sc_name (str): ip service chain name
        :return: (dict) dictionary of IP data stored for sc_name
        """
        response = self._object_db.list_kv(
            self._path_key(self._ETCD_SERVICE_CHAIN_IP_PATH, sc_name))
        return {self._strip_sc_name(key, sc_name): value for (key, value) in response}

    def add_service_chain_ip(self, sc_name, ip_dict):
        """Store multiple values from ip_dict in etcd.

        For each ip_dict entry create a single entry in etcd.  Each etc
        key is created by combining prefix, _ETC_SERVICE_CHAIN_IP_PATH,
        sc_name and ip_dict key.

        :param sc_name (str): ip service chain name
        :param ip_dict (dict): dictionary to be stored
        """
        key_path = self._path_key(self._ETCD_SERVICE_CHAIN_IP_PATH, sc_name)
        for k, v in ip_dict.items():
            self._object_db.put_kv("%s/%s" % (key_path, k), v)

    def remove_service_chain_ip(self, sc_name):
        """Remove all data for sc_name in etcd.

        :param sc_name (str): ip service chain name
        """
        self._object_db.delete_path(self._path_key(
            self._ETCD_SERVICE_CHAIN_IP_PATH, sc_name))

    def list_service_chain_uuid(self):
        """List all objects with 'service_chain' object type.

        :return: (gen) Generator of fetched objects
        """
        response = self._object_db.list_kv(
            "%s/%s" % (self._ETCD_SCHEMA_TRANSFORMER_PREFIX, self._ETCD_SERVICE_CHAIN_UUID_PATH))
        return (self._patch_encapsulate_value(value) for _, value in response)

    def add_service_chain_uuid(self, key, value):
        """Store a value with a key created from combining prefix,
        _ETCD_SERVICE_CHAIN_UUID_PATH and key in etcd.

        :param key (str): identifier (UUID)
        :param value (Any): data to store, typically a string (object
                            serialized with jsonpickle.encode)
        """
        key_path = self._path_key(self._ETCD_SERVICE_CHAIN_UUID_PATH, key)
        self._object_db.put_kv(key_path, value)

    def remove_service_chain_uuid(self, key):
        """Remove a etcd entry with key created from combining prefix,
        _ETCD_SERVICE_CHAIN_UUID_PATH and key.

        :param key (str): identifier (UUID)
        """
        key_path = self._path_key(self._ETCD_SERVICE_CHAIN_UUID_PATH, key)
        self._object_db.delete_kv(key_path)

    def alloc_bgpaas_port(self, *args, **kwargs):
        """Allocate unique port for BGPaaS.

        params NOT USED, left for backward compatibility.
        :return: (int) allocated port number.
        """
        return self._vnc_lib.allocate_int(self._BGPAAS_PORT_ALLOC_POOL)

    def free_bgpaas_port(self, port):
        """Free allocated port for BGPaaS.

        :param port (int): Port number to be freed.
        """
        self._vnc_lib.deallocate_int(self._BGPAAS_PORT_ALLOC_POOL, port)
