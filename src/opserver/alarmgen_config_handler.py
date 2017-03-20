#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#


import socket
import json

from vnc_api.gen.resource_client import Alarm
from vnc_api.gen.resource_xsd import IdPermsType, AlarmExpression, \
    AlarmOperand2, AlarmAndList, AlarmOrList, UveKeysType
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh.alarmgen_ctrl.ttypes import AlarmgenConfigLog
from config_handler import ConfigHandler
from opserver_util import camel_case_to_hyphen, inverse_dict
from plugins.alarm_base import AlarmBase 
from sandesh.viz.constants import UVE_MAP


_INVERSE_UVE_MAP = inverse_dict(UVE_MAP)


class AlarmGenConfigHandler(ConfigHandler):

    def __init__(self, sandesh_instance, module_id, instance_id, logger,
                 api_server_config, keystone_info, rabbitmq_info,
                 alarm_plugins, alarm_config_change_callback):
        service_id = socket.gethostname()+':'+module_id+':'+instance_id
        config_types = ['global-system-config', 'alarm']
        super(AlarmGenConfigHandler, self).__init__(service_id, logger,
              api_server_config, keystone_info, rabbitmq_info, config_types)
        self._sandesh_instance = sandesh_instance
        self._alarm_plugins = alarm_plugins
        self._alarm_config_change_callback = alarm_config_change_callback
        self._inbuilt_alarms = {}
        self._config_ownership = False
        self._inbuilt_alarms_created = False
        self._config_db = {}
        self._alarm_config_db = {}
        self._create_inbuilt_alarms_config()
    # end __init__

    def config_db(self):
        return self._config_db
    # end config_db

    def alarm_config_db(self):
        return self._alarm_config_db
    # end alarm_config_db

    def _update_alarm_config_table(self, alarm_fqname, alarm_obj, uve_keys,
                                   operation):
        alarm_config_change_map = {}
        for key in uve_keys:
            uve_type_name = key.split(':', 1)
            try:
                table = UVE_MAP[uve_type_name[0]]
            except KeyError:
                self._logger('Invalid uve_key "%s" specified in '
                    'alarm config "%s"' % (key, alarm_fqname),
                    SandeshLevel.SYS_ERR)
            else:
                if len(uve_type_name) == 2:
                    uve_key = table+':'+uve_type_name[1]
                else:
                    uve_key = table
                try:
                    alarm_table = self._alarm_config_db[uve_key]
                except KeyError:
                    self._alarm_config_db[uve_key] = {}
                    alarm_table = self._alarm_config_db[uve_key]
                finally:
                    if operation == 'CREATE' or operation == 'UPDATE':
                        if not isinstance(alarm_obj, AlarmBase):
                            if alarm_table.has_key(alarm_fqname):
                                alarm_table[alarm_fqname].set_config(alarm_obj)
                            else:
                                alarm_base_obj = AlarmBase(config=alarm_obj)
                                alarm_table[alarm_fqname] = alarm_base_obj
                        else:
                            alarm_table[alarm_fqname] = alarm_obj
                    elif operation == 'DELETE':
                        if alarm_table.has_key(alarm_fqname):
                            del alarm_table[alarm_fqname]
                        if not len(alarm_table):
                            del self._alarm_config_db[uve_key]
                    else:
                        assert(0)
                    alarm_config_change_map[uve_key] = {alarm_fqname:operation}
        return alarm_config_change_map
    # end _update_alarm_config_table

    def _create_inbuilt_alarms_config(self):
        self._inbuilt_alarms = {}
        for table, plugins in self._alarm_plugins.iteritems():
            for extn in plugins[table]:
                alarm_name = camel_case_to_hyphen(
                    extn.obj.__class__.__name__)
                if self._inbuilt_alarms.has_key(alarm_name):
                    uve_keys = self._inbuilt_alarms[alarm_name].get_uve_keys()
                    uve_keys.uve_key.append(_INVERSE_UVE_MAP[table])
                    self._inbuilt_alarms[alarm_name].set_uve_keys(uve_keys)
                else:
                    alarm_or_list = None
                    if extn.obj.rules():
                        alarm_or_list = []
                        for and_list in extn.obj.rules()['or_list']:
                            alarm_and_list = []
                            for exp in and_list['and_list']:
                                alarm_and_list.append(AlarmExpression(
                                    operation=exp['operation'],
                                    operand1=exp['operand1'],
                                    operand2=AlarmOperand2(uve_attribute=
                                        exp['operand2'].get('uve_attribute'),
                                        json_value=exp['operand2'].get(
                                            'json_value')),
                                    variables=exp.get('variables')))
                            alarm_or_list.append(AlarmAndList(alarm_and_list))
                    desc = ' '.join([l.strip() \
                        for l in extn.obj.__doc__.splitlines()])
                    id_perms = IdPermsType(creator='system', description=desc)
                    kwargs = {'parent_type': 'global-system-config',
                        'fq_name': ['default-global-system-config',
                            alarm_name]}
                    self._inbuilt_alarms[alarm_name] = Alarm(name=alarm_name,
                        uve_keys=UveKeysType([_INVERSE_UVE_MAP[table]]),
                        alarm_severity=extn.obj.severity(),
                        alarm_rules=AlarmOrList(alarm_or_list),
                        id_perms=id_perms, **kwargs)
                extn.obj._config = self._inbuilt_alarms[alarm_name]
                fqname_str = self._fqname_to_str(extn.obj._config.fq_name)
                uve_keys = [_INVERSE_UVE_MAP[table]]
                self._update_alarm_config_table(fqname_str, extn.obj,
                    uve_keys, 'CREATE')
    # end _create_inbuilt_alarms_config

    def _handle_config_update(self, config_type, fq_name, config_obj,
                              operation):
        # Log config update
        config_dict = None
        if config_obj is not None:
            config_dict = {k: json.dumps(v) for k, v in \
                self.obj_to_dict(config_obj).iteritems()}
        alarmgen_config_log = AlarmgenConfigLog(fq_name, config_type,
            operation, config_dict, sandesh=self._sandesh_instance)
        alarmgen_config_log.send(sandesh=self._sandesh_instance)
        if not self._config_db.get(config_type):
            self._config_db[config_type] = {}
        alarm_config_change_map = {}
        if operation == 'CREATE' or operation == 'UPDATE':
            if config_type == 'alarm':
                if '_alarm_rules' not in config_obj.__dict__:
                    self._logger('Ignoring conf for inbuilt alarm %s' % \
                            fq_name, SandeshLevel.SYS_INFO)
                    return
                alarm_config = self._config_db[config_type].get(fq_name)
                if alarm_config is None:
                    alarm_config_change_map = self._update_alarm_config_table(
                        fq_name, config_obj, config_obj.uve_keys.uve_key,
                        'CREATE')
                else:
                    # If the alarm config already exists, then check for
                    # addition/deletion of elements from uve_keys and
                    # update the alarm_config_db appropriately.
                    add_uve_keys = set(config_obj.uve_keys.uve_key) - \
                        set(alarm_config.uve_keys.uve_key)
                    if add_uve_keys:
                        alarm_config_change_map.update(
                            self._update_alarm_config_table(
                                fq_name, config_obj, add_uve_keys, 'CREATE'))
                    del_uve_keys = set(alarm_config.uve_keys.uve_key) - \
                        set(config_obj.uve_keys.uve_key)
                    if del_uve_keys:
                        alarm_config_change_map.update(
                            self._update_alarm_config_table(
                                fq_name, None, del_uve_keys, 'DELETE'))
                    upd_uve_keys = \
                        set(config_obj.uve_keys.uve_key).intersection(
                            set(alarm_config.uve_keys.uve_key))
                    if upd_uve_keys:
                        alarm_config_change_map.update(
                            self._update_alarm_config_table(
                                fq_name, config_obj, upd_uve_keys, 'UPDATE'))
            self._config_db[config_type][fq_name] = config_obj
        elif operation == 'DELETE':
            config_obj = self._config_db[config_type].get(fq_name)
            if config_obj is not None:
                if config_type == 'alarm':
                    alarm_config_change_map = self._update_alarm_config_table(
                        fq_name, None, config_obj.uve_keys.uve_key, 'DELETE')
                del self._config_db[config_type][fq_name]
            if not len(self._config_db[config_type]):
                del self._config_db[config_type]
        else:
            # Invalid operation
            assert(0)
        if alarm_config_change_map:
            self._alarm_config_change_callback(alarm_config_change_map)
    # end _handle_config_update

    def _handle_config_sync(self, config):
        for cfg_type, cfg_obj_list in config.iteritems():
            self._logger('sync for config type "%s"' % (cfg_type),
                SandeshLevel.SYS_INFO)
            for cfg_obj in cfg_obj_list:
                fq_name = self._fqname_to_str(cfg_obj.fq_name)
                self._handle_config_update(cfg_type, fq_name, cfg_obj,
                    'UPDATE')
    # end _handle_config_sync


# end class AlarmGenConfigHandler
