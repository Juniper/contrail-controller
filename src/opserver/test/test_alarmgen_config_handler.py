#!/usr/bin/env python

#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import signal
import unittest
import mock
import logging
from collections import namedtuple

from vnc_api.gen.resource_client import GlobalSystemConfig, Alarm
from vnc_api.gen.resource_xsd import AlarmExpression, \
    AlarmAndList, AlarmOrList, UveKeysType
from pysandesh.sandesh_logger import SandeshLogger
from opserver.plugins.alarm_base import AlarmBase
from opserver.alarmgen_config_handler import AlarmGenConfigHandler


logging.basicConfig(level=logging.DEBUG,
    format='%(asctime)s %(levelname)s %(message)s')


class TestAlarmGenConfigHandler(unittest.TestCase):

    def setUp(self):
        self.maxDiff = None
    # end setUp

    def tearDown(self):
        pass
    # end tearDown

    def _logger(self, msg, level):
        logging.log(SandeshLogger.get_py_logger_level(level), msg)
    # end _logger

    def _get_config_object(self, config_type, config_dict):
        if config_type == 'global-system-config':
            return GlobalSystemConfig(name=config_dict['name'])
        elif config_type == 'alarm':
            alarm_or_list = []
            for and_list in config_dict['alarm_rules']['or_list']:
                alarm_and_list = []
                for exp in and_list['and_list']:
                    alarm_and_list.append(AlarmExpression(
                        operation=exp['operation'],
                        operand1=exp['operand1'],
                        operand2=exp['operand2'],
                        variables=exp.get('variables')))
                alarm_or_list.append(AlarmAndList(alarm_and_list))
            return Alarm(name=config_dict['name'],
                         uve_keys=UveKeysType(config_dict['uve_keys']),
                         alarm_severity=config_dict['alarm_severity'],
                         alarm_rules=AlarmOrList(alarm_or_list),
                         **config_dict['kwargs'])
        return None
    # end _get_config_object

    def _alarm_config_change_callback(self, alarm_config_change_map):
        logging.info('alarm_config_change_callback: '
            'alarm_config_change_map: %s' % str(alarm_config_change_map))
    # end _alarm_config_change_callback

    def test_handle_config_update(self):
        TestCase = namedtuple('TestCase', ['name', 'input', 'output'])
        TestInput = namedtuple('TestInput', ['config_type', 'fq_name',
            'config', 'operation'])
        TestOutput = namedtuple('TestOutput', ['config_db', 'alarm_config_db',
            'alarm_config_change_map'])

        global_system_config = self._get_config_object('global-system-config',
            {
                'name': 'global-syscfg-default'
            }
        )
        alarm_config1 = self._get_config_object('alarm',
            {
                'name': 'alarm1',
                'uve_keys': ['analytics-node', 'control-node',
                    'vrouter:host1'],
                'alarm_severity': AlarmBase.ALARM_CRITICAL,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'ContrailConfig',
                                    'operation': '==',
                                    'operand2': 'null'
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'global-system-config',
                    'fq_name': ['global-syscfg-default', 'alarm1']
                }
            }
        )
        alarm_config1_1 = self._get_config_object('alarm',
            {
                'name': 'alarm1',
                'uve_keys': ['invalid', 'control-node', 'config-node',
                    'vrouter:host2'],
                'alarm_severity': AlarmBase.ALARM_CRITICAL,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'ContrailConfig',
                                    'operation': '==',
                                    'operand2': 'null'
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'global-system-config',
                    'fq_name': ['global-syscfg-default', 'alarm1']
                }
            }
        )
        alarm_config2 = self._get_config_object('alarm',
            {
                'name': 'alarm1',
                'uve_keys': ['virtual-network', 'invalid'],
                'alarm_severity': AlarmBase.ALARM_MINOR,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'VnStats.tx_pkts',
                                    'operation': '>=',
                                    'operand2': '5000'
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'project',
                    'fq_name': ['default-domain', 'admin', 'alarm1']
                }
            }
        )
        alarm_config2_1 = self._get_config_object('alarm',
            {
                'name': 'alarm1',
                'uve_keys': ['virtual-network'],
                'alarm_severity': AlarmBase.ALARM_MINOR,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'VnStats.tx_pkts',
                                    'operation': '>=',
                                    'operand2': '5000'
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'project',
                    'fq_name': ['default-domain', 'admin', 'alarm1']
                }
            }
        )
        alarm_config3 = self._get_config_object('alarm',
            {
                'name': 'alarm1',
                'uve_keys': ['virtual-network'],
                'alarm_severity': AlarmBase.ALARM_MAJOR,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'VnStats.rx_pkts',
                                    'operation': '>=',
                                    'operand2': '3000'
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'project',
                    'fq_name': ['default-domain', 'demo', 'alarm1']
                }
            }
        )

        tests = [
            TestCase(
                name='CREATE global-system-config:global-syscfg-default',
                input=TestInput(config_type='global-system-config',
                    fq_name='global-system-config:global-syscfg-default',
                    config=global_system_config,
                    operation='CREATE'
                ),
                output=TestOutput(
                    config_db={
                        'global-system-config': {
                            'global-system-config:global-syscfg-default':
                                global_system_config
                        }
                    },
                    alarm_config_db={},
                    alarm_config_change_map={}
                )
            ),
            TestCase(
                name='CREATE global-syscfg-default:alarm1',
                input=TestInput(config_type='alarm',
                    fq_name='global-syscfg-default:alarm1',
                    config=alarm_config1,
                    operation='CREATE'
                ),
                output=TestOutput(
                    config_db={
                        'global-system-config': {
                            'global-system-config:global-syscfg-default':
                                global_system_config
                        },
                        'alarm': {
                            'global-syscfg-default:alarm1': alarm_config1
                        }
                    },
                    alarm_config_db={
                        'ObjectCollectorInfo': {
                            'global-syscfg-default:alarm1':
                                AlarmBase(config=alarm_config1)
                        },
                        'ObjectBgpRouter': {
                            'global-syscfg-default:alarm1':
                                AlarmBase(config=alarm_config1)
                        },
                        'ObjectVRouter:host1': {
                            'global-syscfg-default:alarm1':
                                AlarmBase(config=alarm_config1)
                        }
                    },
                    alarm_config_change_map={
                        'ObjectCollectorInfo': {
                            'global-syscfg-default:alarm1': 'CREATE'
                        },
                        'ObjectBgpRouter': {
                            'global-syscfg-default:alarm1': 'CREATE'
                        },
                        'ObjectVRouter:host1': {
                            'global-syscfg-default:alarm1': 'CREATE'
                        }
                    }
                )
            ),
            TestCase(
                name='UPDATE global-syscfg-default:alarm1 - '
                    'with different uve_keys',
                input=TestInput(config_type='alarm',
                    fq_name='global-syscfg-default:alarm1',
                    config=alarm_config1_1,
                    operation='CREATE'
                ),
                output=TestOutput(
                    config_db={
                        'global-system-config': {
                            'global-system-config:global-syscfg-default':
                                global_system_config
                        },
                        'alarm': {
                            'global-syscfg-default:alarm1': alarm_config1_1
                        }
                    },
                    alarm_config_db={
                        'ObjectBgpRouter': {
                            'global-syscfg-default:alarm1':
                                AlarmBase(config=alarm_config1_1)
                        },
                        'ObjectConfigNode': {
                            'global-syscfg-default:alarm1':
                                AlarmBase(config=alarm_config1_1)
                        },
                        'ObjectVRouter:host2': {
                            'global-syscfg-default:alarm1':
                                AlarmBase(config=alarm_config1_1)
                        }
                    },
                    alarm_config_change_map={
                        'ObjectCollectorInfo': {
                            'global-syscfg-default:alarm1': 'DELETE'
                        },
                        'ObjectBgpRouter': {
                            'global-syscfg-default:alarm1': 'UPDATE'
                        },
                        'ObjectConfigNode': {
                            'global-syscfg-default:alarm1': 'CREATE'
                        },
                        'ObjectVRouter:host1': {
                            'global-syscfg-default:alarm1': 'DELETE'
                        },
                        'ObjectVRouter:host2': {
                            'global-syscfg-default:alarm1': 'CREATE'
                        }
                    }
                )
            ),
            TestCase(
                name='CREATE default-domain:admin:alarm1 - alarm1 already '
                    'exists under global-system-config',
                input=TestInput(config_type='alarm',
                    fq_name='default-domain:admin:alarm1',
                    config=alarm_config2,
                    operation='CREATE'
                ),
                output=TestOutput(
                    config_db={
                        'global-system-config': {
                            'global-system-config:global-syscfg-default':
                                global_system_config
                        },
                        'alarm': {
                            'global-syscfg-default:alarm1': alarm_config1_1,
                            'default-domain:admin:alarm1': alarm_config2
                        }
                    },
                    alarm_config_db={
                        'ObjectBgpRouter': {
                            'global-syscfg-default:alarm1':
                                AlarmBase(config=alarm_config1_1)
                        },
                        'ObjectConfigNode': {
                            'global-syscfg-default:alarm1':
                                AlarmBase(config=alarm_config1_1)
                        },
                        'ObjectVNTable': {
                            'default-domain:admin:alarm1':
                                AlarmBase(config=alarm_config2)
                        },
                        'ObjectVRouter:host2': {
                            'global-syscfg-default:alarm1':
                                AlarmBase(config=alarm_config1_1)
                        }
                    },
                    alarm_config_change_map={
                        'ObjectVNTable': {
                            'default-domain:admin:alarm1': 'CREATE'
                        }
                    }
                )
            ),
            TestCase(
                name='CREATE default-domain:demo:alarm1 - alarm1 already '
                'exists under project default-domain:admin',
                input=TestInput(config_type='alarm',
                    fq_name='default-domain:demo:alarm1',
                    config=alarm_config3,
                    operation='CREATE'
                ),
                output=TestOutput(
                    config_db={
                        'global-system-config': {
                            'global-system-config:global-syscfg-default':
                                global_system_config
                        },
                        'alarm': {
                            'global-syscfg-default:alarm1': alarm_config1_1,
                            'default-domain:admin:alarm1': alarm_config2,
                            'default-domain:demo:alarm1': alarm_config3
                        }
                    },
                    alarm_config_db={
                        'ObjectBgpRouter': {
                            'global-syscfg-default:alarm1':
                                AlarmBase(config=alarm_config1_1)
                        },
                        'ObjectConfigNode': {
                            'global-syscfg-default:alarm1':
                                AlarmBase(config=alarm_config1_1)
                        },
                        'ObjectVNTable': {
                            'default-domain:admin:alarm1':
                                AlarmBase(config=alarm_config2),
                            'default-domain:demo:alarm1':
                                AlarmBase(config=alarm_config3)
                        },
                        'ObjectVRouter:host2': {
                            'global-syscfg-default:alarm1':
                                AlarmBase(config=alarm_config1_1)
                        }
                    },
                    alarm_config_change_map={
                        'ObjectVNTable': {
                            'default-domain:demo:alarm1': 'CREATE'
                        }
                    }
                )
            ),
            TestCase(
                name='UPDATE default-domain:admin:alarm1',
                input=TestInput(config_type='alarm',
                    fq_name='default-domain:admin:alarm1',
                    config=alarm_config2_1,
                    operation='UPDATE'
                ),
                output=TestOutput(
                    config_db={
                        'global-system-config': {
                            'global-system-config:global-syscfg-default':
                                global_system_config
                        },
                        'alarm': {
                            'global-syscfg-default:alarm1': alarm_config1_1,
                            'default-domain:admin:alarm1': alarm_config2_1,
                            'default-domain:demo:alarm1': alarm_config3
                        }
                    },
                    alarm_config_db={
                        'ObjectBgpRouter': {
                            'global-syscfg-default:alarm1':
                                AlarmBase(config=alarm_config1_1)
                        },
                        'ObjectConfigNode': {
                            'global-syscfg-default:alarm1':
                                AlarmBase(config=alarm_config1_1)
                        },
                        'ObjectVNTable': {
                            'default-domain:admin:alarm1':
                                AlarmBase(config=alarm_config2_1),
                            'default-domain:demo:alarm1':
                                AlarmBase(config=alarm_config3)
                        },
                        'ObjectVRouter:host2': {
                            'global-syscfg-default:alarm1':
                                AlarmBase(config=alarm_config1_1)
                        }
                    },
                    alarm_config_change_map={
                        'ObjectVNTable': {
                            'default-domain:admin:alarm1': 'UPDATE'
                        }
                    }
                )
            ),
            TestCase(
                name='DELETE global-syscfg-default:alarm1',
                input=TestInput(config_type='alarm',
                    fq_name='global-syscfg-default:alarm1',
                    config=None,
                    operation='DELETE'
                ),
                output=TestOutput(
                    config_db={
                        'global-system-config': {
                            'global-system-config:global-syscfg-default':
                                global_system_config
                        },
                        'alarm': {
                            'default-domain:admin:alarm1': alarm_config2_1,
                            'default-domain:demo:alarm1': alarm_config3
                        }
                    },
                    alarm_config_db={
                        'ObjectVNTable': {
                            'default-domain:admin:alarm1':
                                AlarmBase(config=alarm_config2_1),
                            'default-domain:demo:alarm1':
                                AlarmBase(config=alarm_config3)
                        }
                    },
                    alarm_config_change_map={
                        'ObjectBgpRouter': {
                            'global-syscfg-default:alarm1': 'DELETE'
                        },
                        'ObjectConfigNode': {
                            'global-syscfg-default:alarm1': 'DELETE'
                        },
                        'ObjectVRouter:host2': {
                            'global-syscfg-default:alarm1': 'DELETE'
                        }
                    }
                )
            ),
            TestCase(
                name='DELETE default-domain:admin:alarm1',
                input=TestInput(config_type='alarm',
                    fq_name='default-domain:admin:alarm1',
                    config=None,
                    operation='DELETE'
                ),
                output=TestOutput(
                    config_db={
                        'global-system-config': {
                            'global-system-config:global-syscfg-default':
                                global_system_config
                        },
                        'alarm': {
                            'default-domain:demo:alarm1': alarm_config3
                        }
                    },
                    alarm_config_db={
                        'ObjectVNTable': {
                            'default-domain:demo:alarm1':
                                AlarmBase(config=alarm_config3)
                        }
                    },
                    alarm_config_change_map={
                        'ObjectVNTable': {
                            'default-domain:admin:alarm1': 'DELETE'
                        }
                    }
                )
            ),
            TestCase(
                name='DELETE global-system-config:global-syscfg-default',
                input=TestInput(config_type='global-system-config',
                    fq_name='global-system-config:global-syscfg-default',
                    config=None,
                    operation='DELETE'
                ),
                output=TestOutput(
                    config_db={
                        'alarm': {
                            'default-domain:demo:alarm1': alarm_config3
                        }
                    },
                    alarm_config_db={
                        'ObjectVNTable': {
                            'default-domain:demo:alarm1':
                                AlarmBase(config=alarm_config3)
                        }
                    },
                    alarm_config_change_map={}
                )
            ),
            TestCase(
                name='DELETE default-domain:demo:alarm1',
                input=TestInput(config_type='alarm',
                    fq_name='default-domain:demo:alarm1',
                    config=None,
                    operation='DELETE'
                ),
                output=TestOutput(
                    config_db={},
                    alarm_config_db={},
                    alarm_config_change_map={
                        'ObjectVNTable': {
                            'default-domain:demo:alarm1': 'DELETE'
                        }
                    }
                )
            ),
            TestCase(
                name='DELETE global-system-config:global-syscfg-default - '
                    'not available',
                input=TestInput(config_type='global-system-config',
                    fq_name='global-system-config:global-syscfg-default',
                    config=None,
                    operation='DELETE'
                ),
                output=TestOutput(
                    config_db={},
                    alarm_config_db={},
                    alarm_config_change_map={}
                )
            ),
            TestCase(
                name='DELETE default-domain:demo:alarm1 - not available',
                input=TestInput(config_type='alarm',
                    fq_name='default-domain:demo:alarm1',
                    config=None,
                    operation='DELETE'
                ),
                output=TestOutput(
                    config_db={},
                    alarm_config_db={},
                    alarm_config_change_map={}
                )
            ),
            TestCase(
                name='UPDATE global-system-config:global-syscfg-default - '
                    'not available (sync case)',
                input=TestInput(config_type='global-system-config',
                    fq_name='global-system-config:global-syscfg-default',
                    config=global_system_config,
                    operation='UPDATE'
                ),
                output=TestOutput(
                    config_db={
                        'global-system-config': {
                            'global-system-config:global-syscfg-default':
                                global_system_config
                        }
                    },
                    alarm_config_db={},
                    alarm_config_change_map={}
                )
            ),
            TestCase(
                name='UPDATE default-domain:demo:alarm1 - not available '
                    '(sync case)',
                input=TestInput(config_type='alarm',
                    fq_name='default-domain:demo:alarm1',
                    config=alarm_config3,
                    operation='UPDATE'
                ),
                output=TestOutput(
                    config_db={
                        'global-system-config': {
                            'global-system-config:global-syscfg-default':
                                global_system_config
                        },
                        'alarm': {
                            'default-domain:demo:alarm1': alarm_config3
                        }
                    },
                    alarm_config_db={
                        'ObjectVNTable': {
                            'default-domain:demo:alarm1':
                                AlarmBase(config=alarm_config3)
                        }
                    },
                    alarm_config_change_map={
                        'ObjectVNTable': {
                            'default-domain:demo:alarm1': 'CREATE'
                        }
                    }
                )
            ),
        ]

        sandesh_instance = mock.MagicMock()
        mock_alarm_config_change_callback = mock.MagicMock()
        api_server_config = {
            'api_server_list': [],
            'api_server_use_ssl': False
        }
        alarmgen_config_handler = AlarmGenConfigHandler(
            sandesh_instance=sandesh_instance, module_id='test',
            instance_id='0', logger=self._logger,
            api_server_config=api_server_config,
            keystone_info=None, rabbitmq_info=None, alarm_plugins={},
            alarm_config_change_callback=mock_alarm_config_change_callback)
        config_db = alarmgen_config_handler.config_db()
        alarm_config_db = alarmgen_config_handler.alarm_config_db()
        for test in tests:
            logging.info('== Test: %s ==' % (test.name))
            alarmgen_config_handler._handle_config_update(
                test.input.config_type, test.input.fq_name, test.input.config,
                test.input.operation)
            # verify that the config_db and alarm_config_db are updated as
            # expected after the call to handle_config_update()
            logging.info('Expected config_db: %s' %
                (str(test.output.config_db)))
            logging.info('Actual config_db: %s' % (str(config_db)))
            self.assertEqual(config_db, test.output.config_db)
            logging.info('Expected alarm_config_db: %s' %
                (str(test.output.alarm_config_db)))
            logging.info('Actual alarm_config_db: %s' % (str(alarm_config_db)))
            self.assertEqual(len(alarm_config_db),
                len(test.output.alarm_config_db))
            for table, alarm_map in alarm_config_db.iteritems():
                self.assertTrue(test.output.alarm_config_db.has_key(table))
                self.assertEqual(len(alarm_map),
                    len(test.output.alarm_config_db[table]))
                for fq_name, alarm_cfg in alarm_map.iteritems():
                    self.assertTrue(test.output.alarm_config_db[table].has_key(
                        fq_name))
                    self.assertEqual(alarm_cfg.config(),
                        test.output.alarm_config_db[table][fq_name].config())
            if test.output.alarm_config_change_map:
                alarmgen_config_handler._alarm_config_change_callback.\
                    assert_called_with(test.output.alarm_config_change_map)
            else:
                alarmgen_config_handler._alarm_config_change_callback.\
                    assert_not_called()
    # end test_handle_config_update


# end class TestAlarmGenConfigHandler


def _term_handler(*_):
    raise IntSignal()


if __name__ == '__main__':
    gevent.signal(signal.SIGINT, _term_handler)
    unittest.main(verbosity=2, catchbreak=True)
