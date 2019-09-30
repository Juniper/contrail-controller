#!/usr/bin/python

#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common.exceptions import NoIdError
from netaddr import IPAddress, IPNetwork
from vnc_api.vnc_api import VncApi


class FilterModule(object):
    def filters(self):
        return {
            'pr_subnet': self.get_pr_subnet,
            'supplemental_config': self.get_supplemental_config,
            'device_functional_group': self.validate_device_functional_group
        }
    # end filters

    @classmethod
    def get_pr_subnet(cls, job_ctx, fabric_uuid, device_fq_name):
        api = VncApi(auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
                     auth_token=job_ctx.get('auth_token'))
        fabric = api.fabric_read(id=fabric_uuid)
        fabric_dict = api.obj_to_dict(fabric)

        vn_uuid = None
        virtual_network_refs = fabric_dict.get('virtual_network_refs') or []
        for virtual_net_ref in virtual_network_refs:
            if 'management' in virtual_net_ref['attr']['network_type']:
                vn_uuid = virtual_net_ref['uuid']
                break
        if vn_uuid is None:
            raise NoIdError("Cannot find mgmt virtual network on fabric")

        virtual_net = api.virtual_network_read(id=vn_uuid)
        virtual_net_dict = api.obj_to_dict(virtual_net)

        subnets = None
        ipam_refs = virtual_net_dict.get('network_ipam_refs')
        if ipam_refs:
            ipam_ref = ipam_refs[0]
            ipam = api.network_ipam_read(id=ipam_ref['uuid'])
            ipam_dict = api.obj_to_dict(ipam)
            ipam_subnets = ipam_dict.get('ipam_subnets')
            if ipam_subnets:
                subnets = ipam_subnets.get('subnets')

        gateway = None
        cidr = None
        if subnets:
            pr = api.physical_router_read(fq_name=device_fq_name)
            pr_dict = api.obj_to_dict(pr)
            ip = pr_dict.get('physical_router_management_ip')
            ip_addr = IPAddress(ip)
            for subnet in subnets:
                inner_subnet = subnet.get('subnet')
                cidr = inner_subnet.get(
                    'ip_prefix') + '/' + str(inner_subnet.get('ip_prefix_len'))
                if ip_addr in IPNetwork(
                        cidr) and subnet.get('default_gateway'):
                    gateway = subnet.get('default_gateway')
                    break
        if cidr and gateway:
            return {'cidr': cidr, 'gateway': gateway}

        raise NoIdError(
            "Cannot find cidr and gateway for device: %s" %
            str(device_fq_name))
    # end get_pr_subnet

    @classmethod
    def get_supplemental_config(
            cls,
            device_name,
            device_to_ztp,
            supplemental_configs):
        supplemental_config = ""
        if device_name and device_to_ztp and supplemental_configs:
            device_map = dict((d.get('hostname', d.get('serial_number')), d)
                              for d in device_to_ztp)
            config_map = dict((c.get('name'), c) for c in supplemental_configs)
            if device_name in device_map:
                config_name = device_map[device_name].get(
                    'supplemental_day_0_cfg')
                if config_name in config_map:
                    supplemental_config = config_map[config_name].get('cfg')
        return supplemental_config
    # end get_supplemental_config


    @classmethod
    def validate_device_functional_group(cls, job_ctx, device_name,
                                         device_to_ztp):
        api = VncApi(auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
                     auth_token=job_ctx.get('auth_token'))
        final_dict = dict()
        if device_name and device_to_ztp:
            device_map = dict((d.get('hostname', d.get('serial_number')), d)
                              for d in device_to_ztp)
            existing_device_functional_group = api.device_functional_groups_list()
            existing_device_functional_list = \
                existing_device_functional_group['device-functional-groups']
            if device_name in device_map:
                if device_map[device_name].get('device_functional_group')!= None:
                    given_dfg = device_map[device_name].get('device_functional_group')
                    for item in existing_device_functional_list:
                        if item['fq_name'][2] == given_dfg:
                            dfg_uuid = item['uuid']
                            dfg_item = api.device_functional_group_read(
                                id=dfg_uuid)
                            dfg_item_dict = api.obj_to_dict(dfg_item)
                            final_dict['os_version'] = dfg_item_dict.get(
                                'device_functional_group_os_version')
                            physical_role_refs = dfg_item_dict.get(
                                'physical_role_refs')
                            final_dict['physical_role'] = physical_role_refs[0]['to'][1]
                            final_dict['rb_roles'] = dfg_item_dict.get(
                                'device_functional_group_routing_bridging_roles').get('rb_roles')
                            rr_flag = device_map[device_name].get('route_reflector')
                            if rr_flag != None:
                                final_dict['rb_roles'].append('Route-Reflector')
                            return final_dict
                    if len(final_dict) == 0:
                        raise NoIdError("The given DFG for device %s is not "
                                         "defined" % str(device_name))
                else:
                    return None
        else:
            return None
# end FilterModule
