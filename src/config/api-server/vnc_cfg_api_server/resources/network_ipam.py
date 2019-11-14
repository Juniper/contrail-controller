#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from builtins import str

from cfgm_common.exceptions import NoIdError
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from vnc_api.gen.resource_common import NetworkIpam

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class NetworkIpamServer(ResourceMixin, NetworkIpam):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):

        subnet_method = obj_dict.get('ipam_subnet_method',
                                     'user-defined-subnet')
        ipam_subnets = obj_dict.get('ipam_subnets')
        if ipam_subnets is not None and subnet_method != 'flat-subnet':
            msg = "ipam-subnets are allowed only with flat-subnet"
            return False, (400, msg)

        ipam_subnetting = obj_dict.get('ipam_subnetting', False)
        if ipam_subnetting and subnet_method != 'flat-subnet':
            msg = "subnetting is allowed only with flat-subnet"
            return False, (400, msg)

        if subnet_method != 'flat-subnet':
            return True, ""

        ipam_subnets = obj_dict.get('ipam_subnets')
        if ipam_subnets is None:
            return True, ""

        ipam_subnets_list = cls.addr_mgmt._ipam_to_subnets(obj_dict)
        if not ipam_subnets_list:
            ipam_subnets_list = []

        ok, result = cls.addr_mgmt.net_check_subnet_overlap(ipam_subnets_list)
        if not ok:
            return (ok, (400, result))

        subnets = ipam_subnets.get('subnets') or []
        (ok, result) = cls.addr_mgmt.net_check_subnet(subnets)
        if not ok:
            return (ok, (409, result))

        try:
            cls.addr_mgmt.ipam_create_req(obj_dict)

            def undo():
                cls.addr_mgmt.ipam_delete_req(obj_dict)
                return True, ""
            get_context().push_undo(undo)
        except Exception as e:
            return (False, (500, str(e)))

        return True, ""

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, read_result = cls.dbe_read(db_conn, 'network_ipam', id)
        if not ok:
            return ok, read_result

        def ipam_mgmt_check():
            old_ipam_mgmt = read_result.get('network_ipam_mgmt')
            new_ipam_mgmt = obj_dict.get('network_ipam_mgmt')
            if not old_ipam_mgmt or not new_ipam_mgmt:
                return True, ""

            old_dns_method = old_ipam_mgmt.get('ipam_dns_method')
            new_dns_method = new_ipam_mgmt.get('ipam_dns_method')
            if not cls.is_change_allowed(old_dns_method, new_dns_method,
                                         read_result, db_conn):
                msg = ("Cannot change DNS Method  with active VMs referring "
                       "to the IPAM")
                return False, (400, msg)
            return True, ""

        ok, result = ipam_mgmt_check()
        if not ok:
            return ok, result

        old_subnet_method = read_result.get('ipam_subnet_method')
        if 'ipam_subnet_method' in obj_dict:
            new_subnet_method = obj_dict.get('ipam_subnet_method')
            if (old_subnet_method != new_subnet_method):
                return (False, (400, 'ipam_subnet_method can not be changed'))

        if (old_subnet_method != 'flat-subnet'):
            if 'ipam_subnets' in obj_dict:
                msg = "ipam-subnets are allowed only with flat-subnet"
                return False, (400, msg)
            return True, ""

        old_subnetting = read_result.get('ipam_subnetting')
        if 'ipam_subnetting' in obj_dict:
            subnetting = obj_dict.get('ipam_subnetting', False)
            if (old_subnetting != subnetting):
                return (False, (400, 'ipam_subnetting can not be changed'))

        if 'ipam_subnets' in obj_dict:
            req_subnets_list = cls.addr_mgmt._ipam_to_subnets(obj_dict)

            # First check the overlap condition within ipam_subnets
            ok, result = cls.addr_mgmt.net_check_subnet_overlap(
                req_subnets_list)
            if not ok:
                return (ok, (400, result))

            # if subnets are modified then make sure new subnet lists are
            # not in overlap conditions with VNs subnets and other ipams
            # referred by all VNs referring this ipam
            vn_refs = read_result.get('virtual_network_back_refs', [])
            ref_ipam_uuid_list = []
            refs_subnets_list = []
            for ref in vn_refs:
                vn_id = ref.get('uuid')
                try:
                    (ok, vn_dict) = db_conn.dbe_read('virtual_network', vn_id)
                except NoIdError:
                    continue
                if not ok:
                    return False, vn_dict
                # get existing subnets on this VN and on other ipams
                # this VN refers and run a overlap check.
                ipam_refs = vn_dict.get('network_ipam_refs', [])
                for ipam in ipam_refs:
                    ref_ipam_uuid = ipam['uuid']
                    if ref_ipam_uuid == id:
                        # This is a ipam for which update request has come
                        continue

                    if ref_ipam_uuid in ref_ipam_uuid_list:
                        continue

                    # check if ipam is a flat-subnet, for flat-subnet ipam
                    # add uuid in ref_ipam_uuid_list, to read ipam later
                    # to get current ipam_subnets from ipam
                    vnsn_data = ipam.get('attr') or {}
                    ref_ipam_subnets = vnsn_data.get('ipam_subnets') or []

                    if len(ref_ipam_subnets) == 1:
                        # flat subnet ipam will have only one entry in
                        # vn->ipam link without any ip_prefix
                        ref_ipam_subnet = ref_ipam_subnets[0]
                        ref_subnet = ref_ipam_subnet.get('subnet') or {}
                        if 'ip_prefix' not in ref_subnet:
                            # This is a flat-subnet,
                            ref_ipam_uuid_list.append(ref_ipam_uuid)

                # vn->ipam link to the refs_subnets_list
                vn_subnets_list = cls.addr_mgmt._vn_to_subnets(vn_dict)
                if vn_subnets_list:
                    refs_subnets_list += vn_subnets_list

            for ipam_uuid in ref_ipam_uuid_list:
                (ok, ipam_dict) = cls.dbe_read(db_conn, 'network_ipam',
                                               ipam_uuid)
                if not ok:
                    return (ok, 409, ipam_dict)
                ref_subnets_list = cls.addr_mgmt._ipam_to_subnets(ipam_dict)
                refs_subnets_list += ref_subnets_list

            (ok, result) = cls.addr_mgmt.check_overlap_with_refs(
                refs_subnets_list, req_subnets_list)
            if not ok:
                return (ok, (400, result))

        ipam_subnets = obj_dict.get('ipam_subnets')
        if ipam_subnets is not None:
            subnets = ipam_subnets.get('subnets') or []
            (ok, result) = cls.addr_mgmt.net_check_subnet(subnets)
            if not ok:
                return (ok, (409, result))

        (ok, result) = cls.addr_mgmt.ipam_check_subnet_delete(read_result,
                                                              obj_dict)
        if not ok:
            return (ok, (409, result))

        (ok, result) = cls.addr_mgmt.ipam_validate_subnet_update(read_result,
                                                                 obj_dict)
        if not ok:
            return (ok, (400, result))

        try:
            cls.addr_mgmt.ipam_update_req(fq_name, read_result, obj_dict, id)

            def undo():
                # failed => update with flipped values for db_dict and req_dict
                cls.addr_mgmt.ipam_update_req(
                    fq_name, obj_dict, read_result, id)
            get_context().push_undo(undo)
        except Exception as e:
            return (False, (500, str(e)))

        return True, ""

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        ok, read_result = cls.dbe_read(db_conn, 'network_ipam', id)
        if not ok:
            return ok, read_result, None

        subnet_method = read_result.get('ipam_subnet_method')
        if subnet_method is None or subnet_method != 'flat-subnet':
            return True, "", None

        ipam_subnets = read_result.get('ipam_subnets')
        if ipam_subnets is None:
            return True, "", None

        cls.addr_mgmt.ipam_delete_req(obj_dict)

        def undo():
            cls.addr_mgmt.ipam_create_req(obj_dict)
        get_context().push_undo(undo)
        return True, "", None

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        cls.addr_mgmt.ipam_create_notify(obj_dict)

        return True, ''

    @classmethod
    def dbe_update_notification(cls, obj_id, extra_dict=None):
        return cls.addr_mgmt.ipam_update_notify(obj_id)

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        cls.addr_mgmt.ipam_delete_notify(obj_id, obj_dict)

        return True, ''

    @classmethod
    def is_change_allowed(cls, old, new, obj_dict, db_conn):
        if cls.is_active_vm_present(obj_dict, db_conn):
            if old in ["default-dns-server", "virtual-dns-server"]:
                if new == "tenant-dns-server" or new is None:
                    return False
            if old == "tenant-dns-server" and new != old:
                return False
            if old is None and new != old:
                return False
        return True

    @classmethod
    def is_active_vm_present(cls, obj_dict, db_conn):
        for vn in obj_dict.get('virtual_network_back_refs') or []:
            ok, result = cls.dbe_read(
                db_conn,
                'virtual_network',
                vn['uuid'],
                obj_fields=['virtual_machine_interface_back_refs'])
            if not ok:
                code, msg = result
                if code == 404:
                    continue
                db_conn.config_log('Error in active vm check %s' % result,
                                   level=SandeshLevel.SYS_ERR)
                # Cannot determine, err on side of caution
                return True
            if result.get('virtual_machine_interface_back_refs'):
                return True
        return False
