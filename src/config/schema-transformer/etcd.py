# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
Schema transformer DB for etcd to allocate ids and store internal data
"""

import cfgm_common as common
from cfgm_common.exceptions import VncError, NoIdError
from cfgm_common import vnc_object_db, vnc_etcd
from vnc_api.exceptions import RefsExistError


class RTHandler(object):
    def get_range(self):
        # TODO: Implement for etcd
        return []


class SchemaTransformerEtcd(vnc_object_db.VncObjectEtcdClient):
    _ROUTE_TARGET_NUMBER_ALLOC_POOL = "route_target_number"

    _BGPAAS_PORT_ALLOC_POOL = "bgpaas_port"
    _ETCD_SCHEMA_TRANSFORMER_PREFIX = "schema_transformer"
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
        self._rt_cf = RTHandler()
        self._logger.log(
            "VncObjectEtcdClient arguments: {}".format(self._etcd_args))
        self._init_bgpaas_ports_index_allocator()

        self._vlan_allocators = {}

        super(SchemaTransformerEtcd, self).__init__(
            logger=self._logger.log, **self._etcd_args)

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

    def _path_key(self, resource, key):
        """Combine and return self._prefix with given key.

        :param (str) res: resource type
        :param (str) key: key of an value
        :return: (str) full key ie: "/contrail/virtual_network/aaa-bbb-ccc"
        """
        return "%s/%s/%s" % (self._ETCD_SCHEMA_TRANSFORMER_PREFIX, resource, key)

    def allocate_service_chain_vlan(self, service_vm, service_chain):
        if service_vm not in self._vlan_allocators:
            int_pool = self._path_key(self._ETCD_VLAN_PATH, service_vm)
            self._vlan_allocators[service_vm] = int_pool
            try:
                self._vnc_lib.create_int_pool(int_pool,
                                              self._SC_MIN_VLAN,
                                              self._SC_MAX_VLAN)
            except RefsExistError:
                pass  # int pool already allocated

        alloc_kv = self._join_strings(self._vlan_allocators[service_vm],
                                      service_chain)
        try:
            vlan_int = self._object_db.get_kv(alloc_kv)
            if vlan_int:
                return int(vlan_int)
        except (KeyError, VncError, NoIdError):
            pass  # vlan int isn't allocated yet

        vlan_int = self._vnc_lib.allocate_int(self._vlan_allocators[service_vm])
        # TODO check if vlan_int has been allocated for this service_chain
        self._object_db.put_kv(alloc_kv, vlan_int)

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
        response = self._object_db.list_kv(
            self._ETCD_SCHEMA_TRANSFORMER_PREFIX+"/"+self._ETCD_ROUTE_TARGET_PATH)
        return (self._patch_encapsulate_value('rtgt_num', value) for key, value in response)

    def _get_route_target(self, ri_fq_name):
        key_path = self._path_key(self._ETCD_ROUTE_TARGET_PATH, ri_fq_name)
        return self._object_db.get_kv(key_path)

    def get_route_target(self, ri_fq_name):
        try:
            return self._get_route_target(ri_fq_name)
        except (VncError, NoIdError):
            return 0

    def alloc_route_target(self, ri_fq_name, alloc_only=False):
        if alloc_only:
            return self._vnc_lib.allocate_int(self._ROUTE_TARGET_NUMBER_ALLOC_POOL)

        rtgt = self.get_route_target(ri_fq_name)
        if rtgt < common.BGP_RTGT_MIN_ID:
            rtgt = self._vnc_lib.allocate_int(self._ROUTE_TARGET_NUMBER_ALLOC_POOL)
        else:
            # TODO check if rtgt has allocated for this ri_fq_name
            pass

        return rtgt

    def free_route_target(self, ri_fq_name):
        try:
            rtgt = self._get_route_target(ri_fq_name)
        except (VncError, NoIdError):
            return

        self._vnc_lib.deallocate_int(self._ROUTE_TARGET_NUMBER_ALLOC_POOL, rtgt)

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
        response = self._object_db.list_kv(
            self._ETCD_SCHEMA_TRANSFORMER_PREFIX+"/"+self._ETCD_SERVICE_CHAIN_PATH)
        return (self._patch_encapsulate_value('value', value) for key, value in response)

    def add_service_chain_uuid(self, key, value):
        """Store a value with a key created from combining prefix,
        _ETCD_SERVICE_CHAIN_PATH and key in etcd.

        :param key (str): identifier (UUID)
        :param value (Any): data to store, typically a string (object
                            serialized with jsonpickle.encode)
        """
        key_path = self._path_key(self._ETCD_SERVICE_CHAIN_PATH, key)
        self._object_db.put_kv(key_path, value)

    def remove_service_chain_uuid(self, key):
        """Remove a etcd entry with key created from combining prefix,
        _ETCD_SERVICE_CHAIN_PATH and key.

        :param key (str): identifier (UUID)
        """
        key_path = self._path_key(self._ETCD_SERVICE_CHAIN_PATH, key)
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
