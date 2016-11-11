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
from cfgm_common.vnc_object_db import VncObjectDBClient
from sandesh_common.vns.constants import SCHEMA_KEYSPACE_NAME
import uuid

class SchemaTransformerDB(VncObjectDBClient):

    _zk_path_prefix = ''

    _BGP_RTGT_MAX_ID = 1 << 24
    _BGP_RTGT_ALLOC_PATH = "/id/bgp/route-targets/"

    _VN_MAX_ID = 1 << 24
    _VN_ID_ALLOC_PATH = "/id/virtual-networks/"

    _SECURITY_GROUP_MAX_ID = 1 << 32
    _SECURITY_GROUP_ID_ALLOC_PATH = "/id/security-groups/id/"

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
        self._db_engine = self._args.db_engine
        db_engine = self._db_engine
        if self._args.cluster_id:
            self._zk_path_pfx = self._args.cluster_id + '/'
        else:
            self._zk_path_pfx = ''

        cred = None
        if db_engine == 'cassandra':
            self._db = zkclient
            cass_server_list = self._args.cassandra_server_list

            if (self._args.cassandra_user is not None and
                self._args.cassandra_password is not None):
                cred={'username':self._args.cassandra_user,
                    'password':self._args.cassandra_password}

            super(SchemaTransformerDB, self).__init__(
                cass_server_list, self._args.cluster_id, None, None,
                manager.logger.log, reset_config=self._args.reset_config,
                credential=cred, db_engine=db_engine)

            # reset zookeeper config
            if self._args.reset_config:
                zkclient.delete_node(
                    self._zk_path_pfx + self._BGP_RTGT_ALLOC_PATH, True)
                zkclient.delete_node(
                    self._zk_path_pfx + self._BGPAAS_PORT_ALLOC_PATH, True)
                zkclient.delete_node(
                    self._zk_path_pfx + self._SERVICE_CHAIN_VLAN_ALLOC_PATH, True)

            self._upgrade_vlan_alloc_path()
        elif db_engine == 'rdbms':
            db_server_list = self._args.rdbms_server_list

            if self._args.rdbms_user is not None and self._args.rdbms_password is not None:
                cred = {'username': self._args.rdbms_user,
                        'password': self._args.rdbms_password}

            super(SchemaTransformerDB, self).__init__(
                db_server_list, self._args.cluster_id, None, None,
                manager.logger.log, reset_config=self._args.reset_config,
                credential=cred, connection=self._args.rdbms_connection,
                db_engine=db_engine)
            self._db = self._object_db.db

        # TODO(ethuleau): We keep the virtual network and security group ID
        #                 allocation in schema and in the vnc API for one
        #                 release overlap to prevent any upgrade issue. So the
        #                 following code need to be remove in release (3.2 + 1)
        self._vn_id_allocator = self.create_index_allocator(
            self._db, self._zk_path_pfx+self._VN_ID_ALLOC_PATH, self._VN_MAX_ID)
        self._sg_id_allocator = self.create_index_allocator(
            self._db, self._zk_path_pfx+self._SECURITY_GROUP_ID_ALLOC_PATH,
            self._SECURITY_GROUP_MAX_ID)

        # 0 is not a valid sg id any more. So, if it was previously allocated,
        # delete it and reserve it
        if self._sg_id_allocator.read(0) != '__reserved__':
            self._sg_id_allocator.delete(0)
        self._sg_id_allocator.reserve(0, '__reserved__')

        self._rt_allocator = self.create_index_allocator(
            self._db, self._zk_path_pfx+self._BGP_RTGT_ALLOC_PATH,
            self._BGP_RTGT_MAX_ID, common.BGP_RTGT_MIN_ID)

        self._bgpaas_port_allocator = self.create_index_allocator(
            self._db, self._zk_path_pfx + self._BGPAAS_PORT_ALLOC_PATH,
            self._args.bgpaas_port_end - self._args.bgpaas_port_start,
            self._args.bgpaas_port_start)

        self._sc_vlan_allocator_dict = {}
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
        for item in self._db.get_children(vlan_alloc_path):
            try:
                # in the old format, item was vm id followed by 10 digit vlan
                # id allocated. Try to parse it to determine if it is still in
                # old format
                vm_id = uuid.UUID(item[:-10])
                vlan_id = int(item[-10:])
            except ValueError:
                continue
            sc_id = self._db.read_node(vlan_alloc_path+item)
            self._db.delete_node(vlan_alloc_path+item)
            self._db.create_node(
                vlan_alloc_path+item[:-10]+'/'+item[-10:], sc_id)
        # end for item


    def allocate_service_chain_vlan(self, service_vm, service_chain):
        alloc_new = False
        if service_vm not in self._sc_vlan_allocator_dict:
            self._sc_vlan_allocator_dict[service_vm] = self.create_index_allocator(
                self._db,
                (self._zk_path_prefix + self._SERVICE_CHAIN_VLAN_ALLOC_PATH +
                 service_vm + '/'),
                self._SERVICE_CHAIN_MAX_VLAN)

        vlan_ia = self._sc_vlan_allocator_dict[service_vm]

        try:
            vlan = self.get_service_chain_vlan(service_vm, service_chain)
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
            self.set_service_chain_vlan(service_vm, service_chain, vlan)

        # Since vlan tag 0 is not valid, increment before returning
        return vlan + 1
    # end allocate_service_chain_vlan

    def free_service_chain_vlan(self, service_vm, service_chain):
        try:
            vlan_ia = self._sc_vlan_allocator_dict[service_vm]
            vlan = self.get_service_chain_vlan(service_vm, service_chain)
            self.remove_service_chain_vlan(service_vm, service_chain)
            vlan_ia.delete(vlan)
            if vlan_ia.empty():
                del self._sc_vlan_allocator_dict[service_vm]
        except (KeyError, VncError, NoIdError):
            # TODO(ethuleau): VncError is raised if more than one row was
            #                 fetched from db with get_one_col method.
            #                 Probably need to be cleaned
            pass
    # end free_service_chain_vlan

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
            self.set_route_target(ri_fq_name, rtgt_num)

        return rtgt_num
    # end alloc_route_target

    def free_route_target_by_number(self, rtgt):
        self._rt_allocator.delete(rtgt)

    def free_route_target(self, ri_fq_name):
        try:
            rtgt = self.get_route_target(ri_fq_name)
            self.remove_route_target(ri_fq_name)
        except NotFoundException:
            pass
        self._rt_allocator.delete(rtgt)

    # TODO(ethuleau): We keep the virtual network and security group ID
    #                 allocation in schema and in the vnc API for one
    #                 release overlap to prevent any upgrade issue. So the
    #                 following code need to be remove in release (3.2 + 1)
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

    def get_bgpaas_port(self, port):
        return self._bgpaas_allocator.read(port)

    def alloc_bgpaas_port(self, name):
        return self._bgpaas_port_allocator.alloc(name)

    def free_bgpaas_port(self, port):
        self._bgpaas_port_allocator.delete(port)
