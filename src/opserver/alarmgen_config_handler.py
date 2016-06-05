#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#


from vnc_api.gen.resource_client import Alarm
from vnc_api.gen.resource_xsd import IdPermsType, AlarmExpression, \
    AlarmAndList, AlarmOrList
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from config_handler import ConfigHandler
from opserver_util import camel_case_to_hyphen, inverse_dict
from plugins.alarm_base import AlarmBase 
from sandesh.viz.constants import UVE_MAP


_INVERSE_UVE_MAP = inverse_dict(UVE_MAP)


class AlarmGenConfigHandler(ConfigHandler):

    def __init__(self, module_id, instance_id, logger,
                 discovery_client, keystone_info, rabbitmq_info,
                 alarm_plugins):
        service_id = module_id+':'+instance_id
        config_types = ['global-system-config', 'alarm']
        super(AlarmGenConfigHandler, self).__init__(service_id, logger,
            discovery_client, keystone_info, rabbitmq_info, config_types)
        self._alarm_plugins = alarm_plugins
        self._inbuilt_alarms = {}
        self._config_ownership = False
        self._inbuilt_alarms_created = False
        self._config = {}
        self._alarm_config = {}
        self._create_inbuilt_alarms_config()
    # end __init__

    def alarm_config(self):
        return self._alarm_config
    # end alarm_config

    def set_config_ownership(self):
        self._config_ownership = True
        # Create inbuilt alarms if not already created.
        self._create_inbuilt_alarms()
    # end set_config_ownership

    def release_config_ownership(self):
        self._config_ownership = False
    # end release_config_ownership

    def _update_alarm_config_table(self, table, alarm_name, alarm_obj):
        try:
            alarm_table = self._alarm_config[table]
        except KeyError:
            self._alarm_config[table] = {}
            alarm_table = self._alarm_config[table]
        finally:
            if not isinstance(alarm_obj, AlarmBase):
                alarm_base_obj = AlarmBase(alarm_obj.alarm_severity)
                alarm_base_obj.set_config(alarm_obj)
                self._alarm_config[table][alarm_name] = alarm_base_obj
            else:
                self._alarm_config[table][alarm_name] = alarm_obj
    # end _update_alarm_config_table

    def _create_inbuilt_alarms_config(self):
        self._inbuilt_alarms = {}
        for table, plugins in self._alarm_plugins.iteritems():
            for extn in plugins[table]:
                alarm_name = camel_case_to_hyphen(
                    extn.obj.__class__.__name__)
                if self._inbuilt_alarms.has_key(alarm_name):
                    uve_keys = self._inbuilt_alarms[alarm_name].get_uve_keys()
                    uve_keys.append(_INVERSE_UVE_MAP[table])
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
                                    operand2=exp['operand2'],
                                    vars=exp.get('vars')))
                            alarm_or_list.append(AlarmAndList(alarm_and_list))
                    desc = ' '.join([l.strip() \
                        for l in extn.obj.__doc__.splitlines()])
                    id_perms = IdPermsType(creator='system', description=desc)
                    kwargs = {'parent_type': 'global-system-config',
                        'fq_name': ['default-global-system-config',
                            alarm_name]}
                    self._inbuilt_alarms[alarm_name] = Alarm(name=alarm_name,
                        uve_keys=[_INVERSE_UVE_MAP[table]],
                        alarm_severity=extn.obj.severity(),
                        alarm_rules=AlarmOrList(alarm_or_list),
                        id_perms=id_perms, **kwargs)
                extn.obj._config = self._inbuilt_alarms[alarm_name]
                self._update_alarm_config_table(table, alarm_name, extn.obj)
    # end _create_inbuilt_alarms_config

    def _create_inbuilt_alarms(self):
        if not self._config_ownership or self._inbuilt_alarms_created:
            return

        try:
            default_gsc = self._config['global-system-config']\
                ['default-global-system-config']
        except KeyError:
            self._logger('No default-global-system-config object. Could not '
                'create inbuilt alarms.', SandeshLevel.SYS_ERR)
            return

        for alarm_name, alarm_obj in self._inbuilt_alarms.iteritems():
            self._logger('Create inbuilt alarm "%s"' % (alarm_name),
                SandeshLevel.SYS_INFO)
            status = self._create_config('alarm', alarm_obj)
            if not status:
                self._logger('Failed to create inbuilt alarm "%s"' %
                    (alarm_name), SandeshLevel.SYS_ERR)
                return
        self._inbuilt_alarms_created = True
    # end _create_inbuilt_alarms

    def _handle_config_update(self, config_type, config_obj):
        self._logger('config update for %s:%s' % (config_type,
            config_obj.name), SandeshLevel.SYS_INFO)
        if not self._config.get(config_type):
            self._config[config_type] = {}
        self._config[config_type][config_obj.name] = config_obj
        if config_type == 'alarm':
            if config_obj.id_perms.creator == 'system':
                return
            for key in config_obj.uve_keys:
                try:
                    table = UVE_MAP[key]
                except KeyError:
                    self._logger('Invalid table name "%s" specified in '
                        'alarm config object "%s"' % (key, config_obj.name),
                        SandeshLevel.SYS_ERR)
                else:
                    self._update_alarm_config_table(table, config_obj.name,
                        config_obj)
    # end _handle_config_update

    def _handle_config_sync(self, config):
        for cfg_type, cfg_obj_list in config.iteritems():
            self._logger('sync for config type "%s"' % (cfg_type),
                SandeshLevel.SYS_INFO)
            for cfg_obj in cfg_obj_list:
                self._handle_config_update(cfg_type, cfg_obj)
        self._create_inbuilt_alarms()
    # end _handle_config_sync


# end class AlarmGenConfigHandler
