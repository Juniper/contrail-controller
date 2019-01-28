# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains an etcd and api server backed implementation of data
model for physical router configuration manager.
"""

import collections
import jsonpickle

from cfgm_common.vnc_etcd import etcd_args as get_etcd_args
from cfgm_common.vnc_object_db import VncObjectEtcdClient
from db import (DeviceManagerDBMixin, PortTupleDM,
                ServiceInstanceDM, VirtualMachineInterfaceDM)
from dm_utils import DMUtils
from vnc_api.exceptions import RefsExistError

from gevent import monkey
monkey.patch_all()

import grpc.experimental.gevent as grpc_gevent
grpc_gevent.init_gevent()


class DMEtcdDB(VncObjectEtcdClient, DeviceManagerDBMixin):
    """Etcd database backend for Device Manager."""

    _ETCD_DEVICE_MANAGER_PREFIX = 'device_manager'
    _ETCD_PNF_RESOURCES_PATH = 'pnf_resource'
    _ETCD_PR_VN_IP_TABLE_KEY = 'pr_vn_ip_table'
    _ETCD_PR_DCI_IP_TABLE_KEY = 'pr_dci_ip_table'
    _ETCD_PR_AE_IP_TABLE_KEY = 'pr_ae_id_table'
    _ETCD_PR_ASN_TABLE_KEY = 'pr_asn_table'
    _ETCD_DCI_ASN_TABLE_KEY = 'dci_asn_table'

    # PNF (Physical Network Function)
    _INT_POOL_PNF_NETWORK_ID = 'pnf_network_id'
    _PNF_MAX_NETWORK_ID = 2147483644
    _INT_POOL_PNF_VLAN_PREFIX = 'pnf_vlan_id'
    _INT_POOL_PNF_PHYSICAL_ROUTER_PREFIX = 'pnf_pr_'
    _PNF_MAX_VLAN = 4093
    _INT_POOL_PNF_PHYSICAL_INTERFACE_PREFIX = 'pnf_pi_'
    _PNF_MAX_UNIT = 16385

    dm_object_db_instance = None

    @classmethod
    def get_instance(cls, args, vnc_lib, logger=None):
        if cls.dm_object_db_instance is None:
            cls.dm_object_db_instance = DMEtcdDB(
                args, vnc_lib, logger)
        return cls.dm_object_db_instance

    @classmethod
    def clear_instance(cls):
        cls.dm_object_db_instance = None
    # end

    def __init__(self, args, vnc_lib, logger=None):
        logger_log = None
        etcd_args = get_etcd_args(args)
        if logger:
            logger.log(
                "VncObjectEtcdClient arguments. {}".format(etcd_args))
            logger_log = logger.log
        super(DMEtcdDB, self).__init__(logger=logger_log, **etcd_args)
        DeviceManagerDBMixin.__init__(self)

        self._vnc_lib = vnc_lib
        self._logger = logger
        self._args = args
        self._init_api_int_pool(
            self._INT_POOL_PNF_NETWORK_ID,
            self._args.pnf_network_start,
            self._args.pnf_network_end,
            self._PNF_MAX_NETWORK_ID)
        self.pnf_resources_map = dict(self._object_db.list_kv(self._etcd_path_key(self._ETCD_PNF_RESOURCES_PATH)))
    # end __init__

    def _init_api_int_pool(self, int_pool_name, range_from, range_to, range_max):
        """Make API server create an int_pool."""
        if not range_from or not range_to:
            range_from = 1
            range_to = range_from + range_max
        try:
            self._vnc_lib.create_int_pool(int_pool_name, range_from, range_to)
        except RefsExistError:
            # int pool already allocated
            pass
    # end _init_api_int_pool

    def _etcd_path_key(self, path, key=None):
        """Make an etcd key (or path) for device manager storage."""
        if key:
            return '%s/%s/%s' % (self._ETCD_DEVICE_MANAGER_PREFIX, path, key)
        else:
            return '%s/%s' % (self._ETCD_DEVICE_MANAGER_PREFIX, path)
    # end _etcd_path_key

    def _pnf_pi_int_pool_name(self, pi_id):
        """Make int_pool name for storing pnf vlans for specified physical interface."""
        return '%s_%s' % (self._INT_POOL_PNF_PHYSICAL_INTERFACE_PREFIX, pi_id)
    # end _pnf_pi_int_pool_name

    def _pnf_pr_int_pool_name(self, pr_id):
        """Make int_pool name for storing pnf vlans for specified physical router."""
        return '%s_%s' % (self._INT_POOL_PNF_PHYSICAL_ROUTER_PREFIX, pr_id)
    # end _pnf_pr_int_pool_name

    def _pnf_vlan_int_pool(self, pr_id):
        """Prepare int_pool for vlans in specified physical router."""
        int_pool_name = self._pnf_pr_int_pool_name(pr_id)
        self._init_api_int_pool(int_pool_name,
                                self._args.pnf_vlan_start,
                                self._args.pnf_vlan_end,
                                self._PNF_MAX_VLAN)
        return int_pool_name
    # end _pnf_vlan_int_pool

    def _pnf_unit_int_pool(self, pi_id):
        """Prepare int_pool for units in specified physical interface."""
        int_pool_name = self._pnf_pi_int_pool_name(pi_id)
        self._init_api_int_pool(int_pool_name,
                                self._args.pnf_unit_start,
                                self._args.pnf_unit_end,
                                self._PNF_MAX_UNIT)
        return int_pool_name
    # end _pnf_unit_int_pool

    def _pnf_resources_path(self, si_id):
        """Make etcd key path for pnf resources with si_id."""
        return '%s/%s/%s' % (self._ETCD_DEVICE_MANAGER_PREFIX,
                             self._ETCD_PNF_RESOURCES_PATH,
                             si_id,)
    # end _pnf_resources_path

    def get_one_entry(self, path, key, column):
        """Get an entry from a serialized dict in etcd."""
        value = self._object_db.get_value(
            self._etcd_path_key(path, key))
        if not value:
            return None
        try:
            result = jsonpickle.decode(value)
        except (TypeError, ValueError):
            result = value
        if column and isinstance(result, dict):
            if column in result.keys():
                return result[column]
        return result
    # end get_one_entry

    def add(self, path, key, value):
        """Store a key-value pair in etcd."""
        try:
            self._object_db.put_kv(
                self._etcd_path_key(path, key), value)
            return True
        except:
            return False
    # end add

    def delete(self, path, key, columns=None):
        """Delete etcd key or its parts.

        If columns are not defined delete an etcd key.  Otherwise
        treat retrieved (from etcd) value as a dict and remove its keys.
        """
        etcd_key = self._etcd_path_key(path, key)
        # simply delete a key
        if not columns:
            self._object_db.delete_kv(etcd_key)
            return True
        # delete some fields in dict stored under etcd_key
        value = self._object_db.get_value(etcd_key)
        try:
            entries = jsonpickle.decode(value)
        except:
            return False
        for c in columns:
            try:
                del entries[c]
            except KeyError:
                pass
        if entries:
            # entries not empty after deletion
            self._object_db.put_kv(
                etcd_key, jsonpickle.encode(entries))
        else:
            # nothing left in entries
            self._object_db.delete_kv(etcd_key)
        return True
    # end delete

    def get_pnf_vlan_allocator(self, pr_id):
        return self.pnf_vlan_allocator_map.setdefault(
            pr_id,
            self._pnf_vlan_int_pool(pr_id)
        )
    # end get_pnf_vlan_allocator

    def get_pnf_unit_allocator(self, pi_id):
        return self.pnf_unit_allocator_map.setdefault(
            pi_id,
            self._pnf_unit_int_pool(pi_id)
        )
    # end get_pnf_unit_allocator

    def get_pnf_resources(self, vmi_obj, pr_id):
        """Allocate and store PNF resources (network, vlan and unit ids)."""
        si_id = vmi_obj.service_instance
        pi_id = vmi_obj.physical_interface
        if not si_id or not pi_id:
            return None
        if si_id in self.pnf_resources_map:
            return self.pnf_resources_map[si_id]

        # TODO maybe si_id is not used after all ?
        # allocate network_id
        network_id = self._vnc_lib.allocate_int(self._INT_POOL_PNF_NETWORK_ID)

        # allocate vlan_id and set them to specific physical routers' configs
        vlan_alloc = self.get_pnf_vlan_allocator(pr_id)
        self._vnc_lib.set_int(vlan_alloc, 0)
        vlan_id = self._vnc_lib.allocate_int(self._pnf_vlan_int_pool(pr_id))
        pr_set = self.get_si_pr_set(si_id)
        for other_pr_uuid in pr_set:
            if other_pr_uuid != pr_id:
                self._vnc_lib.set_int(
                    self._pnf_vlan_int_pool(other_pr_uuid), vlan_id)

        # allocate unit_id and set it in specific physical interface's config
        unit_id = self._vnc_lib.allocate_int(self._pnf_unit_int_pool(pi_id))

        # prepare and store pnf_resource
        pnf_resources = {
            'network_id': str(network_id),
            'vlan_id': str(vlan_id),
            'unit_id': str(unit_id)
        }
        self.pnf_resources_map[si_id] = pnf_resources
        self._object_db.put_kv(
            self._pnf_resources_path(si_id),
            jsonpickle.encode(pnf_resources))
        return pnf_resources
    # end get_pnf_resources

    def fetch_pnf_resources(self, si_id):
        """Fetch pnf_resources stored in etcd."""
        value = self._object_db.get_value(
            self._pnf_resources_path(si_id))
        if isinstance(value, basestring) or isinstance(value, buffer):
            return jsonpickle.decode(value)
        elif isinstance(value, collections.Mapping):
            return value
        return {}
    # end fetch_pnf_resources

    def delete_pnf_resources(self, si_id):
        """Deallocate PNF resources (network, vlan and unit ids) and remove
        them from etcd storage.
        """
        pnf_resources = self.pnf_resources_map.get(si_id, None)
        if not pnf_resources:
            pnf_resources = self.fetch_pnf_resources(si_id)
        if not pnf_resources:
            return

        # deallocate network_id
        self._vnc_lib.deallocate_int(
            self._INT_POOL_PNF_NETWORK_ID, int(pnf_resources['network_id']))

        # deallocate vlan_ids and unset them in specific physical routers' configs
        pr_set = self.get_si_pr_set(si_id)
        for pr_uuid in pr_set:
            self._vnc_lib.deallocate_int(
                self._pnf_vlan_int_pool(pr_uuid), int(pnf_resources['vlan_id']))

        # deallocate unit_id and unset it in specifoc physical interface's config
        si_obj = ServiceInstanceDM.get(si_id)
        for pt_uuid in si_obj.port_tuples:
            pt_obj = PortTupleDM.get(pt_uuid)
            for vmi_uuid in pt_obj.virtual_machine_interfaces:
                vmi_obj = VirtualMachineInterfaceDM.get(vmi_uuid)
                if vmi_obj.physical_interface:
                    self._vnc_lib.deallocate_int(
                        self._pnf_unit_int_pool(vmi_obj.physical_interface),
                        int(pnf_resources['unit_id']))

        # remove local dict and etcd resource
        del self.pnf_resources_map[si_id]
        self._object_db.delete_kv(self._pnf_resources_path(si_id))
    # end delete_pnf_resources

    def handle_pnf_resource_deletes(self, si_id_list):
        """Delete PNF resources unless si_id exists in si_id_list."""
        for si_id in self.pnf_resources_map.keys():
            if si_id not in si_id_list:
                self.delete_pnf_resources(si_id)
    # end handle_pnf_resource_deletes

    def init_pr_map(self):
        pr_entries = self._object_db.list_kv(
            self._etcd_path_key(self._ETCD_PR_VN_IP_TABLE_KEY))
        self.populate_pr_map(dict(pr_entries))
    # end init_pr_map

    def init_pr_dci_map(self):
        pr_entries = self._object_db.list_kv(
            self._etcd_path_key(self._ETCD_PR_DCI_IP_TABLE_KEY))
        self.populate_pr_dci_map(dict(pr_entries))
    # end init_pr_dci_map

    def init_pr_ae_map(self):
        pr_entries = self._object_db.list_kv(
            self._etcd_path_key(self._ETCD_PR_AE_IP_TABLE_KEY))
        self.populate_pr_ae_map(dict(pr_entries))
    # end init_pr_ae_map

    def init_pr_asn_map(self):
        pr_entries = self._object_db.list_kv(
            self._etcd_path_key(self._ETCD_PR_ASN_TABLE_KEY))
        self.populate_pr_asn_map(dict(pr_entries))
    # end init_pr_asn_map

    def init_dci_asn_map(self):
        dci_entries = self._object_db.list_kv(
            self._etcd_path_key(self._ETCD_DCI_ASN_TABLE_KEY))
        self.populate_dci_asn_map(dict(dci_entries))
    # end init_dci_asn_map

    def get_ip(self, key, ip_used_for):
        return self.get_one_entry(self._ETCD_PR_VN_IP_TABLE_KEY, key,
                                  DMUtils.get_ip_cs_column_name(ip_used_for))
    # end get_ip

    def get_ae_id(self, key):
        return self.get_one_entry(self._ETCD_PR_AE_IP_TABLE_KEY, key, "index")
    # end get_ae_id

    def get_dci_ip(self, key):
        return self.get_one_entry(self._ETCD_PR_DCI_IP_TABLE_KEY, key, "ip")
    # end get_dci_ip

    def add_ip(self, key, ip_used_for, ip):
        self.add(self._ETCD_PR_VN_IP_TABLE_KEY, key,
                 {DMUtils.get_ip_cs_column_name(ip_used_for): ip})
    # end add_ip

    def add_dci_ip(self, key, ip):
        self.add(self._ETCD_PR_DCI_IP_TABLE_KEY, key, {'ip': ip})
    # end add_dci_ip

    def add_ae_id(self, pr_uuid, esi, ae_id):
        key = pr_uuid + ':' + esi
        self.add(self._ETCD_PR_AE_IP_TABLE_KEY, key, {'index': ae_id})
        super(DMEtcdDB, self).add_ae_id(pr_uuid, esi, ae_id)
    # end add_ae_id

    def add_asn(self, pr_uuid, asn):
        self.add(self._ETCD_PR_ASN_TABLE_KEY, pr_uuid, {'asn': asn})
        super(DMEtcdDB, self).add_asn(pr_uuid, asn)
    # end add_asn

    def add_dci_asn(self, dci_uuid, asn):
        self.add(self._ETCD_DCI_ASN_TABLE_KEY, dci_uuid, {'asn': asn})
        super(DMEtcdDB, self).add_dci_asn(dci_uuid, asn)
    # end add_dci_asn

    def delete_ip(self, key, ip_used_for):
        self.delete(self._ETCD_PR_VN_IP_TABLE_KEY, key,
                    [DMUtils.get_ip_cs_column_name(ip_used_for)])
    # end delete_ip

    def delete_dci_ip(self, key):
        self.delete(self._ETCD_PR_DCI_IP_TABLE_KEY, key)
    # end delete_dci_ip

    def delete_ae_id(self, pr_uuid, esi):
        key = pr_uuid + ':' + esi
        self.delete(self._ETCD_PR_AE_IP_TABLE_KEY, key)
        super(DMEtcdDB, self).delete_ae_id(pr_uuid, esi)
    # end delete_ae_id

    def delete_pr(self, pr_uuid):
        vn_subnet_set = self.get_pr_vn_set(pr_uuid)
        for vn_subnet_ip_used_for in vn_subnet_set:
            vn_subnet = vn_subnet_ip_used_for[0]
            ip_used_for = vn_subnet_ip_used_for[1]
            ret = self.delte(self._ETCD_PR_VN_IP_TABLE_KEY, pr_uuid + ':' + vn_subnet,
                             [DMUtils.get_ip_cs_column_name(ip_used_for)])
            if ret == False:
                self._logger.error("Unable to free ip from db for vn/pr/subnet/ip_used_for "
                                   "(%s/%s/%s)" % (pr_uuid, vn_subnet, ip_used_for))
        esi_map = self.get_pr_ae_id_map(pr_uuid)
        for esi, _ in esi_map.values():
            ret = self.delete(self._ETCD_PR_AE_IP_TABLE_KEY,
                              pr_uuid + ':' + esi)
            if ret == False:
                self._logger.error("Unable to free ae id from db for pr/esi"
                                   "(%s/%s)" % (pr_uuid, esi))
        asn = self.pr_asn_map.pop(pr_uuid, None)
        if asn is not None:
            self.asn_pr_map.pop(asn, None)
            ret = self.delete(self._ETCD_PR_ASN_TABLE_KEY, pr_uuid)
            if not ret:
                self._logger.error("Unable to free asn from db for pr %s" %
                                   pr_uuid)
    # end delete_pr

    def delete_dci(self, dci_uuid):
        asn = self.dci_asn_map.pop(dci_uuid, None)
        if asn is not None:
            self.asn_dci_map.pop(asn, None)
            ret = self.delete(self._ETCD_DCI_ASN_TABLE_KEY, dci_uuid)
            if not ret:
                self._logger.error("Unable to free dci asn from db for dci %s" %
                                   dci_uuid)
    # end delete_dci

    def handle_dci_deletes(self, current_dci_set):
        cs_dci_set = set(self.dci_asn_map.keys())
        delete_set = cs_dci_set.difference(current_dci_set)
        for dci_uuid in delete_set:
            self.delete_dci(dci_uuid)
    # end handle_dci_deletes

    def handle_pr_deletes(self, current_pr_set):
        cs_pr_set = set(self.pr_vn_ip_map.keys())
        delete_set = cs_pr_set.difference(current_pr_set)
        for pr_uuid in delete_set:
            self.delete_pr(pr_uuid)
    # end hendle_pr_deletes

    @classmethod
    def get_db_info(cls):
        db_info = [(cls._ETCD_DEVICE_MANAGER_PREFIX,
                    [cls._ETCD_PR_VN_IP_TABLE_KEY])]
        return db_info
    # end get_db_info

# end DMEtcdDB
