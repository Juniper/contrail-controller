# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Schema transformer DB to store ids allocated by it
"""
from cfgm_common import vnc_object_db, vnc_etcd

# /usr/bin/contrail-schema --conf_file /etc/contrail/contrail-schema.conf --conf_file /etc/contrail/contrail-keystone-auth.conf

class RTHandler(object):
    def get_range(self):
        return []

class SchemaTransformerETCD(vnc_object_db.VncObjectEtcdClient):

    def __init__(self, args, logger):
        self._etcd_args = vnc_etcd.etcd_args(args)
        self._db_logger = logger
        self._rt_cf = RTHandler()
        self._db_logger.log("VncObjectEtcdClient arguments: {}".format(self._etcd_args))
        super(SchemaTransformerETCD, self).__init__(logger=self._db_logger.log, **self._etcd_args)

    def allocate_service_chain_vlan(self, service_vm, service_chain):
        return 1234
    # end allocate_service_chain_vlan

    def free_service_chain_vlan(self, service_vm, service_chain):
        pass

    def get_route_target(self, ri_fq_name):
        return 0

    def alloc_route_target(self, ri_fq_name, zk_only=False):
        return 1234
    # end alloc_route_target

    def free_route_target(self, ri_fq_name):
        pass
    # end free_route_target

    def get_service_chain_ip(self, sc_name):
        return self.get(self._SC_IP_CF, sc_name)

    def add_service_chain_ip(self, sc_name, ip_dict):
        pass

    def remove_service_chain_ip(self, sc_name):
        pass

    def list_service_chain_uuid(self):
        return []

    def add_service_chain_uuid(self, name, value):
        pass

    def remove_service_chain_uuid(self, name):
        pass

    def get_bgpaas_port(self, port):
        return 1234

    def alloc_bgpaas_port(self, name):
        return 1234

    def free_bgpaas_port(self, port):
        pass