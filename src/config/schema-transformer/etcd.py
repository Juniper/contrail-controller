# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
Schema transformer DB for etcd to allocate ids and store internal data
"""

import jsonpickle

import cfgm_common as common
from cfgm_common.exceptions import VncError, NoIdError
from cfgm_common import vnc_object_db, vnc_etcd
from vnc_api.exceptions import RefsExistError


class SchemaTransformerEtcd(vnc_object_db.VncObjectEtcdClient):
    _ETCD_SCHEMA_TRANSFORMER_PREFIX = "schema_transformer"
    _BGPAAS_PORT_ALLOC_POOL = "bgpaas_port"
    _ETCD_SERVICE_CHAIN_UUID_PATH = "service_chain_uuid"
    _ETCD_SERVICE_CHAIN_IP_PATH = "service_chain_ip"
    _ROUTE_TARGET_NUMBER_ALLOC_POOL = "route_target_number"
    _ETCD_SERVICE_CHAIN_PATH = "service_chain"
    _ETCD_ROUTE_TARGET_PATH = "route_target"
    _ETCD_VLAN_PATH = "vlan"

    _SC_MIN_VLAN = 1
    _SC_MAX_VLAN = 4093

    def __init__(self, args, vnc_lib, logger):
        self._args = args
        self._etcd_args = vnc_etcd.etcd_args(args)
        self._logger = logger
        self._vnc_lib = vnc_lib
        logger_log = None
        if self._logger:
            self._logger.log(
                "VncObjectEtcdClient arguments: {}".format(self._etcd_args))
            logger_log = self._logger.log
        self._init_bgpaas_ports_index_allocator()

        self._vlan_allocators = {}

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

    def _patch_encapsulate_value(self, key, obj):
        """Encaplsulate each obj in dictionary with single element having
        'value' key and `obj` value.
        This is required for backward compatibility.

        :param (key) obj: key value
        :param (obj) obj: Vanilla object from etcd
        :return: (dict) Obj with patched 'value' key
        """
        return {key: obj}

    def _get_key_from_path(self, resource, path):
        """Strip prefix from etcd key."""
        return path.replace(self._path_key(resource, ''), '', 1)

    def _path_key(self, resource, key):
        """Combine and return self._prefix with given key.

        :param (str) key: key of an
        :param (str) resource: resourece prefix
        :return: (str) full key ie: "schema_transformer/service_chain_uuid/13eb9327-f40e-4ef1-8020-1c36af1b4b70"
        """
        return "%s/%s/%s" % (self._ETCD_SCHEMA_TRANSFORMER_PREFIX, resource, key)

    def allocate_service_chain_vlan(self, service_vm, service_chain):
        if service_vm not in self._vlan_allocators:
            vlan_alloc_path = self._path_key(self._ETCD_VLAN_PATH, service_vm)
            self._vlan_allocators[service_vm] = vlan_alloc_path
            try:
                self._vnc_lib.create_int_pool(vlan_alloc_path,
                                              self._SC_MIN_VLAN,
                                              self._SC_MAX_VLAN)
            except RefsExistError:
                pass  # int pool already allocated

        vlan_int_path = self._join_strings(self._vlan_allocators[service_vm],
                                           service_chain)
        try:
            vlan_int = self._object_db.get_kv(vlan_int_path)
            int_owner = self._vnc_lib.get_int_owner(self._vlan_allocators[service_vm], vlan_int)
            if vlan_int and int_owner == service_chain:
                return int(vlan_int)
        except (KeyError, VncError, NoIdError):
            pass  # vlan int isn't allocated yet

        vlan_int = self._vnc_lib.allocate_int(self._vlan_allocators[service_vm], service_chain)
        self._object_db.put_kv(vlan_int_path, vlan_int)

        return int(vlan_int)

    def free_service_chain_vlan(self, service_vm, service_chain):
        if service_vm in self._vlan_allocators:
            int_pool = self._vlan_allocators[service_vm]
            alloc_kv = self._join_strings(int_pool, service_chain)
            del self._vlan_allocators[service_vm]
        else:
            int_pool = self._path_key(self._ETCD_VLAN_PATH, service_vm)
            alloc_kv = self._join_strings(int_pool, service_chain)

        try:
            vlan_int = self._object_db.get_kv(alloc_kv)
        except (VncError, NoIdError):
            pass  # vlan int already deallocated
        else:
            self._vnc_lib.deallocate_int(int_pool, vlan_int)
            self._object_db.delete_kv(alloc_kv)

    def _join_strings(self, prefix, suffix, char=":"):
        return "{p}{c}{s}".format(p=prefix, s=suffix, c=char)

    def get_route_target_range(self):
        """List routing instance fq names and associated route target numbers.

        :return: (gen) fq name and route target pair
        """
        path = self._path_key(self._ETCD_ROUTE_TARGET_PATH, '')
        response = self._object_db.list_kv(path)
        return ((self._get_key_from_path(self._ETCD_ROUTE_TARGET_PATH, key),
                 self._patch_encapsulate_value('rtgt_num', value)) for key, value in response)

    def _get_route_target(self, ri_fq_name):
        key_path = self._path_key(self._ETCD_ROUTE_TARGET_PATH, ri_fq_name)
        return self._object_db.get_kv(key_path)

    def get_route_target(self, ri_fq_name):
        """Get route target number associated with routing instance

        :param (str) ri_fq_name: routing instance fq name
        :return: (int) route target number
        """
        rtgt = self._get_route_target(ri_fq_name)
        if rtgt is None:
            return 0
        return int(rtgt)

    def alloc_route_target(self, ri_fq_name, alloc_only=False):
        """Allocate route target number and routing instance in db

        :param (str) ri_fq_name: routing instance fq name
        :param (bool) alloc_only: allocate only do not store route target in db
        :return: (int) allocated route target number
        """
        if alloc_only:
            return self._vnc_lib.allocate_int(self._ROUTE_TARGET_NUMBER_ALLOC_POOL, ri_fq_name)

        rtgt = self.get_route_target(ri_fq_name)
        owner = self._vnc_lib.get_int_owner(self._ROUTE_TARGET_NUMBER_ALLOC_POOL, rtgt)
        if rtgt >= common.BGP_RTGT_MIN_ID and owner == ri_fq_name:
            return rtgt
        rtgt = self._vnc_lib.allocate_int(self._ROUTE_TARGET_NUMBER_ALLOC_POOL, ri_fq_name)
        key_path = self._path_key(self._ETCD_ROUTE_TARGET_PATH, ri_fq_name)
        self._object_db.put_kv(key_path, str(rtgt))

        return rtgt

    def free_route_target(self, ri_fq_name):
        """Free route target number and remove routing instance from db

        :param (str) ri_fq_name: routing instance fq name
        """
        rtgt = self._get_route_target(ri_fq_name)
        if rtgt is None:
            return

        self._vnc_lib.deallocate_int(
            self._ROUTE_TARGET_NUMBER_ALLOC_POOL, int(rtgt))
        key_path = self._path_key(self._ETCD_ROUTE_TARGET_PATH, ri_fq_name)
        self._object_db.delete_kv(key_path)

    def get_service_chain_ip(self, sc_name):
        """List all service chain ip objects for sc_name.

        :param sc_name (str): ip service chain name
        :return: (dict) dictionary of IP data stored for sc_name
        """
        value = self._object_db.get_value(self._path_key(
            self._ETCD_SERVICE_CHAIN_IP_PATH, sc_name))
        if value:
            result = jsonpickle.decode(value)
        else:
            result = {}
        return result

    def add_service_chain_ip(self, sc_name, ip_dict):
        """Store multiple values from ip_dict in etcd.

        For each ip_dict entry create a single entry in etcd.  Each etc
        key is created by combining prefix, _ETC_SERVICE_CHAIN_IP_PATH,
        sc_name and ip_dict key.

        :param sc_name (str): ip service chain name
        :param ip_dict (dict): dictionary to be stored
        """
        self._object_db.put_kv(
            self._path_key(self._ETCD_SERVICE_CHAIN_IP_PATH, sc_name),
            jsonpickle.encode(ip_dict))

    def remove_service_chain_ip(self, sc_name):
        """Remove all data for sc_name in etcd.

        :param sc_name (str): ip service chain name
        """
        self._object_db.delete_kv(self._path_key(
            self._ETCD_SERVICE_CHAIN_IP_PATH, sc_name))

    def list_service_chain_uuid(self):
        """List all objects with 'service_chain' object type.

        :return: (gen) Generator of fetched objects
        """
        response = self._object_db.list_kv(
            "%s/%s" % (self._ETCD_SCHEMA_TRANSFORMER_PREFIX, self._ETCD_SERVICE_CHAIN_UUID_PATH))
        return (self._patch_encapsulate_value('value', value) for _, value in response)

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

    def bgpaas_port_allocator_reallocate(self, new_range):
        """Reallocate bgpaas_port allocator setting new start and end values.

        Keyword arguments:
        :param new_range (dict): A dictionary with at least two keys: 'start'
                                 and 'end'
        """
        try:
            # TODO(Mateusz Neumann): implement reallocate_int method in
            # vnc_api and then remove try/catch block
            self._vnc_lib.reallocate_int(self._BGPAAS_PORT_ALLOC_POOL,
                                         new_range)
        except Exception:
            self._logger.warning("Silently skipping bgpaas_port_allocator " +
                                 "reallocate: not yet implemented for etcd.")
