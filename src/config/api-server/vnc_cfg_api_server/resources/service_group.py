#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common import proto_dict
from vnc_api.gen.resource_common import ServiceGroup

from vnc_cfg_api_server.resources._security_base import SecurityResourceBase


class ServiceGroupServer(SecurityResourceBase, ServiceGroup):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls.check_draft_mode_state(obj_dict)
        if not ok:
            return False, result

        # create protcol id
        try:
            firewall_services = obj_dict[
                'service_group_firewall_service_list']['firewall_service']
        except Exception:
            return True, ''

        for service in firewall_services:
            if service.get('protocol') is None:
                continue
            protocol = service['protocol']
            if protocol.isdigit():
                protocol_id = int(protocol)
                if protocol_id < 0 or protocol_id > 255:
                    return False, (400, 'Invalid protocol: %s' % protocol)
            elif protocol not in proto_dict:
                return False, (400, 'Invalid protocol: %s' % protocol)
            else:
                protocol_id = proto_dict[protocol]
            service['protocol_id'] = protocol_id

        return True, ''

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls.pre_dbe_create(None, obj_dict, db_conn)
