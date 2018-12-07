#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import re

from vnc_api.gen.resource_common import GlobalSystemConfig

from vnc_cfg_api_server.resources import ResourceMixin
from vnc_cfg_api_server.resources import RouteTargetServer
from vnc_cfg_api_server.resources import SecurityMixin


class GlobalSystemConfigServer(ResourceMixin, GlobalSystemConfig):
    @classmethod
    def _get_global_system_config(cls, fields=None):
        return cls.locate(fq_name=['default-global-system-config'],
                          create_it=False, fields=fields)

    @classmethod
    def _check_valid_port_range(cls, port_start, port_end):
        if int(port_start) > int(port_end):
            return (False, (400, 'Invalid Port range specified'))
        return True, ''

    @classmethod
    def _check_bgpaas_ports(cls, obj_dict, db_conn):
        bgpaas_ports = obj_dict.get('bgpaas_parameters')
        if not bgpaas_ports:
            return (True, '')

        ok, msg = cls._check_valid_port_range(bgpaas_ports['port_start'],
                                              bgpaas_ports['port_end'])
        if not ok:
            return ok, msg

        ok, result = cls._get_global_system_config(['bgpaas_parameters'])
        if not ok:
            return False, result
        global_sys_cfg = result

        cur_bgpaas_ports = global_sys_cfg.get('bgpaas_parameters') or \
            {'port_start': 50000, 'port_end': 50512}

        (ok, bgpaas_list, _) = db_conn.dbe_list('bgp_as_a_service',
                                                is_count=True)

        if not ok:
            return (ok, bgpaas_list)

        if bgpaas_list:
            if (int(bgpaas_ports['port_start']) >
                    int(cur_bgpaas_ports['port_start']) or
                int(bgpaas_ports['port_end']) <
                    int(cur_bgpaas_ports['port_end'])):
                return (False, (400, 'BGP Port range cannot be shrunk'))
        return (True, '')

    @classmethod
    def _check_asn(cls, obj_dict):
        global_asn = obj_dict.get('autonomous_system')
        if not global_asn:
            return True, ''

        ok, result, _ = cls.db_conn.dbe_list('virtual_network',
                                             field_names=['route_target_list'])
        if not ok:
            return False, (500, 'Error in dbe_list: %s' % result)
        vn_list = result

        founded_vn_using_asn = []
        for vn in vn_list:
            rt_dict = vn.get('route_target_list', {})
            for rt in rt_dict.get('route_target', []):
                ok, result = RouteTargetServer.is_user_defined(rt, global_asn)
                if not ok:
                    return False, result
                user_defined_rt = result
                if not user_defined_rt:
                    founded_vn_using_asn.append((':'.join(vn['fq_name']),
                                                vn['uuid'], rt))
        if not founded_vn_using_asn:
            return True, ''
        msg = ("Virtual networks are configured with a route target with this "
               "ASN %d and route target value in the same range as used by "
               "automatically allocated route targets:\n" % global_asn)
        for fq_name, uuid, rt in founded_vn_using_asn:
            msg += ("\t- %s (%s) have route target %s configured\n" %
                    (':'.join(fq_name), uuid, rt[7:]))
        return False, (400, msg)

    @classmethod
    def _check_udc(cls, obj_dict, udcs):
        udcl = []
        for udc in udcs:
            if all(k in udc.get('value') or {} for k in ('name', 'pattern')):
                udcl.append(udc['value'])
        udck = obj_dict.get('user_defined_log_statistics')
        if udck:
            udcl += udck['statlist']

        for udc in udcl:
            try:
                re.compile(udc['pattern'])
            except Exception as e:
                msg = ("Regex error in user-defined-log-statistics at %s: %s "
                       "(Error: %s)" % (udc['name'], udc['pattern'], str(e)))
                return False, (400, msg)
        return True, ''

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls._get_global_system_config(['fq_name', 'uuid'])
        if not ok:
            return False, result
        gsc = result

        msg = ("Global System Config already exists with name %s (%s)" %
               (':'.join(gsc['fq_name']), gsc['uuid']))
        return False, (400, msg)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        udcs = filter(
            lambda x: (x.get('field', '') == 'user_defined_log_statistics' and
                       x.get('operation', '') == 'set'),
            kwargs.get('prop_collection_updates') or [])
        ok, result = cls._check_udc(obj_dict, udcs)
        if not ok:
            return ok, result

        ok, result = cls._check_asn(obj_dict)
        if not ok:
            return ok, result

        ok, result = cls._check_bgpaas_ports(obj_dict, db_conn)
        if not ok:
            return ok, result

        if 'enable_security_policy_draft' in obj_dict:
            fields = ['fq_name', 'uuid', 'enable_security_policy_draft']
            ok, result = cls._get_global_system_config(fields)
            if not ok:
                return False, result
            db_obj_dict = result

            obj_dict['fq_name'] = db_obj_dict['fq_name']
            obj_dict['uuid'] = db_obj_dict['uuid']
            SecurityMixin.server = cls.server
            return SecurityMixin.\
                set_policy_management_for_security_draft(
                    cls.resource_type,
                    obj_dict,
                    draft_mode_enabled=db_obj_dict.get(
                        'enable_security_policy_draft', False),
                )

        return True, ''

    @classmethod
    def post_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        if 'autonomous_system' in obj_dict:
            cls.server.global_autonomous_system = obj_dict['autonomous_system']

        return True, ''
