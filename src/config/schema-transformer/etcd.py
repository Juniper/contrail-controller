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

    def __init__(self, args, logger):
        self._etcd_args = vnc_etcd.etcd_args(args)
        self._db_logger = logger
        self._rt_cf = RTHandler()
        self._db_logger.log("VncObjectEtcdClient arguments: {}".format(self._etcd_args))
        super(SchemaTransformerEtcd, self).__init__(logger=self._db_logger.log, **self._etcd_args)

    def allocate_service_chain_vlan(self, service_vm, service_chain):
        #TODO: Implement for etcd
        return 1234

    def free_service_chain_vlan(self, service_vm, service_chain):
        #TODO: Implement for etcd
        pass

    def get_route_target(self, ri_fq_name):
        #TODO: Implement for etcd
        return 0

    def alloc_route_target(self, ri_fq_name, zk_only=False):
        #TODO: Implement for etcd
        return 1234

    def free_route_target(self, ri_fq_name):
        #TODO: Implement for etcd
        pass

    def get_service_chain_ip(self, sc_name):
        #TODO: Implement for etcd
        return None

    def add_service_chain_ip(self, sc_name, ip_dict):
        #TODO: Implement for etcd
        pass

    def remove_service_chain_ip(self, sc_name):
        #TODO: Implement for etcd
        pass

    def list_service_chain_uuid(self):
        #TODO: Implement for etcd
        return []

    def add_service_chain_uuid(self, name, value):
        #TODO: Implement for etcd
        pass

    def remove_service_chain_uuid(self, name):
        #TODO: Implement for etcd
        pass

    def get_bgpaas_port(self, port):
        #TODO: Implement for etcd
        return 1234

    def alloc_bgpaas_port(self, name):
        #TODO: Implement for etcd
        return 1234

    def free_bgpaas_port(self, port):
        #TODO: Implement for etcd
        pass

