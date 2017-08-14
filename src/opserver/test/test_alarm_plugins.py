#!/usr/bin/env python

#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import signal
import unittest
import logging
import json
from collections import namedtuple

from vnc_api.gen.resource_client import Alarm
from vnc_api.gen.resource_xsd import IdPermsType, AlarmExpression, \
    AlarmOperand2, AlarmAndList, AlarmOrList
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import \
    AlarmOperand2 as SandeshAlarmOperand2, AlarmCondition, AlarmMatch, \
    AlarmConditionMatch, AlarmAndList as SandeshAlarmAndList
from opserver.alarmgen import AlarmProcessor
from opserver.opserver_util import camel_case_to_hyphen

from cfgm_common.exceptions import *
from contrail_alarm import alarm_list

logging.basicConfig(level=logging.DEBUG,
    format='%(asctime)s %(levelname)s %(message)s')

TestCase = namedtuple('TestCase', ['name', 'input', 'output'])
TestInput = namedtuple('TestInput', ['uve_key', 'uve_data'])
TestOutput = namedtuple('TestOutput', ['or_list'])


class TestAlarmPlugins(unittest.TestCase):

    def setUp(self):
        self.maxDiff = None
    # end setUp

    def tearDown(self):
        pass
    # end tearDown

    def get_alarm_config(self, plugin):
        alarm_or_list = []
        if plugin.rules():
            for and_list in plugin.rules()['or_list']:
                alarm_and_list = []
                for exp in and_list['and_list']:
                    alarm_and_list.append(AlarmExpression(
                        operation=exp['operation'], operand1=exp['operand1'],
                        operand2=AlarmOperand2(uve_attribute=
                            exp['operand2'].get('uve_attribute'),
                            json_value=exp['operand2'].get('json_value')),
                        variables=exp.get('variables')))
                alarm_or_list.append(AlarmAndList(alarm_and_list))
        alarm_name = camel_case_to_hyphen(plugin.__class__.__name__)
        kwargs = {'parent_type': 'global-system-config',
            'fq_name': ['default-global-system-config', alarm_name]}
        return Alarm(name=alarm_name, alarm_rules=AlarmOrList(alarm_or_list),
            **kwargs)
    # end get_alarm_config

    def get_alarm_config_by_name(self, alarm_name):

        for alarm in alarm_list:
            if ((alarm['fq_name'])[1] == alarm_name):
                alarm['uuid'] = None
                return Alarm.from_dict(**alarm)
        return None
    # end get_alarm_config_by_name

    def test_alarm_address_mismatch_control(self):
        tests = [
            TestCase(
                name='ContrailConfig == null',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
                    uve_data={
                        'BgpRouterState': {
                            'bgp_router_ip_list': ['10.1.1.1']
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='BgpRouterState == null',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                                'bgp_router_parameters': \
                                    '{"address": "10.1.1.1"}'
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='ContrailConfig.elements.bgp_router_parameters.address'
                    ' == null',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                                'bgp_router_parameters': \
                                    '{"router_type": "control-node"}'
                            }
                        },
                        'BgpRouterState': {
                            'num_bgp_peer': 1,
                            'bgp_router_ip_list': ['10.1.1.1']
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'ContrailConfig.elements.'
                                        'bgp_router_parameters.address',
                                    'operand2': {
                                        'uve_attribute':
                                            'BgpRouterState.bgp_router_ip_list'
                                    },
                                    'operation': 'not in'
                                },
                                'match': [
                                    {
                                        'json_operand1_val': 'null',
                                        'json_operand2_val': '["10.1.1.1"]'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='BgpRouterState.bgp_router_ip_list == null',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                                'bgp_router_parameters': \
                                    '{"address": "10.1.1.1"}'
                            }
                        },
                        'BgpRouterState': {
                            'num_bgp_peer': 1
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'ContrailConfig.elements.'
                                        'bgp_router_parameters.address',
                                    'operand2': {
                                        'uve_attribute':
                                            'BgpRouterState.bgp_router_ip_list'
                                    },
                                    'operation': 'not in'
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"10.1.1.1"',
                                        'json_operand2_val': 'null'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='ContrailConfig.elements.bgp_router_parameters.address'
                    ' not in BgpRouterState.bgp_router_ip_list',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
                    uve_data={
                        'BgpRouterState': {
                            'bgp_router_ip_list': ['10.1.1.1']
                        },
                        'ContrailConfig': {
                            'elements': {
                                'bgp_router_parameters': \
                                    '{"address": "1.1.1.2"}'
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'ContrailConfig.elements.'
                                        'bgp_router_parameters.address',
                                    'operand2': {
                                        'uve_attribute':
                                            'BgpRouterState.bgp_router_ip_list'
                                    },
                                    'operation': 'not in'
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"1.1.1.2"',
                                        'json_operand2_val': '["10.1.1.1"]'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='ContrailConfig.elements.bgp_router_parameters.address'
                    ' in BgpRouterState.bgp_router_ip_list',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
                    uve_data={
                        'BgpRouterState': {
                            'bgp_router_ip_list': ['10.1.1.1']
                        },
                        'ContrailConfig': {
                            'elements': {
                                'bgp_router_parameters': \
                                    '{"address": "10.1.1.1"}'
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            )
        ]
        self._verify(tests, alarm_name="system-defined-address-mismatch-control")
    # end test_alarm_address_mismatch_control

    def test_alarm_address_mismatch_compute(self):
        tests = [
            TestCase(
                name='ContrailConfig.elements.virtual_router_ip_address '
                    '== null',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={
                        'VrouterAgent': {
                            'self_ip_list': ['10.1.1.1'],
                            'control_ip': '10.1.1.1'
                        },
                        'ContrailConfig': {
                            'elements': {
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'ContrailConfig.elements.'
                                        'virtual_router_ip_address',
                                    'operand2': {
                                        'uve_attribute':
                                            'VrouterAgent.control_ip'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': 'null',
                                        'json_operand2_val': '"10.1.1.1"'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='VrouterAgent.control_ip == null',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={
                        'VrouterAgent': {
                            'self_ip_list': ['1.1.1.1', '10.1.1.1'],
                        },
                        'ContrailConfig': {
                            'elements': {
                                'virtual_router_ip_address': '"10.1.1.1"'
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'ContrailConfig.elements.'
                                        'virtual_router_ip_address',
                                    'operand2': {
                                        'uve_attribute':
                                            'VrouterAgent.control_ip'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"10.1.1.1"',
                                        'json_operand2_val': 'null'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_ip_address '
                    '!= VrouterAgent.control_ip',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={
                        'VrouterAgent': {
                            'self_ip_list': ['1.1.1.1', '1.1.1.2'],
                            'control_ip': '1.1.1.1'
                        },
                        'ContrailConfig': {
                            'elements': {
                                'virtual_router_ip_address': '"1.1.1.2"'
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'ContrailConfig.elements.'
                                        'virtual_router_ip_address',
                                    'operand2': {
                                        'uve_attribute':
                                            'VrouterAgent.control_ip'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"1.1.1.2"',
                                        'json_operand2_val': '"1.1.1.1"'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_ip_address '
                    '== VrouterAgent.control_ip',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={
                        'VrouterAgent': {
                            'self_ip_list': ['1.1.1.1', '10.1.1.1'],
                            'control_ip': '10.1.1.1'
                        },
                        'ContrailConfig': {
                            'elements': {
                                'virtual_router_ip_address': '"10.1.1.1"'
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            )
        ]
        self._verify(tests, alarm_name="system-defined-address-mismatch-compute")
    # end test_alarm_address_mismatch_compute

    def test_alarm_bgp_connectivity(self):
        tests = [
            TestCase(
                name='BgpRouterState == null',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='BgpRouterState.num_up_bgp_peer == null',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
                    uve_data={
                        'BgpRouterState': {
                            'num_bgp_peer': 2
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1':
                                        'BgpRouterState.num_up_bgp_peer',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '=='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': 'null'
                                    }
                                ]
                            }
                        ]
                    },
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1':
                                        'BgpRouterState.num_up_bgp_peer',
                                    'operand2': {
                                        'uve_attribute':
                                            'BgpRouterState.num_bgp_peer'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': 'null',
                                        'json_operand2_val': '2'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='BgpRouterState.num_up_bgp_peer != '
                    'BgpRouterState.num_bgp_peer',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
                    uve_data={
                        'BgpRouterState': {
                            'num_bgp_peer': 2,
                            'num_up_bgp_peer': 1
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1':
                                        'BgpRouterState.num_up_bgp_peer',
                                    'operand2': {
                                        'uve_attribute':
                                            'BgpRouterState.num_bgp_peer'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '1',
                                        'json_operand2_val': '2'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='BgpRouterState.num_up_bgp_peer == '
                    'BgpRouterState.num_bgp_peer',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
                    uve_data={
                        'BgpRouterState': {
                            'num_bgp_peer': 2,
                            'num_up_bgp_peer': 2
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            )
        ]
        self._verify(tests, alarm_name="system-defined-bgp-connectivity")
    # end test_alarm_bgp_connectivity

    def test_alarm_incorrect_config(self):
        tests = [
            TestCase(
                name='analytics-node: ContrailConfig == null',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={}),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'ContrailConfig',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '=='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': 'null'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='analytics-node: ContrailConfig != null',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                                'display_name': '"host1"'
                            }
                        }
                    }),
                output=TestOutput(or_list=None)
            )
        ]
        self._verify(tests, alarm_name="system-defined-conf-incorrect")
    # end test_alarm_incorrect_config

    def test_alarm_disk_usage_high(self):
        tests = [
            TestCase(
                name='NodeStatus == null',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='NodeStatus.disk_usage_info == null',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={
                        'NodeStatus': {}
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='NodeStatus.disk_usage_info.*.'
                    'percentage_partition_space_used range [70, 90] - no match',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={
                        'NodeStatus': {
                            'disk_usage_info': {
                                'dev/sda1': {
                                    'partition_space_available_1k': 100663296,
                                    'partition_space_used_1k': 33554432,
                                    'partition_type': 'ext2',
                                    'percentage_partition_space_used': 25
                                }
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='NodeStatus.disk_usage_info.*.'
                    'percentage_partition_space_used range [70, 90]',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={
                        'NodeStatus': {
                            'disk_usage_info': {
                                'dev/sda1': {
                                    'partition_space_available_1k': 2097152,
                                    'partition_space_used_1k': 8388608,
                                    'partition_type': 'ext2',
                                    'percentage_partition_space_used': 80
                                },
                                'dev/sda2': {
                                    'partition_space_available_1k': 524288,
                                    'partition_space_used_1k': 9961472,
                                    'partition_type': 'ext2',
                                    'percentage_partition_space_used': 95
                                },
                                'dev/sda3': {
                                    'partition_space_available_1k': 1048576,
                                    'partition_space_used_1k': 9437184,
                                    'partition_type': 'ext4',
                                    'percentage_partition_space_used': 90
                                },
                                'dev/sda4': {
                                    'partition_space_available_1k': 3145728,
                                    'partition_space_used_1k': 7340032,
                                    'partition_type': 'ext4',
                                    'percentage_partition_space_used': 70
                                },
                                'dev/sda5': {
                                    'partition_space_available_1k': 100663296,
                                    'partition_space_used_1k': 33554432,
                                    'partition_type': 'ext2',
                                    'percentage_partition_space_used': 25
                                }
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'NodeStatus.disk_usage_info'
                                        '.*.percentage_partition_space_used',
                                    'operand2': {
                                        'json_value': '[70, 90]'
                                    },
                                    'operation': 'range',
                                    'variables': [
                                        'NodeStatus.disk_usage_info.__key'
                                    ]
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '90',
                                        'json_variables': {
                                            'NodeStatus.disk_usage_info.__key':
                                                '"dev/sda3"'
                                        }
                                    },
                                    {
                                        'json_operand1_val': '80',
                                        'json_variables': {
                                            'NodeStatus.disk_usage_info.__key':
                                                '"dev/sda1"'
                                        }
                                    },
                                    {
                                        'json_operand1_val': '70',
                                        'json_variables': {
                                            'NodeStatus.disk_usage_info.__key':
                                                '"dev/sda4"'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            )
        ]
        self._verify(tests, alarm_name="system-defined-disk-usage-high")
    # end test_alarm_disk_usage_high

    def test_alarm_disk_usage_critical(self):
        tests = [
            TestCase(
                name='NodeStatus == null',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='NodeStatus.disk_usage_info == null',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={
                        'NodeStatus': {}
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='NodeStatus.disk_usage_info.*.'
                    'percentage_partition_space_used > 90 - no match',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={
                        'NodeStatus': {
                            'disk_usage_info': {
                                'dev/sda1': {
                                    'partition_space_available_1k': 100663296,
                                    'partition_space_used_1k': 33554432,
                                    'partition_type': 'ext2',
                                    'percentage_partition_space_used': 25
                                }
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='NodeStatus.disk_usage_info.*.'
                    'percentage_partition_space_used > 90',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={
                        'NodeStatus': {
                            'disk_usage_info': {
                                'dev/sda1': {
                                    'partition_space_available_1k': 2097152,
                                    'partition_space_used_1k': 8388608,
                                    'partition_type': 'ext2',
                                    'percentage_partition_space_used': 80
                                },
                                'dev/sda2': {
                                    'partition_space_available_1k': 524288,
                                    'partition_space_used_1k': 9961472,
                                    'partition_type': 'ext2',
                                    'percentage_partition_space_used': 95
                                },
                                'dev/sda3': {
                                    'partition_space_available_1k': 1048576,
                                    'partition_space_used_1k': 9437184,
                                    'partition_type': 'ext4',
                                    'percentage_partition_space_used': 90
                                }
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'NodeStatus.disk_usage_info'
                                        '.*.percentage_partition_space_used',
                                    'operand2': {
                                        'json_value': '90'
                                    },
                                    'operation': '>',
                                    'variables': [
                                        'NodeStatus.disk_usage_info.__key'
                                    ]
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '95',
                                        'json_variables': {
                                            'NodeStatus.disk_usage_info.__key':
                                                '"dev/sda2"'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            )
        ]
        self._verify(tests, alarm_name="system-defined-disk-usage-critical")
    # end test_alarm_disk_usage_critical

    def test_alarm_partial_sysinfo(self):
        tests = [
            TestCase(
                name='NodeStatus == null',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='NodeStatus.build_info == null',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={
                        'NodeStatus': {
                            'self_ip_list': ['10.10.10.1']
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'NodeStatus.build_info',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '=='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': 'null'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='NodeStatus.build_info != null',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={
                        'NodeStatus': {
                            'self_ip_list': ['10.10.10.1'],
                            'build_info': '"{"build-number":"100"}"'
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            )
        ]
        self._verify(tests, alarm_name="system-defined-partial-sysinfo")
    # end test_alarm_partial_sysinfo

    def test_alarm_process_connectivity(self):
        tests = [
            TestCase(
                name='NodeStatus == null',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='NodeStatus.process_status == null',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={'NodeStatus': {}}),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'NodeStatus.process_status',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '=='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': 'null'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='All processes: NodeStatus.process_status.state'
                    ' == Functional',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={'NodeStatus': {'process_status': [
                        {
                            'instance_id': '0',
                            'module_id': 'contrail-snmp-collector',
                            'state': 'Functional'
                        },
                        {
                            'instance_id': '0',
                            'module_id': 'contrail-topology',
                            'state': 'Functional'
                        }
                    ]}}
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='One process: NodeStatus.process_status.state'
                    ' != Functional',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={'NodeStatus': {'process_status': [
                        {
                            'instance_id': '0',
                            'module_id': 'contrail-snmp-collector',
                            'state': 'Functional'
                        },
                        {
                            'instance_id': '0',
                            'module_id': 'contrail-topology',
                            'state': 'Non-Functional'
                        }
                    ]}}
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1':
                                        'NodeStatus.process_status.state',
                                    'operand2': {
                                        'json_value': '"Functional"'
                                    },
                                    'operation': '!=',
                                    'variables': [
                                        'NodeStatus.process_status.module_id',
                                        'NodeStatus.process_status.instance_id'
                                    ]
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"Non-Functional"',
                                        'json_variables': {
                                            'NodeStatus.process_status.'
                                                'module_id':
                                                    '"contrail-topology"',
                                            'NodeStatus.process_status.'
                                                'instance_id': '0'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='Multiple processes: NodeStatus.process_status.state'
                    ' != Functional',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={'NodeStatus': {'process_status': [
                        {
                            'instance_id': '0',
                            'module_id': 'contrail-snmp-collector',
                            'state': 'Non-Functional'
                        },
                        {
                            'instance_id': '0',
                            'module_id': 'contrail-topology',
                            'state': 'Functional'
                        },
                        {
                            'instance_id': '1',
                            'module_id': 'contrail-snmp-collector',
                            'state': 'Non-Functional'
                        }
                    ]}}
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1':
                                        'NodeStatus.process_status.state',
                                    'operand2': {
                                        'json_value': '"Functional"'
                                    },
                                    'operation': '!=',
                                    'variables': [
                                        'NodeStatus.process_status.module_id',
                                        'NodeStatus.process_status.instance_id'
                                    ]
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"Non-Functional"',
                                        'json_variables': {
                                            'NodeStatus.process_status.'
                                                'module_id':
                                                    '"contrail-snmp-collector"',
                                            'NodeStatus.process_status.'
                                                'instance_id': '0'
                                        }
                                    },
                                    {
                                        'json_operand1_val': '"Non-Functional"',
                                        'json_variables': {
                                            'NodeStatus.process_status.'
                                                'module_id':
                                                    '"contrail-snmp-collector"',
                                            'NodeStatus.process_status.'
                                                'instance_id': '1'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
        ]
        self._verify(tests, alarm_name="system-defined-process-connectivity")
    # end test_alarm_process_connectivity

    def test_alarm_process_status(self):
        tests = [
            TestCase(
                name='NodeStatus == null',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='NodeStatus.process_info == null',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={'NodeStatus': {}}),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'NodeStatus.process_info',
                                    'operand2': {
                                         'json_value': 'null'
                                    },
                                    'operation': '=='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': 'null'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='All processes: NodeStatus.process_info.process_state'
                    ' == PROCESS_STATE_RUNNING',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={'NodeStatus': {'process_info': [
                        {
                            'process_name': 'contrail-topology',
                            'process_state': 'PROCESS_STATE_RUNNING'
                        },
                        {
                            'process_name': 'contrail-collector',
                            'process_state': 'PROCESS_STATE_RUNNING'
                        }
                    ]}}
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='One process: NodeStatus.process_info.process_state != '
                    'PROCESS_STATE_RUNNING',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={'NodeStatus': {'process_info': [
                        {
                            'process_name': 'contrail-topology',
                            'process_state': 'PROCESS_STATE_STOPPED'
                        },
                        {
                            'process_name': 'contrail-snmp-collector',
                            'process_state': 'PROCESS_STATE_RUNNING'
                        }
                    ]}}
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'NodeStatus.process_info.'
                                        'process_state',
                                    'operand2': {
                                        'json_value':
                                            '"PROCESS_STATE_RUNNING"'
                                    },
                                    'operation': '!=',
                                    'variables': [
                                        'NodeStatus.process_info.process_name'
                                    ]
                                },
                                'match': [
                                    {
                                        'json_operand1_val':
                                            '"PROCESS_STATE_STOPPED"',
                                        'json_variables': {
                                            'NodeStatus.process_info.'
                                                'process_name':
                                                    '"contrail-topology"'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='Multiple processes: with process_state != '
                    'PROCESS_STATE_RUNNING',
                input=TestInput(uve_key='ObjectCollectorInfo:host4',
                    uve_data={'NodeStatus': {'process_info': [
                        {
                            'process_name': 'contrail-topology',
                            'process_state': 'PROCESS_STATE_STOPPED'
                        },
                        {
                            'process_name': 'contrail-snmp-collector',
                            'process_state': 'PROCESS_STATE_RUNNING'
                        },
                        {
                            'process_name': 'contrail-query-engine',
                            'process_state': 'PROCESS_STATE_EXITED'
                        }
                    ]}}
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'NodeStatus.process_info.'
                                        'process_state',
                                    'operand2': {
                                        'json_value':
                                            '"PROCESS_STATE_RUNNING"'
                                    },
                                    'operation': '!=',
                                    'variables': [
                                        'NodeStatus.process_info.process_name'
                                    ]
                                },
                                'match': [
                                    {
                                        'json_operand1_val':
                                            '"PROCESS_STATE_STOPPED"',
                                        'json_variables': {
                                            'NodeStatus.process_info.'
                                                'process_name':
                                                    '"contrail-topology"'
                                        }
                                    },
                                    {
                                        'json_operand1_val':
                                            '"PROCESS_STATE_EXITED"',
                                        'json_variables': {
                                            'NodeStatus.process_info.'
                                                'process_name':
                                                    '"contrail-query-engine"'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            )
        ]
        self._verify(tests, alarm_name="system-defined-process-status")
    # end test_alarm_process_status

    def test_alarm_prouter_connectivity(self):
        tests = [
            TestCase(
                name='ProuterData == null',
                input=TestInput(uve_key='ObjectPRouter:prouter1',
                    uve_data={}
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='ProuterData.connected_agent_list == null',
                input=TestInput(uve_key='ObjectPRouter:prouter1',
                    uve_data={
                        'ProuterData': {}
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_refs == null',
                input=TestInput(uve_key='ObjectPRouter:prouter1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_refs != null &'
                    ' ProuterData.connected_agent_list == null',
                input=TestInput(uve_key='ObjectPRouter:prouter1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                                'virtual_router_refs': '[{"to": ["tor1"]}]'
                            }
                        },
                        'ProuterData': {
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'ContrailConfig.elements.'
                                        'virtual_router_refs',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val':
                                            '[{"to": ["tor1"]}]'
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1':
                                        'ProuterData.connected_agent_list',
                                    'operand2': {
                                        'json_value': '1'
                                    },
                                    'operation': 'size!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': 'null'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_refs != null &'
                    ' ProuterData.connected_agent_list size!= 1',
                input=TestInput(uve_key='ObjectPRouter:prouter1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                                'virtual_router_refs': '[{"to": ["tor1"]}]'
                            }
                        },
                        'ProuterData': {
                            'connected_agent_list': ['tor1', 'tor2']
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'ContrailConfig.elements.'
                                        'virtual_router_refs',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val':
                                            '[{"to": ["tor1"]}]'
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1':
                                        'ProuterData.connected_agent_list',
                                    'operand2': {
                                        'json_value': '1'
                                    },
                                    'operation': 'size!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '["tor1", "tor2"]'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_refs != null &'
                    ' ProuterData.connected_agent_list size= 1',
                input=TestInput(uve_key='ObjectPRouter:prouter1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                                'virtual_router_refs': '[{"to": ["tor1"]}]'
                            }
                        },
                        'ProuterData': {
                            'connected_agent_list': ['tor1']
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            )
        ]
        self._verify(tests, alarm_name="system-defined-prouter-connectivity")
    # end test_alarm_prouter_connectivity

    def test_alarm_prouter_tsn_connectivity(self):
        tests = [
            TestCase(
                name='ProuterData == null',
                input=TestInput(uve_key='ObjectPRouter:prouter1',
                    uve_data={}
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='ProuterData.tsn_agent_list == null',
                input=TestInput(uve_key='ObjectPRouter:prouter1',
                    uve_data={
                        'ProuterData': {}
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_refs == null',
                input=TestInput(uve_key='ObjectPRouter:prouter1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_refs != null &'
                    ' ProuterData.tsn_agent_list == null',
                input=TestInput(uve_key='ObjectPRouter:prouter1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                                'virtual_router_refs': '[{"to": ["tor1"]}]'
                            }
                        },
                        'ProuterData': {
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'ContrailConfig.elements.'
                                        'virtual_router_refs',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val':
                                            '[{"to": ["tor1"]}]'
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1': 'ProuterData.tsn_agent_list',
                                    'operand2': {
                                        'json_value': '1'
                                    },
                                    'operation': 'size!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': 'null'
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1': 'ProuterData.gateway_mode',
                                    'operand2': {
                                        'json_value': '"SERVER"'
                                    },
                                    'operation': '!=',

                                },
                                'match': [
                                    {
                                        'json_operand1_val': 'null'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_refs != null &'
                    ' ProuterData.tsn_agent_list size!= 1',
                input=TestInput(uve_key='ObjectPRouter:prouter1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                                'virtual_router_refs': '[{"to": ["tor1"]}]'
                            }
                        },
                        'ProuterData': {
                            'tsn_agent_list': ['tor1', 'tor2']
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'ContrailConfig.elements.'
                                        'virtual_router_refs',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val':
                                            '[{"to": ["tor1"]}]'
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1': 'ProuterData.tsn_agent_list',
                                    'operand2': {
                                        'json_value': '1'
                                    },
                                    'operation': 'size!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '["tor1", "tor2"]'
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1': 'ProuterData.gateway_mode',
                                    'operand2': {
                                        'json_value': '"SERVER"'
                                    },
                                    'operation': '!=',

                                },
                                'match': [
                                    {
                                        'json_operand1_val': 'null'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_refs != null &'
                    ' ProuterData.tsn_agent_list size = 0 &'
                    ' ProuterData.gateway_mode = "VCPE"',
                input=TestInput(uve_key='ObjectPRouter:prouter1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                                'virtual_router_refs': '[{"to": ["tor1"]}]'
                            }
                        },
                        'ProuterData': {
                            'tsn_agent_list': [],
                            'gateway_mode': 'VCPE'
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'ContrailConfig.elements.'
                                        'virtual_router_refs',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val':
                                            '[{"to": ["tor1"]}]'
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1': 'ProuterData.tsn_agent_list',
                                    'operand2': {
                                        'json_value': '1'
                                    },
                                    'operation': 'size!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '[]'
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1': 'ProuterData.gateway_mode',
                                    'operand2': {
                                        'json_value': '"SERVER"'
                                    },
                                    'operation': '!=',

                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"VCPE"'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_refs != null &'
                    ' ProuterData.tsn_agent_list size = 0 &'
                    ' ProuterData.gateway_mode = "SERVER"',
                input=TestInput(uve_key='ObjectPRouter:prouter1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                                'virtual_router_refs': '[{"to": ["tor1"]}]'
                            }
                        },
                        'ProuterData': {
                            'tsn_agent_list': [],
                            'gateway_mode': 'SERVER'
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_refs != null &'
                    ' ProuterData.tsn_agent_list size= 1',
                input=TestInput(uve_key='ObjectPRouter:prouter1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                                'virtual_router_refs': '[{"to": ["tor1"]}]'
                            }
                        },
                        'ProuterData': {
                            'tsn_agent_list': ['tor1']
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_refs != null &'
                    ' ProuterData.tsn_agent_list size = 1 &'
                    ' ProuterData.gateway_mode = "VCPE"',
                input=TestInput(uve_key='ObjectPRouter:prouter1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                                'virtual_router_refs': '[{"to": ["tor1"]}]'
                            }
                        },
                        'ProuterData': {
                            'tsn_agent_list': ['tor1'],
                            'gateway_mode': 'VCPE'
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_refs != null &'
                    ' ProuterData.tsn_agent_list size = 1 &'
                    ' ProuterData.gateway_mode = "SERVER"',
                input=TestInput(uve_key='ObjectPRouter:prouter1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                                'virtual_router_refs': '[{"to": ["tor1"]}]'
                            }
                        },
                        'ProuterData': {
                            'tsn_agent_list': ['tor1'],
                            'gateway_mode': 'SERVER'
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'ContrailConfig.elements.'
                                        'virtual_router_refs',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val':
                                            '[{"to": ["tor1"]}]'
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1': 'ProuterData.tsn_agent_list',
                                    'operand2': {
                                        'json_value': '0'
                                    },
                                    'operation': 'size!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '["tor1"]'
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1': 'ProuterData.gateway_mode',
                                    'operand2': {
                                        'json_value': '"SERVER"'
                                    },
                                    'operation': '==',

                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"SERVER"'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            )
        ]
        self._verify(tests, alarm_name="system-defined-prouter-tsn-connectivity")
    # end test_alarm_prouter_tsn_connectivity
        
    def test_alarm_storage(self):
        tests = [
            TestCase(
                name='StorageCluster.info_stats.status == 0',
                input=TestInput(uve_key='ObjectStorageClusterTable:cls1',
                    uve_data={
                        'StorageCluster': {
                            'info_stats': [
                                {
                                    'status': 0,
                                    'health_summary': 'HEALTH_OK'
                                }
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='StorageCluster.info_stats.status != 0',
                input=TestInput(uve_key='ObjectStorageClusterTable:cls1',
                    uve_data={
                        'StorageCluster': {
                            'info_stats': [
                                {
                                    'status': 1,
                                    'health_summary': 'HEALTH_WARN'
                                }
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'StorageCluster.info_stats.'
                                        'status',
                                    'operand2': {
                                        'json_value': '0'
                                    },
                                    'operation': '!=',
                                    'variables': [
                                        'StorageCluster.info_stats.'
                                            'health_summary'
                                    ]
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '1',
                                        'json_variables': {
                                            'StorageCluster.info_stats.'
                                            'health_summary': '"HEALTH_WARN"'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
        ]
        self._verify(tests, alarm_name="system-defined-storage-cluster-state")
    # end test_alarm_storage

    def test_alarm_vrouter_interface(self):
        tests = [
            TestCase(
                name='VrouterAgent == null',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='VrouterAgent.error_intf_list == null',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={
                        'VrouterAgent': {
                            'self_ip_list': ['10.1.1.1']
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='VrouterAgent.error_intf_list == []',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={
                        'VrouterAgent': {
                            'self_ip_list': ['10.1.1.1'],
                            'down_interface_count': 0,
                            'error_intf_list': []
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='VrouterAgent.error_intf_list != []',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={
                        'VrouterAgent': {
                            'self_ip_list': ['10.1.1.1'],
                            'down_interface_count': 1,
                            'error_intf_list': ['error1']
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'VrouterAgent.'
                                        'down_interface_count',
                                    'operand2': {
                                        'json_value': '1'
                                    },
                                    'operation': '>=',
                                    'variables':[
                                        'VrouterAgent.error_intf_list',
                                        'VrouterAgent.no_config_intf_list'
                                    ]
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '1',
                                        'json_variables': {
                                            'VrouterAgent.error_intf_list':
                                                '["error1"]',
                                            'VrouterAgent.no_config_intf_list':
                                                'null'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            )
        ]
        self._verify(tests, alarm_name="system-defined-vrouter-interface")
    # end test_alarm_vrouter_interface

    def test_alarm_xmpp_connectivity(self):
        tests = [
            TestCase(
                name='BgpRouterState == null',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='BgpRouterState.num_up_xmpp_peer == null',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
                    uve_data={
                        'BgpRouterState': {
                            'num_xmpp_peer': 3
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'BgpRouterState.'
                                        'num_up_xmpp_peer',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '=='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': 'null'
                                    }
                                ]
                            }
                        ]
                    },
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'BgpRouterState.'
                                        'num_up_xmpp_peer',
                                    'operand2': {
                                        'uve_attribute':
                                            'BgpRouterState.num_xmpp_peer'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': 'null',
                                        'json_operand2_val': '3'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='BgpRouterState.num_up_xmpp_peer != '
                    'BgpRouterState.num_xmpp_peer',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
                    uve_data={
                        'BgpRouterState': {
                            'num_xmpp_peer': 3,
                            'num_up_xmpp_peer': 2
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'BgpRouterState.'
                                        'num_up_xmpp_peer',
                                    'operand2': {
                                        'uve_attribute':
                                            'BgpRouterState.num_xmpp_peer'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '2',
                                        'json_operand2_val': '3'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='BgpRouterState.num_up_xmpp_peer == '
                    'BgpRouterState.num_xmpp_peer',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
                    uve_data={
                        'BgpRouterState': {
                            'num_xmpp_peer': 1,
                            'num_up_xmpp_peer': 1
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            )
        ]
        self._verify(tests, alarm_name="system-defined-xmpp-connectivity")
    # end test_alarm_xmpp_connectivity

    def test_alarm_phyif_bandwidth(self):
        tests = [
            TestCase(
                name='VrouterStatsAgent == null',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='VrouterStatsAgent.in_bps_ewm',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={
                        'VrouterStatsAgent': {
                            'in_bps_ewm': {
                                'p4p1': { 'sigma': 2.11,
                                          'samples': 10,
                                          'stddev': 33238,
                                          'mean': 16619 }
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'VrouterStatsAgent.'
                                        'in_bps_ewm.*.sigma',
                                    'operand2': {
                                        'json_value': '2'
                                    },
                                    'operation': '>=',
                                    'variables': [
                                        'VrouterStatsAgent.in_bps_ewm.__key'
                                    ]
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '2.11',
                                        'json_variables': {
                                            'VrouterStatsAgent.in_bps_ewm.'
                                                '__key': '"p4p1"'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='VrouterStatsAgent.out_bps_ewm',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={
                        'VrouterStatsAgent': {
                            'in_bps_ewm': {
                                'p4p1': { 'sigma': 1.11,
                                          'samples': 10,
                                          'stddev': 33238,
                                          'mean': 16619 }
                            },
                            'out_bps_ewm': {
                                'p4p1': { 'sigma': -2.11,
                                          'samples': 10,
                                          'stddev': 3238,
                                          'mean': 16619 }
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'VrouterStatsAgent.'
                                        'out_bps_ewm.*.sigma',
                                    'operand2': {
                                        'json_value': '-2'
                                    },
                                    'operation': '<=',
                                    'variables': [
                                        'VrouterStatsAgent.out_bps_ewm.__key'
                                    ]
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '-2.11',
                                        'json_variables': {
                                            'VrouterStatsAgent.out_bps_ewm.'
                                                '__key': '"p4p1"'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='VrouterStatsAgent normal',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={
                        'VrouterStatsAgent': {
                            'in_bps_ewm': {
                                'p4p1': { 'sigma': 1.11,
                                          'samples': 10,
                                          'stddev': 33238,
                                          'mean': 16619 }
                            },
                            'out_bps_ewm': {
                                'p4p1': { 'sigma': -0.11,
                                          'samples': 10,
                                          'stddev': 3238,
                                          'mean': 16619 }
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
        ]
        self._verify(tests, alarm_name="system-defined-phyif-bandwidth")
    # end test_alarm_phyif_bandwidth

    def test_alarm_node_status(self):
        tests = [
            TestCase(
                name='NodeStatus == null',
                input=TestInput(uve_key='ObjectCollectorInfo:host2',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                                'fq_name': ['global-sys-config', 'host2']
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'NodeStatus',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '=='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': 'null'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='NodeStatus != null',
                input=TestInput(uve_key='ObjectCollectorInfo:host2',
                    uve_data={
                        'NodeStatus': {
                            'process_info': [
                                {
                                    'name': 'contrail-topology',
                                    'state': 'RUNNING'
                                }
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            )
        ]
        self._verify(tests, alarm_name="system-defined-node-status")
    # end test_alarm_node_status

    def test_alarm_core_files(self):
        tests = [
            TestCase(
                name='NodeStatus == null',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='NodeStatus.all_core_file_list == []',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={
                        'NodeStatus': {
                            'all_core_file_list': []
                        },
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='NodeStatus.all_core_file_list != null 1 core',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={
                        'NodeStatus': {
                            'all_core_file_list': ['core-file1']
                        },
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'NodeStatus.all_core_file_list',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '["core-file1"]',
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1': 'NodeStatus.all_core_file_list',
                                    'operand2': {
                                        'json_value': '0'
                                    },
                                    'operation': 'size!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '["core-file1"]',
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='NodeStatus.all_core_file_list != null 2 cores',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={
                        'NodeStatus': {
                            'all_core_file_list': ['core-file1', 'core-file2']
                        },
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'NodeStatus.all_core_file_list',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val':
                                            '["core-file1", "core-file2"]'
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1': 'NodeStatus.all_core_file_list',
                                    'operand2': {
                                        'json_value': '0'
                                    },
                                    'operation': 'size!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val':
                                            '["core-file1", "core-file2"]'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            )
        ]
        self._verify(tests, alarm_name="system-defined-core-files")
    # end test_alarm_core_files

    def test_alarm_pending_cassandra_compaction_tasks(self):
        tests = [
            TestCase(
                name='CassandraStatusData == null',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='CassandraStatusData.cassandra_compaction_task == null',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={
                        'CassandraStatusData': {}
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='CassandraStatusData.cassandra_compaction_task.'
                     'pending_compaction_tasks < threshold',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={
                        'CassandraStatusData': {
                            'cassandra_compaction_task': [
                                {
                                    'pending_compaction_tasks': 10,
                                }
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='CassandraStatusData.cassandra_compaction_task.'
                     'pending_compaction_tasks == threshold',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={
                        'CassandraStatusData': {
                            'cassandra_compaction_task': [
                                {
                                    'pending_compaction_tasks': '300',
                                }
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'CassandraStatusData.'
                                        'cassandra_compaction_task.'
                                        'pending_compaction_tasks',
                                    'operand2': {
                                        'json_value': '300'
                                    },
                                    'operation': '>='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '300'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='CassandraStatusData.cassandra_compaction_task.'
                     'pending_compaction_tasks > threshold',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={
                        'CassandraStatusData': {
                            'cassandra_compaction_task': [
                                {
                                    'pending_compaction_tasks': '320',
                                }
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'CassandraStatusData.'
                                        'cassandra_compaction_task.'
                                        'pending_compaction_tasks',
                                    'operand2': {
                                        'json_value': '300'
                                    },
                                    'operation': '>='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '320'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            )
        ]
        self._verify(tests, alarm_name="system-defined-pending-cassandra-compaction-tasks")
    # end test_alarm_pending_cassandra_compaction_tasks

    def test_alarm_package_version(self):
        tests = [

            TestCase(
                name='NodeStatus == null',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='NodeStatus.running_package_version == ' +\
                     'NodeStatus.installed_package_version',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={
                        'NodeStatus': {
                            'running_package_version': '"3.1.0.0-2740"',
                            'installed_package_version': '"3.1.0.0-2740"'
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='NodeStatus.running_package_version != ' +\
                     'NodeStatus.installed_package_version',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={
                        'NodeStatus': {
                            'running_package_version': '"3.1.0.0-2740"',
                            'installed_package_version': '"3.1.0.0-18"'
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'NodeStatus.running_package_version',
                                    'operand2': {
                                        'uve_attribute':
                                            'NodeStatus.installed_package_version'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"3.1.0.0-2740"',
                                        'json_operand2_val': '"3.1.0.0-18"'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            )
        ]
        self._verify(tests, alarm_name="system-defined-package-version-mismatch")
    # end test_alarm_package_version

    def _verify(self, tests, plugin=None, alarm_name=None):
        for test in tests:
            name = alarm_name or plugin.__class__.__name__
            logging.info('Test: [%s]: [%s]' % (name, test.name))
            exp_or_list = None
            if test.output.or_list is not None:
                exp_or_list = []
                for elt in test.output.or_list:
                    and_list = []
                    for and_elt in elt['and_list']:
                        cond = and_elt['condition']
                        match = and_elt['match']
                        and_list.append(AlarmConditionMatch(
                            condition=AlarmCondition(
                                operation=cond['operation'],
                                operand1=cond['operand1'],
                                operand2=SandeshAlarmOperand2(
                                    uve_attribute=cond['operand2'].get(
                                        'uve_attribute'),
                                    json_value=cond['operand2'].get(
                                        'json_value')),
                                variables=cond.get('variables') or []),
                            match=[AlarmMatch(
                                json_operand1_value=m['json_operand1_val'],
                                json_operand2_value=m.get('json_operand2_val'),
                                json_variables=m.get('json_variables') or {}) \
                                    for m in match]))
                    exp_or_list.append(SandeshAlarmAndList(and_list))
            if ((plugin != None) and hasattr(plugin, '__call__')):
                or_list = plugin.__call__(test.input.uve_key,
                    test.input.uve_data)
            else:
                if (plugin != None):
                    alarm_cfg = self.get_alarm_config(plugin)
                else:
                    alarm_cfg = self.get_alarm_config_by_name(alarm_name)
                alarm_processor = AlarmProcessor(logging)
                or_list = alarm_processor._evaluate_uve_for_alarms(
                    alarm_cfg, test.input.uve_key, test.input.uve_data)
            logging.info('exp_or_list: %s' % (str(exp_or_list)))
            logging.info('or_list: %s' % (str(or_list)))
            self.assertEqual(exp_or_list, or_list)
    # end _verify


# end class TestAlarmPlugins

def _term_handler(*_):
    raise IntSignal()

if __name__ == '__main__':
    gevent.signal(signal.SIGINT, _term_handler)
    unittest.main(verbosity=2, catchbreak=True)
