#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import json

import six
from vnc_api.gen.resource_common import Alarm

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class AlarmServer(ResourceMixin, Alarm):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        if 'alarm_rules' not in obj_dict or obj_dict['alarm_rules'] is None:
            return False, (400, 'alarm_rules not specified or null')
        ok, error = cls._check_alarm_rules(obj_dict['alarm_rules'])
        if not ok:
            return False, error
        return True, ''

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        if 'alarm_rules' in obj_dict:
            if obj_dict['alarm_rules'] is None:
                return False, (400, 'alarm_rules cannot be removed')
            ok, error = cls._check_alarm_rules(obj_dict['alarm_rules'])
            if not ok:
                return False, error
        return True, ''

    @staticmethod
    def _check_alarm_rules(alarm_rules):
        operand2_fields = ['uve_attribute', 'json_value']
        try:
            for and_list in alarm_rules['or_list']:
                for and_cond in and_list['and_list']:
                    if any(k in and_cond['operand2'] for k in operand2_fields):
                        uve_attr = and_cond['operand2'].get('uve_attribute')
                        json_val = and_cond['operand2'].get('json_value')
                        if uve_attr is not None and json_val is not None:
                            msg = ("operand2 should have either "
                                   "'uve_attribute' or 'json_value', not both")
                            return False, (400, msg)
                        if json_val is not None:
                            try:
                                json.loads(json_val)
                            except ValueError:
                                msg = ("Invalid json_value '%s' specified in "
                                       "alarm_rules" % json_val)
                                return False, (400, msg)
                        if and_cond['operation'] == 'range':
                            if json_val is None:
                                msg = ("json_value not specified for 'range' "
                                       "operation")
                                return False, (400, msg)
                            val = json.loads(json_val)
                            valid_types = (six.integer_types, float)
                            if not (isinstance(val, list) and
                                    len(val) == 2 and
                                    isinstance(val[0], valid_types) and
                                    isinstance(val[1], valid_types) and
                                    val[0] < val[1]):
                                msg = ("Invalid json_value %s for 'range' "
                                       "operation. json_value should be "
                                       "specified as '[x, y]', where x < y" %
                                       json_val)
                                return False, (400, msg)
                    else:
                        msg = ("operand2 should have 'uve_attribute' or "
                               "'json_value'")
                        return False, (400, msg)
        except Exception:
            return False, (400, 'Invalid alarm_rules')
        return True, ''
