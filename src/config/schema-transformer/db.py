# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Schema transformer DB to store ids allocated by it
"""
from pycassa import NotFoundException

import cfgm_common as common
from cfgm_common.zkclient import IndexAllocator
from cfgm_common.vnc_cassandra import VncCassandraClient
from sandesh_common.vns.constants import SCHEMA_KEYSPACE_NAME

class SchemaTransformerDB(VncCassandraClient):

    _KEYSPACE = SCHEMA_KEYSPACE_NAME
    _RT_CF = 'route_target_table'
    _SC_IP_CF = 'service_chain_ip_address_table'
    _SERVICE_CHAIN_CF = 'service_chain_table'
    _SERVICE_CHAIN_UUID_CF = 'service_chain_uuid_table'
    _zk_path_prefix = ''

    _BGP_RTGT_MAX_ID = 1 << 24
    _BGP_RTGT_ALLOC_PATH = "/id/bgp/route-targets/"

    _VN_MAX_ID = 1 << 24
    _VN_ID_ALLOC_PATH = "/id/virtual-networks/"

    _SECURITY_GROUP_MAX_ID = 1 << 32
    _SECURITY_GROUP_ID_ALLOC_PATH = "/id/security-groups/id/"

    _SERVICE_CHAIN_MAX_VLAN = 4093
    _SERVICE_CHAIN_VLAN_ALLOC_PATH = "/id/service-chain/vlan/"


    @classmethod
    def get_db_info(cls):
        db_info = [(cls._KEYSPACE, [cls._RT_CF, cls._SC_IP_CF,
                                    cls._SERVICE_CHAIN_CF,
                                    cls._SERVICE_CHAIN_UUID_CF])]
        return db_info
    # end get_db_info

    def __init__(self, manager, zkclient):
        self._manager = manager
        self._args = manager._args
        self._zkclient = zkclient

        if self._args.cluster_id:
            self._keyspace = '%s_%s' % (self._args.cluster_id, self._KEYSPACE)
            self._zk_path_pfx = self._args.cluster_id + '/'
        else:
            self._keyspace = self._KEYSPACE
            self._zk_path_pfx = ''

        keyspaces = {
            self._keyspace: [(self._RT_CF, None),
                             (self._SC_IP_CF, None),
                             (self._SERVICE_CHAIN_CF, None),
                             (self._SERVICE_CHAIN_UUID_CF, None)]}
        cass_server_list = self._args.cassandra_server_list

        if self._args.reset_config:
            cass_reset_config = [self._keyspace]
        else:
            cass_reset_config = []

        super(SchemaTransformerDB, self).__init__(
            cass_server_list, self._args.cluster_id, keyspaces,
            manager.config_log, reset_config=cass_reset_config)

        SchemaTransformerDB._rt_cf = self._cf_dict[self._RT_CF]
        SchemaTransformerDB._sc_ip_cf = self._cf_dict[self._SC_IP_CF]
        SchemaTransformerDB._service_chain_cf = self._cf_dict[
            self._SERVICE_CHAIN_CF]
        SchemaTransformerDB._service_chain_uuid_cf = self._cf_dict[
            self._SERVICE_CHAIN_UUID_CF]

        # reset zookeeper config
        if self._args.reset_config:
            zkclient.delete_node(self._zk_path_pfx + "/id", True)

        self._vn_id_allocator = IndexAllocator(
            zkclient, self._zk_path_pfx+self._VN_ID_ALLOC_PATH, self._VN_MAX_ID)
        self._sg_id_allocator = IndexAllocator(
            zkclient, self._zk_path_pfx+self._SECURITY_GROUP_ID_ALLOC_PATH,
            self._SECURITY_GROUP_MAX_ID)

        # 0 is not a valid sg id any more. So, if it was previously allocated,
        # delete it and reserve it
        if self._sg_id_allocator.read(0) != '__reserved__':
            self._sg_id_allocator.delete(0)
        self._sg_id_allocator.reserve(0, '__reserved__')

        self._rt_allocator = IndexAllocator(
            zkclient, self._zk_path_pfx+self._BGP_RTGT_ALLOC_PATH,
            self._BGP_RTGT_MAX_ID, common.BGP_RTGT_MIN_ID)

        self._sc_vlan_allocator_dict = {}
    # end __init__

    def allocate_service_chain_vlan(self, service_vm, service_chain):
        alloc_new = False
        if service_vm not in self._sc_vlan_allocator_dict:
            self._sc_vlan_allocator_dict[service_vm] = IndexAllocator(
                self._zkclient,
                (self._zk_path_prefix + self._SERVICE_CHAIN_VLAN_ALLOC_PATH +
                 service_vm + '/'),
                self._SERVICE_CHAIN_MAX_VLAN)

        vlan_ia = self._sc_vlan_allocator_dict[service_vm]

        try:
            vlan = int(
                self._service_chain_cf.get(service_vm)[service_chain])
            db_sc = vlan_ia.read(vlan)
            if (db_sc is None) or (db_sc != service_chain):
                alloc_new = True
        except (KeyError, NotFoundException):
            alloc_new = True

        if alloc_new:
            vlan = vlan_ia.alloc(service_chain)
            self._service_chain_cf.insert(service_vm,
                                          {service_chain: str(vlan)})

        # Since vlan tag 0 is not valid, increment before returning
        return vlan + 1
    # end allocate_service_chain_vlan

    def free_service_chain_vlan(self, service_vm, service_chain):
        try:
            vlan_ia = self._sc_vlan_allocator_dict[service_vm]
            vlan = int(self._service_chain_cf.get(service_vm)[service_chain])
            self._service_chain_cf.remove(service_vm, [service_chain])
            vlan_ia.delete(vlan)
            if vlan_ia.empty():
                del self._sc_vlan_allocator_dict[service_vm]
        except (KeyError, NotFoundException):
            pass
    # end free_service_chain_vlan

    def get_route_target(self, ri_fq_name):
        try:
            return int(self._rt_cf.get(ri_fq_name)['rtgt_num'])
        except NotFoundException:
            return 0

    def alloc_route_target(self, ri_fq_name, zk_only=False):
        alloc_new = False

        if zk_only:
            alloc_new = True
        else:
            rtgt_num = self.get_route_target(ri_fq_name)
            if rtgt_num < common.BGP_RTGT_MIN_ID:
                alloc_new = True
            else:
                rtgt_ri_fq_name_str = self._rt_allocator.read(rtgt_num)
                if (rtgt_ri_fq_name_str != ri_fq_name):
                    alloc_new = True

        if (alloc_new):
            rtgt_num = self._rt_allocator.alloc(ri_fq_name)
            self._rt_cf.insert(ri_fq_name, {'rtgt_num': str(rtgt_num)})

        return rtgt_num
    # end alloc_route_target

    def free_route_target_by_number(self, rtgt):
        self._rt_allocator.delete(rtgt)

    def free_route_target(self, ri_fq_name):
        try:
            rtgt = self.get_route_target(ri_fq_name)
            self._rt_cf.remove(ri_fq_name)
        except NotFoundException:
            pass
        self._rt_allocator.delete(rtgt)
    # end free_route_target

    def get_service_chain_ip(self, sc_name):
        try:
            return self._sc_ip_cf.get(sc_name)['ip_address']
        except NotFoundException:
            return None

    def add_service_chain_ip(self, sc_name, ip):
        self._sc_ip_cf.insert(sc_name, {'ip_address': ip})

    def remove_service_chain_ip(self, sc_name):
        try:
            self._sc_ip_cf.remove(sc_name)
        except NotFoundException:
            pass

    def list_service_chain_uuid(self):
        try:
            return self._service_chain_uuid_cf.get_range()
        except NotFoundException:
            return []

    def add_service_chain_uuid(self, name, value):
        self._service_chain_uuid_cf.insert(name, {'value': value})

    def remove_service_chain_uuid(self, name):
        try:
            self._service_chain_uuid_cf.remove(name)
        except NotFoundException:
            pass

    def get_sg_from_id(self, sg_id):
        return self._sg_id_allocator.read(sg_id)

    def alloc_sg_id(self, name):
        return self._sg_id_allocator.alloc(name)

    def free_sg_id(self, sg_id):
        self._sg_id_allocator.delete(sg_id)

    def get_vn_from_id(self, vn_id):
        return self._vn_id_allocator.read(vn_id)

    def alloc_vn_id(self, name):
        return self._vn_id_allocator.alloc(name)

    def free_vn_id(self, vn_id):
        self._vn_id_allocator.delete(vn_id)
