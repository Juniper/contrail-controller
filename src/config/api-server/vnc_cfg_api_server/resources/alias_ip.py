#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#


from builtins import str

from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from vnc_api.gen.resource_common import AliasIp

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class AliasIpServer(ResourceMixin, AliasIp):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        vn_fq_name = obj_dict['fq_name'][:-2]
        req_ip = obj_dict.get("alias_ip_address")
        if req_ip and cls.addr_mgmt.is_ip_allocated(req_ip, vn_fq_name):
            return (False, (409, 'IP address already in use'))
        try:
            ok, result = cls.addr_mgmt.get_ip_free_args(vn_fq_name)
            if not ok:
                return ok, result
            aip_addr, sn_uuid, s_name = cls.addr_mgmt.ip_alloc_req(
                vn_fq_name,
                asked_ip_addr=req_ip,
                alloc_id=obj_dict['uuid'])

            def undo():
                msg = ('AddrMgmt: free AIP %s for vn=%s tenant=%s, on undo' %
                       (aip_addr, vn_fq_name, tenant_name))
                db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
                cls.addr_mgmt.ip_free_req(
                    aip_addr,
                    vn_fq_name,
                    alloc_id=obj_dict['uuid'],
                    vn_dict=result.get('vn_dict'),
                    ipam_dicts=result.get('ipam_dicts'))
                return True, ""
            get_context().push_undo(undo)
        except Exception as e:
            return (False, (500, str(e)))

        obj_dict['alias_ip_address'] = aip_addr
        msg = ('AddrMgmt: alloc %s AIP for vn=%s, tenant=%s, askip=%s' %
               (aip_addr, vn_fq_name, tenant_name, req_ip))
        db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)

        return True, ""

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        ok, ip_free_args = cls.addr_mgmt.get_ip_free_args(
            obj_dict['fq_name'][:-2])
        return ok, '', ip_free_args

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn, **kwargs):
        vn_fq_name = obj_dict['fq_name'][:-2]
        aip_addr = obj_dict['alias_ip_address']
        db_conn.config_log('AddrMgmt: free AIP %s for vn=%s'
                           % (aip_addr, vn_fq_name),
                           level=SandeshLevel.SYS_DEBUG)
        cls.addr_mgmt.ip_free_req(aip_addr, vn_fq_name,
                                  alloc_id=obj_dict['uuid'],
                                  vn_dict=kwargs.get('vn_dict'),
                                  ipam_dicts=kwargs.get('ipam_dicts'))

        return True, ""

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        aip_addr = obj_dict['alias_ip_address']
        vn_fq_name = obj_dict['fq_name'][:-2]
        cls.addr_mgmt.ip_alloc_notify(aip_addr, vn_fq_name)

        return True, ''

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        aip_addr = obj_dict['alias_ip_address']
        vn_fq_name = obj_dict['fq_name'][:-2]
        return cls.addr_mgmt.ip_free_notify(aip_addr, vn_fq_name,
                                            alloc_id=obj_dict['uuid'])
