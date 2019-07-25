#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common import get_bgp_rtgt_max_id
from cfgm_common import get_bgp_rtgt_min_id
from cfgm_common.exceptions import VncError
from netaddr import AddrFormatError
from netaddr import IPAddress
from vnc_api.gen.resource_common import RouteTarget

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class RouteTargetServer(ResourceMixin, RouteTarget):

    @staticmethod
    def _parse_route_target_name(name):
        try:
            if isinstance(name, basestring):
                prefix, asn, target = name.split(':')
            elif isinstance(name, list):
                prefix, asn, target = name
            else:
                raise ValueError
            if prefix != 'target':
                raise ValueError
            target = int(target)
<<<<<<< HEAD   (c71833 Merge "[DM] Allow combination of tagged and untagged IFL" in)
=======
            if asn.lower().endswith('l'):
                if target > 0xFFFF:
                    msg = ("Route Target index value must be less than "
                           "0xFFFF in case of 4 byte ASN")
                    return False, (400, msg)
                asn = asn[:-1]
>>>>>>> CHANGE (ec93b6 [Config] Add range checks for user created route targets)
            if not asn.isdigit():
                try:
                    IPAddress(asn)
                except AddrFormatError:
                    raise ValueError
            else:
                asn = int(asn)
        except ValueError:
            msg = ("Route target must be of the format "
                   "'target:<asn>:<number>' or 'target:<ip>:<number>'")
            return False, (400, msg)
        return True, (asn, target)

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls._parse_route_target_name(obj_dict['fq_name'][-1])

    @classmethod
    def validate_route_target(cls, route_target_name, global_asn=None):
        ok, result = cls._parse_route_target_name(route_target_name)
        if not ok:
            return False, result
        asn, target = result

        if not global_asn:
            try:
                global_asn = cls.server.global_autonomous_system
            except VncError as e:
                return False, (400, str(e))

        if type(asn) == int:
            ok, result = cls.server.get_resource_class(
                'global_system_config').check_asn_range(asn)
            if not ok:
                return ok, result

        # If the ASN of Route Target matches Global ASN, we need to check
        # for its target range. The target should not clash with contrail
        # RT target reserved space as detailed below
        if asn == global_asn:
            global_4byte_flag = cls.server.enable_4byte_as
            if global_4byte_flag:
                # Case when 4 byte ASN flag is set
                # Target should be
                # 1. 1 <= target < _BGP_RTGT_MIN_ID_TYPE1_2
                # 2. _BGP_RTGT_MAX_ID_TYPE1_2 < target <= 0xFFFF
                # This is because contrail allocates RT targets in the range of
                # 8000-32768
                if ((1 <= target < get_bgp_rtgt_min_id(asn)) or
                   (get_bgp_rtgt_max_id(asn) < target <= 0xFFFF)):
                    return True, True
                else:
                    return True, False
            else:
                # Case when 2 byte ASN flag is set
                # Target should be:
                # 1. 1 <= target < _BGP_RTGT_MIN_ID_TYPE0
                # 2. _BGP_RTGT_MAX_ID_TYPE0 < target <= 0xFFFFFFFF
                if ((1 <= target < get_bgp_rtgt_min_id(asn)) or
                   (get_bgp_rtgt_max_id(asn) < target <= 0xFFFFFFFF)):
                    return True, True
                else:
                    return True, False

        return True, True
