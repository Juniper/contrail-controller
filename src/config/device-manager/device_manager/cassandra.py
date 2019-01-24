#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of data model for physical router
configuration manager
"""

from cfgm_common.exceptions import ResourceExistsError
from cfgm_common.vnc_object_db import VncObjectDBClient
from cfgm_common.zkclient import IndexAllocator
from db import (DeviceMapperDBMixin, PhysicalInterfaceDM, PortTupleDM,
                ServiceInstanceDM, VirtualMachineInterfaceDM)
from dm_utils import DMUtils
from sandesh_common.vns.constants import DEVICE_MANAGER_KEYSPACE_NAME


class DMCassandraDB(VncObjectDBClient, DeviceMapperDBMixin):
    _KEYSPACE = DEVICE_MANAGER_KEYSPACE_NAME
    _PR_VN_IP_CF = 'dm_pr_vn_ip_table'
    _PR_AE_ID_CF = 'dm_pr_ae_id_table'
    _PR_ASN_CF = 'dm_pr_asn_table'
    _DCI_ASN_CF = 'dm_dci_asn_table'
    _PR_DCI_IP_CF = 'dm_pr_dci_ip_table'
    # PNF table
    _PNF_RESOURCE_CF = 'dm_pnf_resource_table'

    _zk_path_pfx = ''

    _PNF_MAX_NETWORK_ID = 4294967292
    _PNF_NETWORK_ALLOC_PATH = "/id/pnf/network_id"

    _PNF_MAX_VLAN = 4093
    _PNF_VLAN_ALLOC_PATH = "/id/pnf/vlan_id"

    _PNF_MAX_UNIT = 16385
    _PNF_UNIT_ALLOC_PATH = "/id/pnf/unit_id"

    dm_object_db_instance = None

    @classmethod
    def get_instance(cls, manager=None, zkclient=None):
        if cls.dm_object_db_instance is None:
            cls.dm_object_db_instance = DMCassandraDB(manager, zkclient)
        return cls.dm_object_db_instance
    # end

    @classmethod
    def clear_instance(cls):
        cls.dm_object_db_instance = None
    # end

    def __init__(self, manager, zkclient):
        self._zkclient = zkclient
        self._manager = manager
        self._args = manager._args

        keyspaces = {
            self._KEYSPACE: {self._PR_VN_IP_CF: {},
                             self._PR_AE_ID_CF: {},
                             self._PR_ASN_CF: {},
                             self._DCI_ASN_CF: {},
                             self._PR_DCI_IP_CF: {},
                             self._PNF_RESOURCE_CF: {}}}

        cass_server_list = self._args.cassandra_server_list
        cred = None
        if (self._args.cassandra_user is not None and
                self._args.cassandra_password is not None):
            cred = {'username': self._args.cassandra_user,
                    'password': self._args.cassandra_password}

        super(DMCassandraDB, self).__init__(
            cass_server_list, self._args.cluster_id, keyspaces, None,
            manager.logger.log, credential=cred,
            ssl_enabled=self._args.cassandra_use_ssl,
            ca_certs=self._args.cassandra_ca_certs)
        DeviceMapperDBMixin.__init__(self)

        self.pnf_vlan_allocator_map = {}
        self.pnf_unit_allocator_map = {}
        self.pnf_network_allocator = IndexAllocator(
            zkclient, self._zk_path_pfx+self._PNF_NETWORK_ALLOC_PATH,
            self._PNF_MAX_NETWORK_ID)

        self.pnf_cf = self.get_cf(self._PNF_RESOURCE_CF)
        self.pnf_resources_map = dict(
            self.pnf_cf.get_range(column_count=0, filter_empty=True))
    # end

    def get_si_pr_set(self, si_id):
        si_obj = ServiceInstanceDM.get(si_id)
        pr_set = set()
        for pt_uuid in si_obj.port_tuples:
            pt_obj = PortTupleDM.get(pt_uuid)
            for vmi_uuid in pt_obj.virtual_machine_interfaces:
                vmi_obj = VirtualMachineInterfaceDM.get(vmi_uuid)
                pi_obj = PhysicalInterfaceDM.get(vmi_obj.physical_interface)
                pr_set.add(pi_obj.physical_router)
        return pr_set

    def get_pnf_vlan_allocator(self, pr_id):
        return self.pnf_vlan_allocator_map.setdefault(
            pr_id,
            IndexAllocator(
                self._zkclient,
                self._zk_path_pfx+self._PNF_VLAN_ALLOC_PATH+pr_id+'/',
                self._PNF_MAX_VLAN)
        )

    def get_pnf_unit_allocator(self, pi_id):
        return self.pnf_unit_allocator_map.setdefault(
            pi_id,
            IndexAllocator(
                self._zkclient,
                self._zk_path_pfx+self._PNF_UNIT_ALLOC_PATH+pi_id+'/',
                self._PNF_MAX_UNIT)
        )

    def get_pnf_resources(self, vmi_obj, pr_id):
        si_id = vmi_obj.service_instance
        pi_id = vmi_obj.physical_interface
        if not si_id or not pi_id:
            return None
        if si_id in self.pnf_resources_map:
            return self.pnf_resources_map[si_id]

        network_id = self.pnf_network_allocator.alloc(si_id)
        vlan_alloc = self.get_pnf_vlan_allocator(pr_id)
        try:
            vlan_alloc.reserve(0)
        except ResourceExistsError:
            # must have been reserved already, restart case
            pass
        vlan_id = vlan_alloc.alloc(si_id)
        pr_set = self.get_si_pr_set(si_id)
        for other_pr_uuid in pr_set:
            if other_pr_uuid != pr_id:
                try:
                    self.get_pnf_vlan_allocator(other_pr_uuid).reserve(vlan_id)
                except ResourceExistsError:
                    pass
        unit_alloc = self.get_pnf_unit_allocator(pi_id)
        try:
            unit_alloc.reserve(0)
        except ResourceExistsError:
            # must have been reserved already, restart case
            pass
        unit_id = unit_alloc.alloc(si_id)
        pnf_resources = {
            "network_id": str(network_id),
            "vlan_id": str(vlan_id),
            "unit_id": str(unit_id)
        }
        self.pnf_resources_map[si_id] = pnf_resources
        self.pnf_cf.insert(si_id, pnf_resources)
        return pnf_resources
    # end

    def delete_pnf_resources(self, si_id):
        pnf_resources = self.pnf_resources_map.get(si_id, None)
        if not pnf_resources:
            return
        self.pnf_network_allocator.delete(int(pnf_resources['network_id']))

        pr_set = self.get_si_pr_set(si_id)
        for pr_uuid in pr_set:
            if pr_uuid in self.pnf_vlan_allocator_map:
                self.get_pnf_vlan_allocator(pr_uuid).delete(
                    int(pnf_resources['vlan_id']))

        si_obj = ServiceInstanceDM.get(si_id)
        for pt_uuid in si_obj.port_tuples:
            pt_obj = PortTupleDM.get(pt_uuid)
            for vmi_uuid in pt_obj.virtual_machine_interfaces:
                vmi_obj = VirtualMachineInterfaceDM.get(vmi_uuid)
                if vmi_obj.physical_interface:
                    self.get_pnf_unit_allocator(vmi_obj.physical_interface).delete(
                        int(pnf_resources['unit_id']))

        del self.pnf_resources_map[si_id]
        self.pnf_cf.remove(si_id)
    # end

    def handle_pnf_resource_deletes(self, si_id_list):
        for si_id in self.pnf_resources_map:
            if si_id not in si_id_list:
                self.delete_pnf_resources(si_id)
    # end

    def init_pr_map(self):
        cf = self.get_cf(self._PR_VN_IP_CF)
        pr_entries = dict(cf.get_range(column_count=1000000))
        self.populate_pr_map(pr_entries)
    # end

    def init_pr_dci_map(self):
        cf = self.get_cf(self._PR_DCI_IP_CF)
        pr_entries = dict(cf.get_range(column_count=1000000))
        self.populate_pr_dci_map(pr_entries)
    # end

    def init_pr_ae_map(self):
        cf = self.get_cf(self._PR_AE_ID_CF)
        pr_entries = dict(cf.get_range(column_count=1000000))
        self.populate_pr_ae_map(pr_entries)
    # end

    def init_pr_asn_map(self):
        cf = self.get_cf(self._PR_ASN_CF)
        pr_entries = dict(cf.get_range())
        self.populate_pr_asn_map(pr_entries)
    # end

    def init_dci_asn_map(self):
        cf = self.get_cf(self._DCI_ASN_CF)
        dci_entries = dict(cf.get_range())
        self.populate_dci_asn_map(dci_entries)
    # end

    def get_ip(self, key, ip_used_for):
        return self.get_one_col(self._PR_VN_IP_CF, key,
                                DMUtils.get_ip_cs_column_name(ip_used_for))
    # end

    def get_ae_id(self, key):
        return self.get_one_col(self._PR_AE_ID_CF, key, "index")
    # end

    def get_dci_ip(self, key):
        return self.get_one_col(self._PR_DCI_IP_CF, key, "ip")
    # end

    def add_ip(self, key, ip_used_for, ip):
        self.add(self._PR_VN_IP_CF, key, {
                 DMUtils.get_ip_cs_column_name(ip_used_for): ip})
    # end

    def add_dci_ip(self, key, ip):
        self.add(self._PR_DCI_IP_CF, key, {"ip": ip})
    # end

    def add_ae_id(self, pr_uuid, esi, ae_id):
        key = pr_uuid + ':' + esi
        self.add(self._PR_AE_ID_CF, key, {"index": ae_id})
        super(DMCassandraDB, self).add_ae_id(pr_uuid, esi, ae_id)
    # end

    def add_asn(self, pr_uuid, asn):
        self.add(self._PR_ASN_CF, pr_uuid, {'asn': asn})
        super(DMCassandraDB, self).add_asn(pr_uuid, asn)
    # end add_asn

    def add_dci_asn(self, dci_uuid, asn):
        self.add(self._DCI_ASN_CF, dci_uuid, {'asn': asn})
        super(DMCassandraDB, self).add_dci_asn(dci_uuid, asn)
    # end add_dci_asn

    def delete_ip(self, key, ip_used_for):
        self.delete(self._PR_VN_IP_CF, key, [
                    DMUtils.get_ip_cs_column_name(ip_used_for)])
    # end

    def delete_dci_ip(self, key):
        self.delete(self._PR_DCI_IP_CF, key)
    # end

    def delete_ae_id(self, pr_uuid, esi):
        key = pr_uuid + ':' + esi
        self.delete(self._PR_AE_ID_CF, key)
        super(DMCassandraDB, self).delete_ae_id(pr_uuid, esi)
    # end

    def delete_pr(self, pr_uuid):
        vn_subnet_set = self.pr_vn_ip_map.get(pr_uuid, set())
        for vn_subnet_ip_used_for in vn_subnet_set:
            vn_subnet = vn_subnet_ip_used_for[0]
            ip_used_for = vn_subnet_ip_used_for[1]
            ret = self.delete(self._PR_VN_IP_CF, pr_uuid + ':' + vn_subnet,
                              [DMUtils.get_ip_cs_column_name(ip_used_for)])
            if ret == False:
                self._logger.error("Unable to free ip from db for vn/pr/subnet/ip_used_for "
                                   "(%s/%s/%s)" % (pr_uuid, vn_subnet, ip_used_for))
        esi_map = self.pr_ae_id_map.get(pr_uuid, {})
        for esi, ae_id in esi_map.values():
            ret = self.delete(self._PR_AE_ID_CF, pr_uuid + ':' + esi)
            if ret == False:
                self._logger.error("Unable to free ae id from db for pr/esi"
                                   "(%s/%s)" % (pr_uuid, esi))
        asn = self.pr_asn_map.pop(pr_uuid, None)
        if asn is not None:
            self.asn_pr_map.pop(asn, None)
            ret = self.delete(self._PR_ASN_CF, pr_uuid)
            if not ret:
                self._logger.error("Unable to free asn from db for pr %s" %
                                   pr_uuid)
    # end

    def delete_dci(self, dci_uuid):
        asn = self.dci_asn_map.pop(dci_uuid, None)
        if asn is not None:
            self.asn_dci_map.pop(asn, None)
            ret = self.delete(self._DCI_ASN_CF, dci_uuid)
            if not ret:
                self._logger.error("Unable to free dci asn from db for dci %s" %
                                   dci_uuid)
    # end

    def handle_dci_deletes(self, current_dci_set):
        cs_dci_set = set(self.dci_asn_map.keys())
        delete_set = cs_dci_set.difference(current_dci_set)
        for dci_uuid in delete_set:
            self.delete_dci(dci_uuid)
    # end

    def handle_pr_deletes(self, current_pr_set):
        cs_pr_set = set(self.pr_vn_ip_map.keys())
        delete_set = cs_pr_set.difference(current_pr_set)
        for pr_uuid in delete_set:
            self.delete_pr(pr_uuid)
    # end

    def get_pr_vn_set(self, pr_uuid):
        return self.pr_vn_ip_map.get(pr_uuid, set())
    # end

    def get_pr_dci_set(self, pr_uuid):
        return self.pr_dci_ip_map.get(pr_uuid, set())
    # end

    def get_pr_ae_id_map(self, pr_uuid):
        return self.pr_ae_id_map.get(pr_uuid, {})
    # end

    @classmethod
    def get_db_info(cls):
        db_info = [(cls._KEYSPACE, [cls._PR_VN_IP_CF])]
        return db_info
    # end get_db_info

# end
