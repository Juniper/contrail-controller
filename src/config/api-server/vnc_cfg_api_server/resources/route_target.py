#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

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
            if asn.lower().endswith('l'):
                asn = asn[:-1]
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
    def is_user_defined(cls, route_target_name, global_asn=None):
        ok, result = cls._parse_route_target_name(route_target_name)
        if not ok:
            return False, result
        asn, target = result
        if not global_asn:
            try:
                global_asn = cls.server.global_autonomous_system
            except VncError as e:
                return False, (400, str(e))

        if (asn == global_asn and
                target >= get_bgp_rtgt_min_id(global_asn)):
            return True, False
        return True, True
