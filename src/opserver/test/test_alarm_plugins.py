#!/usr/bin/env python

#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import signal
import unittest
import logging
from collections import namedtuple

from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
from alarm_process_status.main import ProcessStatus
from alarm_bgp_connectivity.main import BgpConnectivity
from alarm_xmpp_connectivity.main import XmppConnectivity
from alarm_partial_sysinfo.main import PartialSysinfoCompute,\
    PartialSysinfoAnalytics, PartialSysinfoConfig, PartialSysinfoControl
from alarm_config_incorrect.main import ConfIncorrectCompute,\
    ConfIncorrectAnalytics, ConfIncorrectConfig, ConfIncorrectControl,\
    ConfIncorrectDatabase
from alarm_address_mismatch.main import AddressMismatchControl,\
    AddressMismatchCompute
from alarm_prouter_connectivity.main import ProuterConnectivity
from alarm_vrouter_interface.main import VrouterInterface
from alarm_storage.main import StorageClusterState
from alarm_disk_usage.main import DiskUsage


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

    def test_alarm_address_mismatch(self):
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
                name='ContrailConfig.elements.bgp_router_parameters.address'+\
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
                        'rule': [
                            ('ContrailConfig.elements.bgp_router_parameters' +\
                                '.address not in BgpRouterState.' +\
                                'bgp_router_ip_list', None,
                             [('null', '["10.1.1.1"]', None)])
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
                        'rule': [
                            ('ContrailConfig.elements.bgp_router_parameters' +\
                                '.address not in BgpRouterState.' +\
                                'bgp_router_ip_list', None,
                             [('"10.1.1.1"', 'null', None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='ContrailConfig.elements.bgp_router_parameters.address' +\
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
                        'rule': [
                            ('ContrailConfig.elements.bgp_router_parameters' +\
                                '.address not in BgpRouterState.' +\
                                'bgp_router_ip_list', None,
                             [('"1.1.1.2"', '["10.1.1.1"]', None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='ContrailConfig.elements.bgp_router_parameters.address' +\
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
        self._verify(AddressMismatchControl(), tests)

        tests = [
            TestCase(
                name='ContrailConfig == null',
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
                name='VrouterAgent == null',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={
                        'ContrailConfig': {
                            'elements': {
                                'virtual_router_ip_address': '"10.1.1.1"'
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_ip_address ' +\
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
                        'rule': [
                            ('ContrailConfig.elements.' +\
                                'virtual_router_ip_address not in '
                                'VrouterAgent.self_ip_list', None,
                             [('null', '["10.1.1.1"]', None)])
                        ]
                    },
                    {
                        'rule': [
                            ('ContrailConfig.elements.' +\
                                'virtual_router_ip_address != '
                                'VrouterAgent.control_ip', None,
                             [('null', '"10.1.1.1"', None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='VrouterAgent.self_ip_list == null',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={
                        'VrouterAgent': {
                            'control_ip': '10.1.1.1'
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
                        'rule': [
                            ('ContrailConfig.elements.' +\
                                'virtual_router_ip_address not in '
                                'VrouterAgent.self_ip_list', None,
                             [('"10.1.1.1"', 'null', None)])
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
                        'rule': [
                            ('ContrailConfig.elements.' +\
                                'virtual_router_ip_address != '
                                'VrouterAgent.control_ip', None,
                             [('"10.1.1.1"', 'null', None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='VrouterAgent.control_ip == null, ' +\
                    'VrouterAgent.self_ip_list == null',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={
                        'VrouterAgent': {
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
                        'rule': [
                            ('ContrailConfig.elements.' +\
                                'virtual_router_ip_address not in '
                                'VrouterAgent.self_ip_list', None,
                             [('"10.1.1.1"', 'null', None)])
                        ]
                    },
                    {
                        'rule': [
                            ('ContrailConfig.elements.' +\
                                'virtual_router_ip_address != '
                                'VrouterAgent.control_ip', None,
                             [('"10.1.1.1"', 'null', None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_ip_address ' +\
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
                        'rule': [
                            ('ContrailConfig.elements.' +\
                                'virtual_router_ip_address != '
                                'VrouterAgent.control_ip', None,
                             [('"1.1.1.2"', '"1.1.1.1"', None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_ip_address ' +\
                    'not in VrouterAgent.self_ip_list',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={
                        'VrouterAgent': {
                            'self_ip_list': ['10.1.1.1'],
                            'control_ip': '1.1.1.2'
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
                        'rule': [
                            ('ContrailConfig.elements.' +\
                                'virtual_router_ip_address not in '
                                'VrouterAgent.self_ip_list', None,
                             [('"1.1.1.2"', '["10.1.1.1"]', None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_ip_address ' +\
                    'in VrouterAgent.self_ip_list, ' +\
                    'ContrailConfig.elements.virtual_router_ip_address ' +\
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
        self._verify(AddressMismatchCompute(), tests)
    # end test_alarm_address_mismatch

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
                        'rule': [
                            ('BgpRouterState.num_up_bgp_peer == null',
                             None, [('null', None, None)])
                        ]
                    },
                    {
                        'rule': [
                            ('BgpRouterState.num_up_bgp_peer != ' +\
                                'BgpRouterState.num_bgp_peer',
                             None, [('null', '2', None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='BgpRouterState.num_up_bgp_peer != ' +\
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
                        'rule': [
                            ('BgpRouterState.num_up_bgp_peer != ' +\
                                'BgpRouterState.num_bgp_peer',
                             None, [('1', '2', None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='BgpRouterState.num_up_bgp_peer == ' +\
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
        self._verify(BgpConnectivity(), tests)
    # end test_alarm_bgp_connectivity

    def test_alarm_incorrect_config(self):
        tests = [
            TestCase(
                name='analytics-node: ContrailConfig == null',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={}),
                output=TestOutput(or_list=[
                    {
                        'rule': [
                            ('ContrailConfig == null', None,
                             [('null', None, None)])
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
        self._verify(ConfIncorrectAnalytics(), tests)

        tests = [
            TestCase(
                name='vrouter: ContrailConfig == null',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={}),
                output=TestOutput(or_list=[
                    {
                        'rule': [
                            ('ContrailConfig == null', None,
                             [('null', None, None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='vrouter: ContrailConfig != null',
                input=TestInput(uve_key='ObjectVRouter:host1',
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
        self._verify(ConfIncorrectCompute(), tests)

        tests= [
            TestCase(
                name='config-node: ContrailConfig == null',
                input=TestInput(uve_key='ObjectConfigNode:host1',
                    uve_data={}),
                output=TestOutput(or_list=[
                    {
                        'rule': [
                            ('ContrailConfig == null', None,
                             [('null', None, None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='config-node: ContrailConfig != null',
                input=TestInput(uve_key='ObjectConfigNode:host1',
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
        self._verify(ConfIncorrectConfig(), tests)

        tests = [
            TestCase(
                name='control-node: ContrailConfig == null',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
                    uve_data={}),
                output=TestOutput(or_list=[
                    {
                        'rule': [
                            ('ContrailConfig == null', None,
                             [('null', None, None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='control-node: ContrailConfig != null',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
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
        self._verify(ConfIncorrectControl(), tests)

        tests = [
            TestCase(
                name='database-node: ContrailConfig == null',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={}),
                output=TestOutput(or_list=[
                    {
                        'rule': [
                            ('ContrailConfig == null', None,
                             [('null', None, None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='database-node: ContrailConfig != null',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
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
        self._verify(ConfIncorrectDatabase(), tests)
    # end test_alarm_incorrect_config

    def test_alarm_disk_usage(self):
        tests = [
            TestCase(
                name='DatabaseUsageInfo == null',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='DatabaseUsageInfo.database_usage.' +\
                    'percentage_disk_space_used < threshold',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={
                        'DatabaseUsageInfo': {
                            'database_usage': [
                                {
                                    'disk_space_available_1k': 100663296,
                                    'disk_space_used_1k': 33554432,
                                    'analytics_db_size': 25165824,
                                    'percentage_disk_space_used': 25
                                }
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='DatabaseUsageInfo.database_usage.' +\
                    'percentage_disk_space_used > threshold',
                input=TestInput(uve_key='ObjectDatabaseInfo:host1',
                    uve_data={
                        'DatabaseUsageInfo': {
                            'database_usage': [
                                {
                                    'disk_space_available_1k': 60397978,
                                    'disk_space_used_1k': 73819750,
                                    'analytics_db_size': 25165824,
                                    'percentage_disk_space_used': 55
                                }
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'rule': [
                            ('DatabaseUsageInfo.database_usage.' +\
                                'percentage_disk_space_used > 50',
                             ['DatabaseUsageInfo.database_usage.' +\
                                'disk_space_used_1k',
                              'DatabaseUsageInfo.database_usage.' +\
                                'disk_space_available_1k'],
                             [('55', None, {
                                 'DatabaseUsageInfo.database_usage.' +\
                                     'disk_space_used_1k': '73819750',
                                 'DatabaseUsageInfo.database_usage.' +\
                                     'disk_space_available_1k': '60397978'}
                              )]
                            )
                        ]
                    }
                ])
            )
        ]
        self._verify(DiskUsage(), tests)
    # end test_alarm_disk_usage

    def test_alarm_partial_sysinfo(self):
        tests = [
            TestCase(
                name='CollectorState == null',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='CollectorState.build_info == null',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={
                        'CollectorState': {
                            'self_ip_list': ['10.10.10.1']
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'rule': [('CollectorState.build_info == null',
                            None, [('null', None, None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='CollectorState.build_info != null',
                input=TestInput(uve_key='ObjectCollectorInfo:host1',
                    uve_data={
                        'CollectorState': {
                            'self_ip_list': ['10.10.10.1'],
                            'build_info': '"{"build-number":"100"}"'
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            )
        ]
        self._verify(PartialSysinfoAnalytics(), tests)

        tests = [
            TestCase(
                name='ModuleCpuState == null',
                input=TestInput(uve_key='ObjectConfigNode:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='ModuleCpuState.build_info == null',
                input=TestInput(uve_key='ObjectConfigNode:host2',
                    uve_data={
                        'ModuleCpuState': {
                            'config_node_ip': ['192.168.1.1']
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'rule': [('ModuleCpuState.build_info == null',
                            None, [('null', None, None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='ModuleCpuState.build_info != null',
                input=TestInput(uve_key='ObjectConfigNode:host1',
                    uve_data={
                        'ModuleCpuState': {
                            'config_node_ip': ['10.10.10.1'],
                            'build_info': '"{"build-number":"2729"}"'
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            )
        ]
        self._verify(PartialSysinfoConfig(), tests)

        tests = [
            TestCase(
                name='BgpRouterState == null',
                input=TestInput(uve_key='ObjectBgpRouter:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='BgpRouterState.build_info == null',
                input=TestInput(uve_key='ObjectBgpRouter:host2',
                    uve_data={
                        'BgpRouterState': {
                            'bgp_router_ip_list': ['192.168.1.1']
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'rule': [('BgpRouterState.build_info == null',
                            None, [('null', None, None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='BgpRouterState.build_info != null',
                input=TestInput(uve_key='ObjectBgpRouter:host3',
                    uve_data={
                        'BgpRouterState': {
                            'bgp_router_ip_list': ['10.10.10.1'],
                            'build_info': '"{"build-number":"2121"}"'
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            )
        ]
        self._verify(PartialSysinfoControl(), tests)

        tests = [
            TestCase(
                name='VrouterAgent == null',
                input=TestInput(uve_key='ObjectVRouter:host1',
                    uve_data={}),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='VrouterAgent.build_info == null',
                input=TestInput(uve_key='ObjectVRouter:host2',
                    uve_data={
                        'VrouterAgent': {
                            'control_ip': '192.168.1.1'
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'rule': [('VrouterAgent.build_info == null',
                            None, [('null', None, None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='VrouterAgent.build_info != null',
                input=TestInput(uve_key='ObjectVRouter:host3',
                    uve_data={
                        'VrouterAgent': {
                            'control_ip': '10.10.10.1',
                            'build_info': '"{"build-number":"2829"}"'
                        },
                        'ContrailConfig': {
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            )
        ]
        self._verify(PartialSysinfoCompute(), tests)
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
                        'rule': [('NodeStatus.process_status == null',
                            None, [('null', None, None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='All processes: NodeStatus.process_status.state' +\
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
                name='One process: NodeStatus.process_status.state' +\
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
                        'rule': [
                            ('NodeStatus.process_status.state != "Functional"',
                             ['NodeStatus.process_status.module_id',
                              'NodeStatus.process_status.instance_id'],
                             [('"Non-Functional"', None, {
                                 'NodeStatus.process_status.module_id':\
                                     '"contrail-topology"',
                                 'NodeStatus.process_status.instance_id': '0'})
                             ])
                        ]
                    }
                ])
            ),
            TestCase(
                name='Multiple processes: NodeStatus.process_status.state' +\
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
                        'rule': [
                            ('NodeStatus.process_status.state != "Functional"',
                             ['NodeStatus.process_status.module_id',
                              'NodeStatus.process_status.instance_id'],
                             [('"Non-Functional"', None, {
                                 'NodeStatus.process_status.module_id':\
                                     '"contrail-snmp-collector"',
                                 'NodeStatus.process_status.instance_id': '0'}),
                              ('"Non-Functional"', None, {
                                  'NodeStatus.process_status.module_id':\
                                      '"contrail-snmp-collector"',
                                  'NodeStatus.process_status.instance_id': '1'})
                             ])
                        ]
                    }
                ])
            ),
        ]
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
                        'rule': [('NodeStatus.process_info == null',
                            None, [('null', None, None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='All processes: NodeStatus.process_info.process_state' +\
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
                name='One process: NodeStatus.process_info.process_state != ' +\
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
                        'rule': [
                            ('NodeStatus.process_info.process_state ' +\
                                 '!= "PROCESS_STATE_RUNNING"',
                             ['NodeStatus.process_info.process_name'],
                             [('"PROCESS_STATE_STOPPED"', None, {
                                 'NodeStatus.process_info.process_name':\
                                     '"contrail-topology"'})])
                        ]
                    }
                ])
            ),
            TestCase(
                name='Multiple processes: with process_state != ' +\
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
                        'rule': [('NodeStatus.process_info.process_state ' +\
                            '!= "PROCESS_STATE_RUNNING"',
                            ['NodeStatus.process_info.process_name'],
                            [('"PROCESS_STATE_STOPPED"', None, {
                                'NodeStatus.process_info.process_name':\
                                    '"contrail-topology"'}),
                             ('"PROCESS_STATE_EXITED"', None, {
                                 'NodeStatus.process_info.process_name':\
                                    '"contrail-query-engine"'})
                            ])
                        ]
                    }
                ])
            )
        ]
        self._verify(ProcessStatus(), tests)
    # end test_alarm_process_status

    def test_prouter_connectivity(self):
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
                name='ContrailConfig.elements.virtual_router_refs != null &' +\
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
                        'rule': [
                            ('ContrailConfig.elements.virtual_router_refs ' +\
                                '!= null', None,
                                [('"[{\\"to\\": [\\"tor1\\"]}]"',
                                    None, None)]),
                            ('ProuterData.connected_agent_list == null', None,
                             [('null', None, None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_refs != null &' +\
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
                        'rule': [
                            ('ContrailConfig.elements.virtual_router_refs ' +\
                                '!= null', None,
                                [('"[{\\"to\\": [\\"tor1\\"]}]"', None, None)]
                            ),
                            ('ProuterData.connected_agent_list size!= 1', None,
                             [('["tor1", "tor2"]', None, None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='ContrailConfig.elements.virtual_router_refs != null &' +\
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
        self._verify(ProuterConnectivity(), tests)
    # end test_prouter_connectivity

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
                        'rule': [
                            ('StorageCluster.info_stats.status != 0',
                             ['StorageCluster.info_stats.health_summary'],
                             [('1', None, {
                                 'StorageCluster.info_stats.health_summary':\
                                     '"HEALTH_WARN"'})])
                        ]
                    }
                ])
            ),
        ]
        self._verify(StorageClusterState(), tests)
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
                            'error_intf_list': ['error1']
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'rule': [
                            ('VrouterAgent.error_intf_list != null', None,
                             [('["error1"]', None, None)]),
                            ('VrouterAgent.error_intf_list != []', None,
                             [('["error1"]', None, None)])
                        ]
                    }
                ])
            )
        ]
        self._verify(VrouterInterface(), tests)
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
                        'rule': [
                            ('BgpRouterState.num_up_xmpp_peer == null',
                             None, [('null', None, None)])
                        ]
                    },
                    {
                        'rule': [
                            ('BgpRouterState.num_up_xmpp_peer != ' +\
                                'BgpRouterState.num_xmpp_peer',
                             None, [('null', '3', None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='BgpRouterState.num_up_xmpp_peer != ' +\
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
                        'rule': [
                            ('BgpRouterState.num_up_xmpp_peer != ' +\
                                'BgpRouterState.num_xmpp_peer',
                             None, [('2', '3', None)])
                        ]
                    }
                ])
            ),
            TestCase(
                name='BgpRouterState.num_up_xmpp_peer == ' +\
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
        self._verify(XmppConnectivity(), tests)
    # end test_alarm_xmpp_connectivity

    def _verify(self, plugin, tests):
        for test in tests:
            logging.info('Test: %s' % (test.name))
            exp_or_list = None
            if test.output.or_list is not None:
                exp_or_list = []
                for elt in test.output.or_list:
                    and_list = []
                    for condition, vars, match in elt['rule']:
                        oper1, tmp = condition.split(' ', 1)
                        oper, oper2 = tmp.rsplit(' ', 1)
                        and_list.append(AlarmConditionMatch(
                            condition=AlarmCondition(
                                operation=oper, operand1=oper1,
                                operand2=oper2, vars=vars),
                            match=[AlarmMatch(
                                json_operand1_value=e[0],
                                json_operand2_value=e[1],
                                json_vars=e[2]) for e in match]))
                    exp_or_list.append(AlarmRuleMatch(rule=and_list))
            or_list = plugin.__call__(test.input.uve_key, test.input.uve_data)
            self.assertEqual(exp_or_list, or_list)
    # end _verify


# end class TestAlarmPlugins

def _term_handler(*_):
    raise IntSignal()

if __name__ == '__main__':
    gevent.signal(signal.SIGINT, _term_handler)
    unittest.main(verbosity=2, catchbreak=True)
