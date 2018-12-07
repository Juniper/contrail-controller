#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import Bgpvpn

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class BgpvpnServer(ResourceMixin, Bgpvpn):
    @classmethod
    def check_network_supports_vpn_type(cls, vn_dict, db_vn_dict=None):
        """Validate bgpvpn type corresponds to VN forwarding type.

        Validate the associated bgpvpn type correspond to the virtual network
        forwarding type.
        """
        if not vn_dict:
            return True, ''

        if ('bgpvpn_refs' not in vn_dict and
                'virtual_network_properties' not in vn_dict):
            return True, ''

        forwarding_mode = 'l2_l3'
        vn_props = None
        if 'virtual_network_properties' in vn_dict:
            vn_props = vn_dict['virtual_network_properties']
        elif db_vn_dict and 'virtual_network_properties' in db_vn_dict:
            vn_props = db_vn_dict['virtual_network_properties']
        if vn_props is not None:
            forwarding_mode = vn_props.get('forwarding_mode', 'l2_l3')
        # Forwarding mode 'l2_l3' (default mode) can support all vpn types
        if forwarding_mode == 'l2_l3':
            return True, ''

        bgpvpn_uuids = []
        if 'bgpvpn_refs' in vn_dict:
            bgpvpn_uuids = set(bgpvpn_ref['uuid'] for bgpvpn_ref
                               in vn_dict.get('bgpvpn_refs', []))
        elif db_vn_dict:
            bgpvpn_uuids = set(bgpvpn_ref['uuid'] for bgpvpn_ref
                               in db_vn_dict.get('bgpvpn_refs', []))

        if not bgpvpn_uuids:
            return True, ''

        ok, result, _ = cls.db_conn.dbe_list('bgpvpn',
                                             obj_uuids=list(bgpvpn_uuids),
                                             field_names=['bgpvpn_type'])
        if not ok:
            return False, result
        bgpvpns = result

        vpn_types = set(bgpvpn.get('bgpvpn_type', 'l3') for bgpvpn in bgpvpns)
        if len(vpn_types) > 1:
            msg = ("Cannot associate different bgpvpn types '%s' on a "
                   "virtual network with a forwarding mode different to"
                   "'l2_l3'" % vpn_types)
            return False, (400, msg)
        elif set([forwarding_mode]) != vpn_types:
            msg = ("Cannot associate bgpvpn type '%s' with a virtual "
                   "network in forwarding mode '%s'" % (vpn_types.pop(),
                                                        forwarding_mode))
            return False, (400, msg)
        return True, ''

    @classmethod
    def check_router_supports_vpn_type(cls, lr_dict):
        """Limit associated bgpvpn types to 'l3' for logical router."""
        if not lr_dict or 'bgpvpn_refs' not in lr_dict:
            return True, ''

        bgpvpn_uuids = set(bgpvpn_ref['uuid'] for bgpvpn_ref
                           in lr_dict.get('bgpvpn_refs', []))
        if not bgpvpn_uuids:
            return True, ''

        ok, result, _ = cls.db_conn.dbe_list('bgpvpn',
                                             obj_uuids=list(bgpvpn_uuids),
                                             field_names=['bgpvpn_type'])
        if not ok:
            return False, result
        bgpvpns = result

        bgpvpn_not_supported = [bgpvpn for bgpvpn in bgpvpns
                                if bgpvpn.get('bgpvpn_type', 'l3') != 'l3']

        if not bgpvpn_not_supported:
            return True, ''

        msg = "Only bgpvpn type 'l3' can be associated to a logical router:\n"
        for bgpvpn in bgpvpn_not_supported:
            msg += ("- bgpvpn %s(%s) type is %s\n" %
                    (bgpvpn.get('display_name', bgpvpn['fq_name'][-1]),
                     bgpvpn['uuid'], bgpvpn.get('bgpvpn_type', 'l3')))
        return False, (400, msg)

    @classmethod
    def check_network_has_bgpvpn_assoc_via_router(cls, vn_dict,
                                                  db_vn_dict=None):
        """Check LR of the VN does not have a bgpvpn allready associated.

        Check if logical routers attached to the network already have
        a bgpvpn associated to it. If yes, forbid to add bgpvpn to that
        networks.
        """
        if vn_dict.get('bgpvpn_refs') is None:
            return True, ''

        # List all logical router's vmis of networks
        filters = {
            'virtual_machine_interface_device_owner':
            ['network:router_interface']
        }
        ok, result, _ = cls.db_conn.dbe_list(
            'virtual_machine_interface',
            back_ref_uuids=[vn_dict['uuid']],
            filters=filters,
            field_names=['logical_router_back_refs'])
        if not ok:
            return False, result
        vmis = result

        # Read bgpvpn refs of logical routers found
        lr_uuids = [vmi['logical_router_back_refs'][0]['uuid']
                    for vmi in vmis]
        if not lr_uuids:
            return True, ''
        ok, result, _ = cls.db_conn.dbe_list('logical_router',
                                             obj_uuids=lr_uuids,
                                             field_names=['bgpvpn_refs'])
        if not ok:
            return False, result
        lrs = result
        found_bgpvpns = [(bgpvpn_ref['to'][-1], bgpvpn_ref['uuid'],
                          lr.get('display_name', lr['fq_name'][-1]),
                          lr['uuid'])
                         for lr in lrs
                         for bgpvpn_ref in lr.get('bgpvpn_refs', [])]
        if not found_bgpvpns:
            return True, ''

        vn_name = (vn_dict.get('fq_name') or db_vn_dict['fq_name'])[-1]
        msg = ("Network %s (%s) is linked to a logical router which is "
               "associated to bgpvpn(s):\n" % (vn_name, vn_dict['uuid']))
        for found_bgpvpn in found_bgpvpns:
            msg += ("- bgpvpn %s (%s) associated to router %s (%s)\n" %
                    found_bgpvpn)
        return False, (400, msg[:-1])

    @classmethod
    def check_router_has_bgpvpn_assoc_via_network(cls, lr_dict,
                                                  db_lr_dict=None):
        """Check VN of the LR does not have a bgpvpn allready associated.

        Check if virtual networks attached to the logical router already have
        a bgpvpn associated to it. If yes, forbid to add bgpvpn to that
        routers.
        """
        if ('bgpvpn_refs' not in lr_dict and
                'virtual_machine_interface_refs' not in lr_dict):
            return True, ''

        bgpvpn_refs = None
        if 'bgpvpn_refs' in lr_dict:
            bgpvpn_refs = lr_dict['bgpvpn_refs']
        elif db_lr_dict:
            bgpvpn_refs = db_lr_dict.get('bgpvpn_refs')
        if not bgpvpn_refs:
            return True, ''

        vmi_refs = []
        if 'virtual_machine_interface_refs' in lr_dict:
            vmi_refs = lr_dict['virtual_machine_interface_refs']
        elif db_lr_dict:
            vmi_refs = db_lr_dict.get('virtual_machine_interface_refs') or []
        vmi_uuids = [vmi_ref['uuid'] for vmi_ref in vmi_refs]
        if not vmi_uuids:
            return True, ''

        # List vmis to obtain their virtual networks
        ok, result, _ = cls.db_conn.dbe_list(
            'virtual_machine_interface',
            obj_uuids=vmi_uuids,
            field_names=['virtual_network_refs'])
        if not ok:
            return False, result
        vmis = result
        vn_uuids = [vn_ref['uuid']
                    for vmi in vmis
                    for vn_ref in vmi.get('virtual_network_refs', [])]
        if not vn_uuids:
            return True, ''

        # List bgpvpn refs of virtual networks found
        ok, result, _ = cls.db_conn.dbe_list('virtual_network',
                                             obj_uuids=vn_uuids,
                                             field_names=['bgpvpn_refs'])
        if not ok:
            return False, result
        vns = result
        found_bgpvpns = [(bgpvpn_ref['to'][-1], bgpvpn_ref['uuid'],
                          vn.get('display_name', vn['fq_name'][-1]),
                          vn['uuid'])
                         for vn in vns
                         for bgpvpn_ref in vn.get('bgpvpn_refs', [])]
        if not found_bgpvpns:
            return True, ''
        lr_name = (lr_dict.get('fq_name') or db_lr_dict['fq_name'])[-1]
        msg = ("Router %s (%s) is linked to virtual network(s) which is/are "
               "associated to bgpvpn(s):\n" % (lr_name, lr_dict['uuid']))
        for found_bgpvpn in found_bgpvpns:
            msg += ("- bgpvpn %s (%s) associated to network %s (%s)\n" %
                    found_bgpvpn)
        return False, (400, msg[:-1])
