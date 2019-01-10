# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
Schema transformer DB for etcd to allocate ids and store internal data
"""

from cfgm_common import vnc_object_db, vnc_etcd
from vnc_api.exceptions import RefsExistError


class RTHandler(object):
    def get_range(self):
        # TODO: Implement for etcd
        return []


class SchemaTransformerEtcd(vnc_object_db.VncObjectEtcdClient):

    _ETCD_SERVICE_CHAIN_PATH = "schema_transformer/service_chain"
    _BGPAAS_PORT_ALLOC_POOL = "bgpaas_port"

    def __init__(self, args, vnc_lib, logger):
        self._args = args
        self._etcd_args = vnc_etcd.etcd_args(args)
        self._logger = logger
        self._vnc_lib = vnc_lib
        self._rt_cf = RTHandler()
        self._logger.log(
            "VncObjectEtcdClient arguments: {}".format(self._etcd_args))

        super(SchemaTransformerEtcd, self).__init__(
            logger=self._logger.log, **self._etcd_args)

        def _init_bgpaas_ports_index_allocator():
            bgpaas_port_start = args.bgpaas_port_start
            bgpaas_port_end = args.bgpaas_port_end
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

        _init_bgpaas_ports_index_allocator()

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

    def alloc_bgpaas_port(self, name):
        """Allocate unique port for BGPaaS.

        :param name (str): NOT USED, left for backward compatibility.
        :return: (int) allocated port number.
        """
        return self._vnc_lib.allocate_int(self._BGPAAS_PORT_ALLOC_POOL)

    def free_bgpaas_port(self, port):
        """Free allocated port for BGPaaS.

        :param port (int): Port number to be freed.
        """
        self._vnc_lib.deallocate_int(self._BGPAAS_PORT_ALLOC_POOL, port)
