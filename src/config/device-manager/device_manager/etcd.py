# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains an etcd and api server backed implementation of data
model for physical router configuration manager.
"""

import jsonpickle

from cfgm_common.exceptions import ResourceExistsError, VncError
from cfgm_common.vnc_etcd import etcd_args as get_etcd_args
from cfgm_common.vnc_object_db import VncObjectEtcdClient
from db import (DeviceManagerDBMixin, PhysicalInterfaceDM, PortTupleDM,
                ServiceInstanceDM, VirtualMachineInterfaceDM)
from dm_utils import DMUtils
from vnc_api.exceptions import RefsExistError


class DMEtcdDB(VncObjectEtcdClient, DeviceManagerDBMixin):

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

    def get_pnf_resources(self, vmi_obj, pr_id):
        """Allocate and store PNF resources (network, vlan and unit ids)."""
        pass

    def delete_pnf_resources(self, si_id):
        """Deallocate PNF resources (network, vlan and unit ids) and remove
        them from etcd storage.
        """
        pass

    def handle_pnf_resource_deletes(self, si_id_list):
        """Delete PNF resources unless si_id exists in si_id_list."""
        pass

    def init_pr_map(self):
        pass

    def init_pr_dci_map(self):
        pass

    def init_pr_ae_map(self):
        pass

    def init_pr_asn_map(self):
        pass

    def init_dci_asn_map(self):
        pass

    def get_ip(self, key, ip_used_for):
        pass

    def get_ae_id(self, key):
        pass

    def get_dci_ip(self, key):
        pass

    def add_ip(self, key, ip_used_for, ip):
        pass

    def add_dci_ip(self, key, ip):
        pass

    def add_ae_id(self, pr_uuid, esi, ae_id):
        pass

    def add_asn(self, pr_uuid, asn):
        pass

    def add_dci_asn(self, dci_uuid, asn):
        pass

    def delete_ip(self, key, ip_used_for):
        pass

    def delete_dci_ip(self, key):
        pass

    def delete_ae_id(self, pr_uuid, esi):
        pass

    def delete_pr(self, pr_uuid):
        pass

    def delete_dci(self, dci_uuid):
        pass

    def handle_dci_deletes(self, current_dci_set):
        pass

    def handle_pr_deletes(self, current_pr_set):
        pass

    def get_pr_vn_set(self, pr_uuid):
        pass

    def get_pr_dci_set(self, pr_uuid):
        pass

    def get_pr_ae_id_map(self, pr_uuid):
        pass

    @classmethod
    def get_db_info(cls):
        pass

# end
