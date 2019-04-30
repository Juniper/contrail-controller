# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Schema transformer DB to store ids allocated by it
"""
from pycassa import NotFoundException

import cfgm_common as common
from cfgm_common.exceptions import VncError, NoIdError
from cfgm_common.zkclient import IndexAllocator
from cfgm_common.vnc_object_db import VncObjectDBClient
from sandesh_common.vns.constants import SCHEMA_KEYSPACE_NAME
import uuid

class SchemaTransformerDB(VncObjectDBClient):

    _KEYSPACE = SCHEMA_KEYSPACE_NAME
    _RT_CF = 'route_target_table'
    _SC_IP_CF = 'service_chain_ip_address_table'
    _SERVICE_CHAIN_CF = 'service_chain_table'
    _SERVICE_CHAIN_UUID_CF = 'service_chain_uuid_table'
    _zk_path_prefix = ''

    _BGP_RTGT_MAX_ID_TYPE0 = 1 << 24
    # We don't left shift by 16 below to reserve certain target
    # values if required for future use
    _BGP_RTGT_MAX_ID_TYPE1_2 = 1 << 15

    _SERVICE_CHAIN_MAX_VLAN = 4093
    _SERVICE_CHAIN_VLAN_ALLOC_PATH = "/id/service-chain/vlan/"

    _BGPAAS_PORT_ALLOC_PATH = "/id/bgpaas/port/"

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
            self._zk_path_pfx = self._args.cluster_id
        else:
            self._zk_path_pfx = ''

        keyspaces = {
            self._KEYSPACE: {self._RT_CF: {},
                             self._SC_IP_CF: {},
                             self._SERVICE_CHAIN_CF: {},
                             self._SERVICE_CHAIN_UUID_CF: {}}}
        cass_server_list = self._args.cassandra_server_list

        cred = None
        if (self._args.cassandra_user is not None and
            self._args.cassandra_password is not None):
            cred={'username':self._args.cassandra_user,
                  'password':self._args.cassandra_password}

        super(SchemaTransformerDB, self).__init__(
            cass_server_list, self._args.cluster_id, keyspaces, None,
            manager.logger.log, reset_config=self._args.reset_config,
            credential=cred, ssl_enabled=self._args.cassandra_use_ssl,
            ca_certs=self._args.cassandra_ca_certs)

        SchemaTransformerDB._rt_cf = self._cf_dict[self._RT_CF]
        SchemaTransformerDB._sc_ip_cf = self._cf_dict[self._SC_IP_CF]
        SchemaTransformerDB._service_chain_cf = self._cf_dict[
            self._SERVICE_CHAIN_CF]
        SchemaTransformerDB._service_chain_uuid_cf = self._cf_dict[
            self._SERVICE_CHAIN_UUID_CF]

        # reset zookeeper config
        if self._args.reset_config:
            zkclient.delete_node(
                self._zk_path_pfx + common.BGP_RTGT_ALLOC_PATH_TYPE0, True)
            zkclient.delete_node(
                self._zk_path_pfx + common.BGP_RTGT_ALLOC_PATH_TYPE1_2, True)
            zkclient.delete_node(
                 self._zk_path_pfx + self._BGPAAS_PORT_ALLOC_PATH, True)
            zkclient.delete_node(
                self._zk_path_pfx + self._SERVICE_CHAIN_VLAN_ALLOC_PATH, True)

        def _init_index_allocators():
            bgpaas_port_start = self._args.bgpaas_port_start
            bgpaas_port_end = self._args.bgpaas_port_end
            gsc_fq_name = ['default-global-system-config']

            gsc_uuid = self._object_db.fq_name_to_uuid(obj_type='global_system_config',
                                                       fq_name=gsc_fq_name)

            _, cfg = self._object_db.object_read('global_system_config', [gsc_uuid])
            cfg_bgpaas_ports = cfg[0].get('bgpaas_parameters')
            if cfg_bgpaas_ports:
                bgpaas_port_start = cfg_bgpaas_ports['port_start']
                bgpaas_port_end = cfg_bgpaas_ports['port_end']

            self._bgpaas_port_allocator = IndexAllocator(
                zkclient, self._zk_path_pfx + self._BGPAAS_PORT_ALLOC_PATH,
                bgpaas_port_end - bgpaas_port_start, bgpaas_port_start)

            self._rt_allocator = IndexAllocator(
                zkclient, self._zk_path_pfx+common.BGP_RTGT_ALLOC_PATH_TYPE0,
                common._BGP_RTGT_MAX_ID_TYPE0, common._BGP_RTGT_MIN_ID_TYPE0)
            self._rt_allocator_4 = IndexAllocator(
                zkclient, self._zk_path_pfx+common.BGP_RTGT_ALLOC_PATH_TYPE1_2,
                common._BGP_RTGT_MAX_ID_TYPE1_2, common._BGP_RTGT_MIN_ID_TYPE1_2)

        _init_index_allocators()

        self._sc_vlan_allocator_dict = {}
        self._upgrade_vlan_alloc_path()
    # end __init__

    def _upgrade_vlan_alloc_path(self):
        # In earlier releases, allocation path for vlans did not end with '/'.
        # This caused the allocated numbers to be just appended to the vm id
        # instead of being created as a child of it. That caused the vlan ids
        # to be leaked when process restarted. With that being fixed, we need
        # to change any vlan ids that were allocated in prior releases to the
        # new format.
        vlan_alloc_path = (self._zk_path_prefix +
                           self._SERVICE_CHAIN_VLAN_ALLOC_PATH)
        for item in self._zkclient.get_children(vlan_alloc_path):
            try:
                # in the old format, item was vm id followed by 10 digit vlan
                # id allocated. Try to parse it to determine if it is still in
                # old format
                vm_id = uuid.UUID(item[:-10])
                vlan_id = int(item[-10:])
            except ValueError:
                continue
            sc_id = self._zkclient.read_node(vlan_alloc_path+item)
            self._zkclient.delete_node(vlan_alloc_path+item)
            self._zkclient.create_node(
                vlan_alloc_path+item[:-10]+'/'+item[-10:], sc_id)
        # end for item

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
            vlan = int(self.get_one_col(self._SERVICE_CHAIN_CF,
                                        service_vm, service_chain))
            db_sc = vlan_ia.read(vlan)
            if (db_sc is None) or (db_sc != service_chain):
                alloc_new = True
        except (KeyError, VncError, NoIdError):
            # TODO(ethuleau): VncError is raised if more than one row was
            #                 fetched from db with get_one_col method.
            #                 Probably need to be cleaned
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
            vlan = int(self.get_one_col(self._SERVICE_CHAIN_CF,
                                        service_vm, service_chain))
            self._service_chain_cf.remove(service_vm, [service_chain])
            vlan_ia.delete(vlan)
            if vlan_ia.empty():
                del self._sc_vlan_allocator_dict[service_vm]
        except (KeyError, VncError, NoIdError):
            # TODO(ethuleau): VncError is raised if more than one row was
            #                 fetched from db with get_one_col method.
            #                 Probably need to be cleaned
            pass
    # end free_service_chain_vlan

    def get_route_target(self, ri_fq_name):
        try:
            return int(self.get_one_col(self._RT_CF, ri_fq_name, 'rtgt_num'))
        except (VncError, NoIdError):
            # TODO(ethuleau): VncError is raised if more than one row was
            #                 fetched from db with get_one_col method.
            #                 Probably need to be cleaned
            return 0

    def get_zk_route_target_allocator(self, asn):
        if (asn > 0xFFFF):
            return self._rt_allocator_4
        else:
            return self._rt_allocator

    def alloc_route_target(self, ri_fq_name, asn, zk_only=False):
        alloc_new = False

        self.current_rt_allocator = self.get_zk_route_target_allocator(asn)

        if zk_only:
            alloc_new = True
        else:
            rtgt_num = self.get_route_target(ri_fq_name)
            if rtgt_num < common.get_bgp_rtgt_min_id(asn):
                alloc_new = True
            else:
                rtgt_ri_fq_name_str = self.current_rt_allocator.read(rtgt_num)
                if (rtgt_ri_fq_name_str != ri_fq_name):
                    alloc_new = True

        if (alloc_new):
            rtgt_num = self.current_rt_allocator.alloc(ri_fq_name)
            self._rt_cf.insert(ri_fq_name, {'rtgt_num': str(rtgt_num)})

        return rtgt_num
    # end alloc_route_target

    def free_route_target(self, ri_fq_name,asn):
        try:
            rtgt = self.get_route_target(ri_fq_name)
            self._rt_cf.remove(ri_fq_name)
        except NotFoundException:
            pass

        self.current_rt_allocator = self.get_zk_route_target_allocator(asn)
        self.current_rt_allocator.delete(rtgt)
    # end free_route_target

    def get_ri_from_route_target(self, rtgt_num,asn):
       self.current_rt_allocator = self.get_zk_route_target_allocator(asn)
       return self.current_rt_allocator.read(rtgt_num)

    def delete_route_target_directory(self, path):
        for zk_node in self._zkclient.get_children(path):
            # The return value of read_node will be an list
            # where 0th element is fq_name and 1st element is node stat for
            # the Zookeeper node
            znode  = self._zkclient.read_node(
                '%s/%s/%s' % (self._zk_path_pfx, path, zk_node),
                include_timestamp=True)
            # Delete all the zk nodes that are not sub-directories
            if znode is not None and znode[1].numChildren == 0:
                self._zkclient.delete_node('%s/%s/%s' % (self._zk_path_pfx, path, zk_node))

    def get_service_chain_ip(self, sc_name):
        return self.get(self._SC_IP_CF, sc_name)

    def add_service_chain_ip(self, sc_name, ip_dict):
        self._sc_ip_cf.insert(sc_name, ip_dict)

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

    def get_bgpaas_port(self, port):
        return self._bgpaas_allocator.read(port)

    def alloc_bgpaas_port(self, name):
        return self._bgpaas_port_allocator.alloc(name)

    def free_bgpaas_port(self, port):
        self._bgpaas_port_allocator.delete(port)
