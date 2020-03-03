#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from vnc_api.gen.resource_common import FloatingIp

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class FloatingIpServer(ResourceMixin, FloatingIp):
    @classmethod
    def _get_fip_pool_subnets(cls, fip_obj_dict, db_conn):
        fip_subnets = None

        # Get floating-ip-pool object.
        fip_pool_fq_name = fip_obj_dict['fq_name'][:-1]
        fip_pool_uuid = db_conn.fq_name_to_uuid('floating_ip_pool',
                                                fip_pool_fq_name)
        ok, res = cls.dbe_read(db_conn, 'floating_ip_pool', fip_pool_uuid)
        if ok:
            # Successful read returns fip pool.
            fip_pool_dict = res
        else:
            return ok, res

        # Get any subnets configured on the floating-ip-pool.
        try:
            fip_subnets = fip_pool_dict['floating_ip_pool_subnets']
        except (KeyError, TypeError):
            # It is acceptable that the prefixes and subnet_list may be absent
            # or may be None.
            pass

        return True, fip_subnets

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        if obj_dict['parent_type'] == 'instance-ip':
            return True, ''

        if len(obj_dict.get('project_refs', [])) == 0:
            project_fq_name = obj_dict['fq_name'][:2]
            obj_dict['project_refs'] = [{'to': project_fq_name,
                                         'attr': None}]

        vn_fq_name = obj_dict['fq_name'][:-2]
        req_ip = obj_dict.get('floating_ip_address')
        if req_ip and cls.addr_mgmt.is_ip_allocated(req_ip, vn_fq_name):
            return False, (409, 'IP address already in use')
        try:
            ok, result = cls.addr_mgmt.get_ip_free_args(vn_fq_name)
            if not ok:
                return ok, result
            #
            # Parse through floating-ip-pool config to see if there are any
            # guidelines laid for allocation of this floating-ip.
            #
            ok, ret_val = cls._get_fip_pool_subnets(obj_dict, db_conn)
            # On a successful fip-pool subnet get, the subnet list is returned.
            # Otherwise, returned value has appropriate reason string.
            if ok:
                fip_subnets = ret_val
            else:
                return ok, (400, 'Floating-ip-pool lookup failed with error: '
                            '%s' % ret_val)

            if not fip_subnets:
                # Subnet specification was not found on the floating-ip-pool.
                # Proceed to allocated floating-ip from any of the subnets
                # on the virtual-network.
                fip_addr, sn_uuid, s_name = cls.addr_mgmt.ip_alloc_req(
                    vn_fq_name,
                    asked_ip_addr=req_ip,
                    alloc_id=obj_dict['uuid'])
            else:
                fip_addr = None
                subnets_tried = []
                # Iterate through configured subnets on floating-ip-pool.
                # We will try to allocate floating-ip by iterating through
                # the list of configured subnets.
                for fip_pool_subnet in fip_subnets['subnet_uuid']:
                    try:
                        # Record the subnets that we try to allocate from.
                        subnets_tried.append(fip_pool_subnet)

                        fip_addr, sn_uuid, s_name = cls.addr_mgmt.ip_alloc_req(
                            vn_fq_name,
                            sub=fip_pool_subnet,
                            asked_ip_addr=req_ip,
                            alloc_id=obj_dict['uuid'])

                    except cls.addr_mgmt.AddrMgmtSubnetExhausted:
                        # This subnet is exhausted. Try next subnet.
                        continue

                if not fip_addr:
                    # Floating-ip could not be allocated from any of the
                    # configured subnets. Raise an exception.
                    raise cls.addr_mgmt.AddrMgmtSubnetExhausted(
                        vn_fq_name, subnets_tried)

            def undo():
                msg = ('AddrMgmt: free FIP %s for vn=%s on tenant=%s, on undo'
                       % (fip_addr, vn_fq_name, tenant_name))
                db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)
                cls.addr_mgmt.ip_free_req(fip_addr, vn_fq_name,
                                          alloc_id=obj_dict['uuid'],
                                          vn_dict=result.get('vn_dict'),
                                          ipam_dicts=result.get('ipam_dicts'))
                return True, ''
            get_context().push_undo(undo)
        except Exception as e:
            return False, (500, str(e))

        obj_dict['floating_ip_address'] = fip_addr
        msg = ('AddrMgmt: alloc %s FIP for vn=%s, tenant=%s, askip=%s' %
               (fip_addr, vn_fq_name, tenant_name, req_ip))
        db_conn.config_log(msg, level=SandeshLevel.SYS_DEBUG)

        return True, ''

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        if obj_dict['parent_type'] == 'instance-ip':
            return True, '', None
        ok, ip_free_args = cls.addr_mgmt.get_ip_free_args(
            obj_dict['fq_name'][:-2])
        return ok, '', ip_free_args

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn, **kwargs):
        if obj_dict['parent_type'] == 'instance-ip':
            return True, ''

        vn_fq_name = obj_dict['fq_name'][:-2]
        fip_addr = obj_dict['floating_ip_address']
        db_conn.config_log('AddrMgmt: free FIP %s for vn=%s'
                           % (fip_addr, vn_fq_name),
                           level=SandeshLevel.SYS_DEBUG)
        cls.addr_mgmt.ip_free_req(fip_addr, vn_fq_name,
                                  alloc_id=obj_dict['uuid'],
                                  vn_dict=kwargs.get('vn_dict'),
                                  ipam_dicts=kwargs.get('ipam_dicts'))

        return True, ''

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        if obj_dict['parent_type'] == 'instance-ip':
            return True, ''

        fip_addr = obj_dict['floating_ip_address']
        vn_fq_name = obj_dict['fq_name'][:-2]
        cls.addr_mgmt.ip_alloc_notify(fip_addr, vn_fq_name)

        return True, ''

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        if obj_dict['parent_type'] == 'instance-ip':
            return True, ''

        fip_addr = obj_dict['floating_ip_address']
        vn_fq_name = obj_dict['fq_name'][:-2]
        return cls.addr_mgmt.ip_free_notify(fip_addr, vn_fq_name,
                                            alloc_id=obj_dict['uuid'])
