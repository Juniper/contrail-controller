#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from builtins import zip

from netaddr import IPRange
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from vnc_api.gen.resource_common import VirtualRouter

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class VirtualRouterServer(ResourceMixin, VirtualRouter):
    @staticmethod
    def _vr_to_pools(obj_dict):
        # given a VR return its allocation-pools in list
        ipam_refs = obj_dict.get('network_ipam_refs')
        if ipam_refs is not None:
            pool_list = []
            for ref in ipam_refs:
                vr_ipam_data = ref['attr']
                vr_pools = vr_ipam_data.get('allocation_pools', [])
                pool_list.extend([(vr_pool['start'], vr_pool['end'])
                                 for vr_pool in vr_pools])
        else:
            pool_list = None

        return pool_list

    # check if any ip address from given alloc_pool sets is used in
    # in given virtual router, for instance_ip
    @classmethod
    def _check_vr_alloc_pool_delete(cls, pool_set, vr_dict, db_conn):
        iip_refs = vr_dict.get('instance_ip_back_refs') or []
        if not iip_refs:
            return True, ''

        iip_uuid_list = [(iip_ref['uuid']) for iip_ref in iip_refs]
        ok, iip_list, _ = db_conn.dbe_list(
            'instance_ip',
            obj_uuids=iip_uuid_list,
            field_names=['instance_ip_address'])
        if not ok:
            return False, iip_list

        for iip in iip_list:
            iip_addr = iip.get('instance_ip_address')
            if not iip_addr:
                cls.config_log(
                    "Error in pool delete ip null: %s" % iip['uuid'],
                    level=SandeshLevel.SYS_ERR)
                continue

            for alloc_pool in pool_set:
                if iip_addr in IPRange(alloc_pool[0], alloc_pool[1]):
                    msg = "Cannot Delete allocation pool, %s in use" % iip_addr
                    return False, (400, msg)

        return True, ''

    @classmethod
    def _vrouter_check_alloc_pool_delete(cls, db_vr_dict, req_vr_dict,
                                         db_conn):
        if 'network_ipam_refs' not in req_vr_dict:
            # alloc_pools not modified in request
            return True, ''

        ipam_refs = req_vr_dict.get('network_ipam_refs')
        if not ipam_refs:
            iip_refs = db_vr_dict.get('instance_ip_back_refs')
            if iip_refs:
                msg = "Cannot Delete allocation pool, IP address in use"
                return False, (400, msg)

        existing_vr_pools = cls._vr_to_pools(db_vr_dict)
        if not existing_vr_pools:
            return True, ''

        requested_vr_pools = cls._vr_to_pools(req_vr_dict)
        delete_set = set(existing_vr_pools) - set(requested_vr_pools)
        if not delete_set:
            return True, ''

        return cls._check_vr_alloc_pool_delete(delete_set, db_vr_dict, db_conn)

    @staticmethod
    def _validate_vrouter_alloc_pools(vrouter_dict, db_conn, ipam_refs):
        if not ipam_refs:
            return True, ''

        vrouter_uuid = vrouter_dict['uuid']
        ipam_uuid_list = []
        for ipam_ref in ipam_refs:
            if 'uuid' in ipam_ref:
                ipam_ref_uuid = ipam_ref.get('uuid')
            else:
                ipam_fq_name = ipam_ref['to']
                ipam_ref_uuid = db_conn.fq_name_to_uuid('network_ipam',
                                                        ipam_fq_name)
            ipam_uuid_list.append(ipam_ref_uuid)
        ok, ipam_list, _ = db_conn.dbe_list(
            'network_ipam',
            obj_uuids=ipam_uuid_list,
            field_names=['ipam_subnet_method',
                         'ipam_subnets',
                         'virtual_router_back_refs'])
        if not ok:
            return False, ipam_list

        for ipam_ref, ipam in zip(ipam_refs, ipam_list):
            subnet_method = ipam.get('ipam_subnet_method')
            if subnet_method != 'flat-subnet':
                msg = "Only flat-subnet ipam can be attached to vrouter"
                return False, (400, msg)

            ipam_subnets = ipam.get('ipam_subnets', {})
            # read data on the link between vrouter and ipam
            # if alloc pool exists, then make sure that alloc-pools are
            # configured in ipam subnet with a flag indicating
            # vrouter specific allocation pool
            vr_ipam_data = ipam_ref['attr']
            vr_alloc_pools = vr_ipam_data.get('allocation_pools')
            if not vr_alloc_pools:
                msg = "No allocation-pools for this vrouter"
                return False, (400, msg)

            for vr_alloc_pool in vr_alloc_pools:
                vr_alloc_pool.pop('vrouter_specific_pool', None)

            # get all allocation pools in this ipam
            subnets = ipam_subnets.get('subnets', [])
            ipam_alloc_pools = []
            for subnet in subnets:
                subnet_alloc_pools = subnet.get('allocation_pools', [])
                for subnet_alloc_pool in subnet_alloc_pools:
                    vr_flag = subnet_alloc_pool.get('vrouter_specific_pool')
                    if vr_flag:
                        ipam_alloc = {'start': subnet_alloc_pool['start'],
                                      'end': subnet_alloc_pool['end']}
                        ipam_alloc_pools.append(ipam_alloc)

            for vr_alloc_pool in vr_alloc_pools:
                if vr_alloc_pool not in ipam_alloc_pools:
                    msg = ("vrouter allocation-pool start:%s, end:%s not in "
                           "ipam" % (vr_alloc_pool['start'],
                                     vr_alloc_pool['end']))
                    return False, (400, msg)

            if 'virtual_router_back_refs' not in ipam:
                continue

            vr_back_refs = ipam['virtual_router_back_refs']
            for vr_back_ref in vr_back_refs:
                if vr_back_ref['uuid'] == vrouter_uuid:
                    continue

                back_ref_ipam_data = vr_back_ref['attr']
                bref_alloc_pools = back_ref_ipam_data.get('allocation_pools')
                if not bref_alloc_pools:
                    continue

                for vr_alloc_pool in vr_alloc_pools:
                    if vr_alloc_pool in bref_alloc_pools:
                        msg = ("vrouter allocation-pool start:%s, end:%s is "
                               "used in other vrouter:%s" %
                               (vr_alloc_pool['start'], vr_alloc_pool['end'],
                                vr_back_ref['uuid']))
                        return False, (400, msg)

        return True, ''

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ipam_refs = obj_dict.get('network_ipam_refs') or []
        if not ipam_refs:
            return True, ''

        ok, result = cls._validate_vrouter_alloc_pools(obj_dict, db_conn,
                                                       ipam_refs)
        if not ok:
            return False, result

        return True, ''

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):

        ok, db_dict = cls.dbe_read(db_conn, 'virtual_router', id)
        ok, result = cls._vrouter_check_alloc_pool_delete(db_dict, obj_dict,
                                                          db_conn)
        if not ok:
            return False, result

        ipam_refs = obj_dict.get('network_ipam_refs')
        if ipam_refs:
            ok, result = cls._validate_vrouter_alloc_pools(obj_dict,
                                                           db_conn,
                                                           ipam_refs)
            if not ok:
                return False, (400, result)

        return True, ''
