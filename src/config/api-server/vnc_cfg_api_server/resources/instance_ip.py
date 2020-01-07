#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from builtins import str

from cfgm_common import IP_FABRIC_VN_FQ_NAME
from cfgm_common import LINK_LOCAL_VN_FQ_NAME
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from vnc_api.gen.resource_common import InstanceIp

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class InstanceIpServer(ResourceMixin, InstanceIp):
    @classmethod
    def _vmi_has_vm_ref(cls, db_conn, iip_dict):
        # is this iip linked to a vmi that is not ref'd by a router
        vmi_refs = iip_dict.get('virtual_machine_interface_refs') or []
        for vmi_ref in vmi_refs or []:
            ok, result = cls.dbe_read(db_conn, 'virtual_machine_interface',
                                      vmi_ref['uuid'],
                                      obj_fields=['virtual_machine_refs'])
            if not ok:
                continue
            if result.get('virtual_machine_refs'):
                return True
        return False

    @classmethod
    def is_gateway_ip(cls, db_conn, iip_uuid):
        ok, iip_dict = cls.dbe_read(db_conn, 'instance_ip', iip_uuid)
        if not ok:
            return False

        try:
            vn_fq_name = iip_dict['virtual_network_refs'][0]['to']
            vn_uuid = iip_dict['virtual_network_refs'][0]['uuid']
            iip_addr = iip_dict['instance_ip_address']
        except Exception:
            return False
        if (vn_fq_name == IP_FABRIC_VN_FQ_NAME or
                vn_fq_name == LINK_LOCAL_VN_FQ_NAME):
            # Ignore ip-fabric and link-local address allocations
            return False

        ok, vn_dict = cls.dbe_read(db_conn, 'virtual_network', vn_uuid,
                                   ['network_ipam_refs'])
        if not ok:
            return False

        return cls.addr_mgmt.is_gateway_ip(vn_dict, iip_addr)

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        virtual_router_refs = obj_dict.get('virtual_router_refs')
        virtual_network_refs = obj_dict.get('virtual_network_refs')
        network_ipam_refs = obj_dict.get('network_ipam_refs')

        if virtual_router_refs and network_ipam_refs:
            msg = "virtual_router_refs and ipam_refs are not allowed"
            return (False, (400, msg))

        if virtual_router_refs and virtual_network_refs:
            msg = "router_refs and network_refs are not allowed"
            return (False, (400, msg))

        if virtual_network_refs:
            vn_fq_name = obj_dict['virtual_network_refs'][0]['to']
            if (vn_fq_name == IP_FABRIC_VN_FQ_NAME or
                    vn_fq_name == LINK_LOCAL_VN_FQ_NAME):
                # Ignore ip-fabric and link-local address allocations
                return True, ""

            vn_id = db_conn.fq_name_to_uuid('virtual_network', vn_fq_name)
            ok, result = cls.dbe_read(db_conn, 'virtual_network', vn_id,
                                      obj_fields=['router_external',
                                                  'network_ipam_refs',
                                                  'address_allocation_mode'])
            if not ok:
                return ok, result

            vn_dict = result
            ipam_refs = None
        else:
            vn_fq_name = vn_id = vn_dict = None
            if virtual_router_refs:
                if len(virtual_router_refs) > 1:
                    msg = "Instance IP cannot refer to multiple vrouters"
                    return False, (400, msg)

                vrouter_uuid = virtual_router_refs[0].get('uuid')
                ok, vrouter_dict = db_conn.dbe_read(obj_type='virtual_router',
                                                    obj_id=vrouter_uuid)
                if not ok:
                    return (ok, (400, obj_dict))
                ipam_refs = vrouter_dict.get('network_ipam_refs') or []
            else:
                ipam_refs = obj_dict.get('network_ipam_refs')

        subnet_uuid = obj_dict.get('subnet_uuid')
        if subnet_uuid and virtual_router_refs:
            msg = "subnet uuid based allocation not supported with vrouter"
            return (False, (400, msg))

        req_ip = obj_dict.get("instance_ip_address")
        req_ip_family = obj_dict.get("instance_ip_family")
        if req_ip_family == "v4":
            req_ip_version = 4
        elif req_ip_family == "v6":
            req_ip_version = 6
        else:
            req_ip_version = None

        # allocation for requested ip from a network_ipam is not supported
        if ipam_refs and req_ip:
            msg = ("allocation for requested IP from a network_ipam is not "
                   "supported")
            return False, (400, msg)

        # if request has ip and not g/w ip, report if already in use.
        # for g/w ip, creation allowed but only can ref to router port.
        if req_ip and cls.addr_mgmt.is_ip_allocated(req_ip, vn_fq_name,
                                                    vn_uuid=vn_id,
                                                    vn_dict=vn_dict):
            if not cls.addr_mgmt.is_gateway_ip(vn_dict, req_ip):
                return False, (409, "IP address already in use")
            elif cls._vmi_has_vm_ref(db_conn, obj_dict):
                return False, (400, "Gateway IP cannot be used by VM port")

        alloc_pool_list = []
        if 'virtual_router_refs' in obj_dict:
            # go over all the ipam_refs and build a list of alloc_pools
            # from where ip is expected
            for vr_ipam in ipam_refs:
                vr_ipam_data = vr_ipam.get('attr', {})
                vr_alloc_pools = vr_ipam_data.get('allocation_pools', [])
                alloc_pool_list.extend(
                    [(vr_alloc_pool) for vr_alloc_pool in vr_alloc_pools])

        subscriber_tag = obj_dict.get('instance_ip_subscriber_tag')
        try:
            if vn_fq_name:
                ok, result = cls.addr_mgmt.get_ip_free_args(vn_fq_name,
                                                            vn_dict)
                if not ok:
                    return ok, result

            (ip_addr, sn_uuid, subnet_name) = cls.addr_mgmt.ip_alloc_req(
                vn_fq_name, vn_dict=vn_dict, sub=subnet_uuid,
                asked_ip_addr=req_ip,
                asked_ip_version=req_ip_version,
                alloc_id=obj_dict['uuid'],
                ipam_refs=ipam_refs,
                alloc_pools=alloc_pool_list,
                iip_subscriber_tag=subscriber_tag)

            def undo():
                msg = ("AddrMgmt: free IIP %s, vn=%s tenant=%s on post fail" %
                       (ip_addr, vn_fq_name, tenant_name))
                db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
                cls.addr_mgmt.ip_free_req(ip_addr, vn_fq_name,
                                          alloc_id=obj_dict['uuid'],
                                          ipam_refs=ipam_refs,
                                          vn_dict=vn_dict,
                                          ipam_dicts=result.get('ipam_dicts'))
                return True, ""
            get_context().push_undo(undo)
        except Exception as e:
            return (False, (400, str(e)))
        obj_dict['instance_ip_address'] = ip_addr
        obj_dict['subnet_uuid'] = sn_uuid
        if subnet_name:
            ip_prefix = subnet_name.split('/')[0]
            prefix_len = int(subnet_name.split('/')[1])
            instance_ip_subnet = {'ip_prefix': ip_prefix,
                                  'ip_prefix_len': prefix_len}
            obj_dict['instance_ip_subnet'] = instance_ip_subnet

        msg = ("AddrMgmt: alloc IIP %s for vn=%s, tenant=%s, askip=%s" %
               (ip_addr, vn_fq_name, tenant_name, req_ip))
        db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
        return True, ""

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn,
                       prop_collection_updates=None, **kwargs):
        # if instance-ip is of g/w ip, it cannot refer to non router port
        req_iip_dict = obj_dict
        ok, result = cls.dbe_read(db_conn, 'instance_ip', id)

        if not ok:
            return ok, result
        db_iip_dict = result

        if 'virtual_network_refs' not in db_iip_dict:
            return True, ''
        vn_uuid = db_iip_dict['virtual_network_refs'][0]['uuid']
        vn_fq_name = db_iip_dict['virtual_network_refs'][0]['to']
        if (vn_fq_name == IP_FABRIC_VN_FQ_NAME or
                vn_fq_name == LINK_LOCAL_VN_FQ_NAME):
            # Ignore ip-fabric and link-local address allocations
            return True, ""

        # instance-ip-address change is not allowed.
        req_ip_addr = obj_dict.get('instance_ip_address')
        db_ip_addr = db_iip_dict.get('instance_ip_address')

        if req_ip_addr and req_ip_addr != db_ip_addr:
            return False, (400, 'Instance IP Address can not be changed')

        ok, result = cls.dbe_read(db_conn, 'virtual_network', vn_uuid,
                                  obj_fields=['network_ipam_refs'])
        if not ok:
            return ok, result

        vn_dict = result
        if cls.addr_mgmt.is_gateway_ip(vn_dict,
                                       db_iip_dict.get('instance_ip_address')):
            if cls._vmi_has_vm_ref(db_conn, req_iip_dict):
                return False, (400, 'Gateway IP cannot be used by VM port')

        return True, ""

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        if 'virtual_network_refs' in obj_dict:
            ok, ip_free_args = cls.addr_mgmt.get_ip_free_args(
                obj_dict['virtual_network_refs'][0]['to'])
            return ok, '', ip_free_args
        else:
            return True, '', None

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn, **kwargs):
        def _get_instance_ip(obj_dict):
            ip_addr = obj_dict.get('instance_ip_address')
            if not ip_addr:
                msg = ("instance_ip_address missing for object %s" %
                       obj_dict['uuid'])
                db_conn.config_log(msg, level=SandeshLevel.SYS_NOTICE)
                return False, ""
            return True, ip_addr

        if 'network_ipam_refs' in obj_dict:
            ipam_refs = obj_dict['network_ipam_refs']
            ok, ip_addr = _get_instance_ip(obj_dict)
            if not ok:
                return True, ""
            cls.addr_mgmt.ip_free_req(ip_addr, None, ipam_refs=ipam_refs)
            return True, ""

        if not obj_dict.get('virtual_network_refs', []):
            return True, ''

        vn_fq_name = obj_dict['virtual_network_refs'][0]['to']
        if (vn_fq_name == IP_FABRIC_VN_FQ_NAME or
                vn_fq_name == LINK_LOCAL_VN_FQ_NAME):
            # Ignore ip-fabric and link-local address allocations
            return True, ""

        ok, ip_addr = _get_instance_ip(obj_dict)
        if not ok:
            return True, ""

        msg = "AddrMgmt: free IIP %s, vn=%s" % (ip_addr, vn_fq_name)
        db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
        cls.addr_mgmt.ip_free_req(ip_addr, vn_fq_name,
                                  alloc_id=obj_dict['uuid'],
                                  vn_dict=kwargs.get('vn_dict'),
                                  ipam_dicts=kwargs.get('ipam_dicts'))

        return True, ""

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        ip_addr = obj_dict['instance_ip_address']
        vn_fq_name = None
        ipam_refs = None
        if obj_dict.get('virtual_network_refs', []):
            vn_fq_name = obj_dict['virtual_network_refs'][0]['to']
        elif obj_dict.get('network_ipam_refs', []):
            ipam_refs = obj_dict['network_ipam_refs']
        else:
            return True, ''
        cls.addr_mgmt.ip_alloc_notify(ip_addr, vn_fq_name, ipam_refs=ipam_refs)

        return True, ''

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        try:
            ip_addr = obj_dict['instance_ip_address']
        except KeyError:
            return True, ''
        vn_fq_name = None
        ipam_refs = None
        if obj_dict.get('virtual_network_refs', []):
            vn_fq_name = obj_dict['virtual_network_refs'][0]['to']
        elif obj_dict.get('virtual_network_refs', []):
            ipam_refs = obj_dict['network_ipam_refs']
        else:
            return True, ''
        return cls.addr_mgmt.ip_free_notify(ip_addr, vn_fq_name,
                                            ipam_refs=ipam_refs)
