#!/usr/bin/env python

#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import gevent
import json
import signal
import logging
import mock
import unittest

from opserver.sandesh.viz.constants import *
from opserver.overlay_to_underlay_mapper import *
from opserver.overlay_to_underlay_mapper \
    import _OverlayToFlowRecordFieldsNameError, \
    _FlowRecordToUFlowDataFieldsNameError, \
    _UnderlayToUFlowDataFieldsNameError, _QueryError

logging.basicConfig(level=logging.DEBUG,
    format='%(asctime)s %(levelname)s %(message)s')


class TestOverlayToUnderlayMapper(unittest.TestCase):

    def setUp(self):
        self.maxDiff = None
    # end setUp

    def tearDown(self):
        pass
    # end tearDown

    @mock.patch('opserver.overlay_to_underlay_mapper.OpServerUtils.convert_to_utc_timestamp_usec')
    @mock.patch.object(OverlayToUnderlayMapper, '_send_query')
    def test_get_overlay_flow_data_noerror(self, mock_send_query,
                                           mock_convert_to_utc_timestamp_usec):
        flow_record_select_fields = [
            FlowRecordNames[FlowRecordFields.FLOWREC_VROUTER_IP],
            FlowRecordNames[FlowRecordFields.FLOWREC_OTHER_VROUTER_IP],
            FlowRecordNames[FlowRecordFields.FLOWREC_UNDERLAY_SPORT],
            FlowRecordNames[FlowRecordFields.FLOWREC_UNDERLAY_PROTO]
        ]
        input_output_list = [
            {
                # Query with no where clause
                'input': {
                    'overlay_to_underlay_map_query': {
                        'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                        'start_time': 'now-10m', 'end_time': 'now-5m',
                        'select_fields': [U_PROUTER]
                    }
                },
                'output': {
                    'flowrecord_query': {
                        'table': FLOW_TABLE,
                        'start_time': 1423458581000000,
                        'end_time': 1423458591000000,
                        'select_fields': flow_record_select_fields,
                        'where': [], 'dir': 1
                    },
                    'flowrecord_data': []
                }
            },
            {
                # Query with empty where clause.
                # sort_fields, sort, limit should not be used in
                # the FlowRecord query.
                'input': {
                    'overlay_to_underlay_map_query': {
                        'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                        'start_time': 'now-5m', 'end_time': 'now',
                        'select_fields': [U_PROUTER, U_SIP, U_DIP],
                        'where': [],
                        'sort_fields': [U_PROUTER],
                        'sort': 2,
                        'limit': 10
                    }
                },
                'output': {
                    'flowrecord_query': {
                        'table': FLOW_TABLE,
                        'start_time': 1423458471000000,
                        'end_time': 1423458591000000,
                        'select_fields': flow_record_select_fields,
                        'where': [], 'dir': 1
                    },
                    'flowrecord_data': [
                        {'vrouter_ip': '1.1.1.1',
                         'other_vrouter_ip': '2.2.2.2',
                         'underlay_source_port': 1234, 'underlay_proto': 1}
                    ]
                }
            },
            {
                # Simple where clause
                'input': {
                    'overlay_to_underlay_map_query': {
                        'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                        'start_time': 'now-5m', 'end_time': 'now',
                        'select_fields': [U_PROUTER],
                        'where': [
                            [
                                {'name': O_SVN, 'value': 'contrail', 'op': 1}
                            ]
                        ]
                    }
                },
                'output': {
                    'flowrecord_query': {
                        'table': FLOW_TABLE,
                        'start_time': 1423457591000000,
                        'end_time': 1423458591000000,
                        'select_fields': flow_record_select_fields,
                        'where': [
                            [
                                {'name': 'sourcevn', 'value': 'contrail',
                                 'op': 1, 'value2': None, 'suffix': None}
                            ]
                        ],
                        'dir': 1
                    },
                    'flowrecord_data': []
                }
            },
            {
                # Where clause with suffix
                'input': {
                    'overlay_to_underlay_map_query': {
                        'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                        'start_time': 'now-2m', 'end_time': 'now',
                        'select_fields': [U_PROUTER, UFLOW_SIP],
                        'where': [
                            [
                                {'name': O_PROTOCOL, 'value': 6, 'op': 1,
                                 'suffix': {'name': O_SPORT, 'value': 8086,
                                 'op': 1, 'value2': None}}
                            ]
                        ]
                    }
                },
                'output': {
                    'flowrecord_query': {
                        'table': FLOW_TABLE,
                        'start_time': 1423458591000000,
                        'end_time': 1423458591234000,
                        'select_fields': flow_record_select_fields,
                        'where': [
                            [
                                {'name': 'protocol', 'value': 6, 'op': 1,
                                 'value2': None, 'suffix': None},
                                {'name': 'sport', 'value': 8086, 'op': 1,
                                 'value2': None, 'suffix': None}
                            ]
                        ],
                        'dir': 1
                    },
                    'flowrecord_data': [
                        {'vrouter_ip': '2.2.2.2',
                         'other_vrouter_ip': '3.3.3.3',
                         'underlay_source_port': 34567, 'underlay_proto': 2},
                        {'vrouter_ip': '1.2.3.4',
                         'other_vrouter_ip': '1.1.1.1',
                         'underlay_source_port': 12345, 'underlay_proto': 1}
                    ]
                }
            },
            {
                # Where clause with multiple match clauses.
                # 'filter' attribute in the query should not have
                # any impact on the FlowRecord query.
                'input': {
                    'overlay_to_underlay_map_query': {
                        'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                        'start_time': 'now-20m', 'end_time': 'now-10m',
                        'select_fields': [UFLOW_SIP],
                        'where': [
                            [
                                {'name': O_SVN, 'value': 'midokura', 'op': 1},
                                {'name': O_DVN, 'value': 'contrail', 'op': 1,
                                 'suffix': {'name': O_DIP, 'value': '2.2.1.1',
                                 'op': 1}}
                            ],
                            [
                                {'name': O_PROTOCOL, 'value': 6, 'op': 3,
                                 'value2': 17, 'suffix': {'name': O_SPORT,
                                 'value': 8084, 'value2': 8089, 'op': 3}}
                            ]
                        ],
                        'filter': [
                            {'name': UFLOW_PROTOCOL, 'value': 6, 'op': 1}
                        ]
                    }
                },
                'output': {
                    'flowrecord_query': {
                        'table': FLOW_TABLE,
                        'start_time': 1423458580000000,
                        'end_time': 1423458591000000,
                        'select_fields': flow_record_select_fields,
                        'where': [
                            [
                                {'name': 'sourcevn', 'value': 'midokura',
                                 'op': 1, 'value2': None, 'suffix': None},
                                {'name': 'destvn', 'value': 'contrail',
                                 'op': 1, 'value2': None, 'suffix': None},
                                {'name': 'destip', 'value': '2.2.1.1',
                                 'op': 1, 'value2': None, 'suffix': None}
                            ],
                            [
                                {'name': 'protocol', 'value': 6, 'op': 3,
                                 'value2': 17, 'suffix': None},
                                {'name': 'sport', 'value': 8084, 'op': 3,
                                 'value2': 8089, 'suffix': None}
                            ]
                        ],
                        'dir': 1
                    },
                    'flowrecord_data': [
                        {'vrouter_ip': '1.2.3.4',
                         'other_vrouter_ip': '1.1.1.1',
                         'underlay_source_port': 12345, 'underlay_proto': 1}
                    ]
                }
            }
        ]

        mock_send_query.side_effect = \
            [item['output']['flowrecord_data'] for item in input_output_list]
        for item in input_output_list:
            mock_convert_to_utc_timestamp_usec.side_effect = \
                [item['output']['flowrecord_query']['start_time'],
                 item['output']['flowrecord_query']['end_time']]
            overlay_to_underlay_mapper = \
                OverlayToUnderlayMapper(
                    item['input']['overlay_to_underlay_map_query'],
                    None, None, None, None, logging)
            self.assertEqual(item['output']['flowrecord_data'],
                overlay_to_underlay_mapper._get_overlay_flow_data())
            args, _ = overlay_to_underlay_mapper._send_query.call_args
            self.assertEqual(json.loads(args[0]),
                item['output']['flowrecord_query'])
    # end test_get_overlay_flow_data_noerror

    def test_get_overlay_flow_data_raise_exception(self):
        queries = [
            {
                'table': OVERLAY_TO_UNDERLAY_FLOW_MAP, 'start_time': 'now-20m',
                'end_time': 'now', 'select_fields': [UFLOW_PROUTER, UFLOW_SIP],
                'where': [
                    [
                        {'name': 'invalid_name', 'value': 10, 'op': 1}
                    ]
                ]
            },
            {
                'table': OVERLAY_TO_UNDERLAY_FLOW_MAP, 'start_time': 'now-3m',
                'end_time': 'now-1m', 'select_fields': [UFLOW_PROUTER],
                'where': [
                    [
                        # U_PROTOCOL is not valid
                        {'name': O_SVN, 'value': 'contrail', 'op': 1},
                        {'suffix': {'name': O_SPORT, 'value': 3456, 'op': 1},
                         'name': U_PROTOCOL, 'value': 17, 'op': 1}
                    ]
                ]
            },
            {
                'table': OVERLAY_TO_UNDERLAY_FLOW_MAP, 'start_time': 'now-3m',
                'end_time': 'now', 'select_fields': [UFLOW_PROUTER],
                'where': [
                    [
                        # U_SIP is invalid
                        {'suffix': {'name': U_SIP, 'value': '1.1.1.1',
                         'op': 1}, 'name': O_SVN, 'value': 'contrail', 'op': 1}
                    ]
                ]
            },
            {
                'table': OVERLAY_TO_UNDERLAY_FLOW_MAP, 'start_time': 'now-30m',
                'end_time': 'now-10m', 'select_fields': [UFLOW_PROUTER],
                'where': [
                    [
                        {'name': O_SVN, 'value': 'contrail', 'op': 1}
                    ],
                    [
                        # U_SPORT is invalid
                        {'name': O_DVN, 'value': 'nicira', 'op': 1},
                        {'suffix': {'name': U_SPORT, 'value': 14567, 'op': 1},
                         'name': O_PROTOCOL, 'value': 6, 'op': 1}
                    ]
                ]
            },
        ]

        for query in queries:
            overlay_to_underlay_mapper = \
                OverlayToUnderlayMapper(query, None, None, None, None, logging)
            self.assertRaises(_OverlayToFlowRecordFieldsNameError,
               overlay_to_underlay_mapper._get_overlay_flow_data)
    # end test_get_overlay_flow_data_raise_exception

    @mock.patch('opserver.overlay_to_underlay_mapper.OpServerUtils.convert_to_utc_timestamp_usec')
    @mock.patch.object(OverlayToUnderlayMapper, '_send_query')
    def test_get_underlay_flow_data_noerror(self, mock_send_query,
                                            mock_convert_to_utc_timestamp_usec):
        input_output_list = [
            {
                # FlowRecord query returning empty result
                'input': {
                    'overlay_to_underlay_map_query': {
                        'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                        'start_time':  1416275005000000,
                        'end_time': 1416278605000000,
                        'select_fields': [U_PROUTER]
                    },
                    'flow_record_data': []
                },
                'output': {
                    'uflow_data_query': None,
                    'uflow_data': []
                }
            },
            {
                # FlowRecord query returning data
                'input': {
                    'overlay_to_underlay_map_query': {
                        'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                        'start_time':  1416275005000000,
                        'end_time': 1416278605000000,
                        'select_fields': [U_PROUTER],
                    },
                    'flow_record_data': [
                        {'vrouter_ip': '1.2.3.4',
                         'other_vrouter_ip': '1.1.1.1',
                         'underlay_source_port': 12345, 'underlay_proto': 1}
                    ]
                },
                'output': {
                    'uflow_data_query': {
                        'table': 'StatTable.UFlowData.flow',
                        'start_time': 1416275005000000,
                        'end_time': 1416278605000000,
                        'select_fields': [UFLOW_PROUTER],
                        'where': [
                            [
                                {'name': UFLOW_SIP, 'value': '1.2.3.4',
                                 'op': 1, 'value2': None, 'suffix': None},
                                {'name': UFLOW_DIP, 'value': '1.1.1.1',
                                 'op': 1, 'value2': None, 'suffix': None},
                                {'name': UFLOW_PROTOCOL, 'value': 47, 'op': 1,
                                 'value2': None, 'suffix': {
                                 'name': UFLOW_SPORT, 'value': 12345, 'op': 1,
                                 'value2': None, 'suffix': None}}
                            ]
                        ]
                    },
                    'uflow_data': [
                        {UFLOW_PROUTER: '30.10.20.1'},
                        {UFLOW_PROUTER: '10.12.34.56'}
                    ]
                }
            },
            {
                # FlowRecord query response containing None
                'input': {
                    'overlay_to_underlay_map_query': {
                        'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                        'start_time':  1416275005000000,
                        'end_time': 1416278605000000,
                        'select_fields': [U_PROUTER, U_PROTOCOL],
                    },
                    'flow_record_data': [
                        {'vrouter_ip': '1.2.3.4',
                         'other_vrouter_ip': None,
                         'underlay_source_port': 12345, 'underlay_proto': 1}
                    ]
                },
                'output': {
                    'uflow_data_query': None,
                    'uflow_data': []
                }
            },
            {
                # FlowRecord query response (multiple records) - not all
                # records contain None column value
                'input': {
                    'overlay_to_underlay_map_query': {
                        'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                        'start_time':  1416265005000000,
                        'end_time': 1416278605000000,
                        'select_fields': [U_PROUTER, U_PIFINDEX],
                    },
                    'flow_record_data': [
                        {'vrouter_ip': '1.2.3.4',
                         'other_vrouter_ip': '1.1.1.1',
                         'underlay_source_port': None, 'underlay_proto': 1},
                        {'vrouter_ip': '2.2.2.1',
                         'other_vrouter_ip': '1.2.2.1',
                         'underlay_source_port': 3456, 'underlay_proto': 2}
                    ]
                },
                'output': {
                    'uflow_data_query': {
                        'table': 'StatTable.UFlowData.flow',
                        'start_time': 1416265005000000,
                        'end_time': 1416278605000000,
                        'select_fields': [UFLOW_PROUTER, UFLOW_PIFINDEX],
                        'where': [
                            [
                                {'name': UFLOW_SIP, 'value': '2.2.2.1',
                                 'op': 1, 'value2': None, 'suffix': None},
                                {'name': UFLOW_DIP, 'value': '1.2.2.1',
                                 'op': 1, 'value2': None, 'suffix': None},
                                {'name': UFLOW_PROTOCOL, 'value': 17, 'op': 1,
                                 'value2': None, 'suffix': {
                                 'name': UFLOW_SPORT, 'value': 3456, 'op': 1,
                                 'value2': None, 'suffix': None}}
                            ]
                        ],
                    },
                    'uflow_data': [
                        {UFLOW_PROUTER: '10.20.30.1', UFLOW_PIFINDEX: 111},
                        {UFLOW_PROUTER: '192.168.1.1', UFLOW_PIFINDEX: 222}
                    ]
                }
            },
            {
                # Query containing sort_fields, limit and empty filter list
                'input': {
                    'overlay_to_underlay_map_query': {
                        'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                        'start_time': 1516275004000000,
                        'end_time': 1516278604000000,
                        'select_fields': [U_PROUTER, U_PIFINDEX],
                        'sort_fields': [U_PROUTER],
                        'sort': 2,
                        'limit': 10,
                        'filter': []
                    },
                    'flow_record_data': [
                        {'vrouter_ip': '1.2.3.4',
                         'other_vrouter_ip': '1.1.1.1',
                         'underlay_source_port': 12345, 'underlay_proto': 1},
                        {'vrouter_ip': '2.2.2.1',
                         'other_vrouter_ip': '1.2.2.1',
                         'underlay_source_port': 3456, 'underlay_proto': 2}
                    ]
                },
                'output': {
                    'uflow_data_query': {
                        'table': 'StatTable.UFlowData.flow',
                        'start_time': 1516275004000000,
                        'end_time': 1516278604000000,
                        'select_fields': [UFLOW_PROUTER, UFLOW_PIFINDEX],
                        'where': [
                            [
                                {'name': UFLOW_SIP, 'value': '1.2.3.4',
                                 'op': 1, 'value2': None, 'suffix': None},
                                {'name': UFLOW_DIP, 'value': '1.1.1.1',
                                 'op': 1, 'value2': None, 'suffix': None},
                                {'name': UFLOW_PROTOCOL, 'value': 47, 'op': 1,
                                 'value2': None, 'suffix': {
                                 'name': UFLOW_SPORT, 'value': 12345, 'op': 1,
                                 'value2': None, 'suffix': None}}
                            ],
                            [
                                {'name': UFLOW_SIP, 'value': '2.2.2.1',
                                 'op': 1, 'value2': None, 'suffix': None},
                                {'name': UFLOW_DIP, 'value': '1.2.2.1',
                                 'op': 1, 'value2': None, 'suffix': None},
                                {'name': UFLOW_PROTOCOL, 'value': 17, 'op': 1,
                                 'value2': None, 'suffix': {
                                 'name': UFLOW_SPORT, 'value': 3456, 'op': 1,
                                 'value2': None, 'suffix': None}}
                            ]
                        ],
                        'sort_fields': [UFLOW_PROUTER],
                        'sort': 2,
                        'limit': 10,
                        'filter': []
                    },
                    'uflow_data': [
                        {UFLOW_PROUTER: '10.10.10.1', UFLOW_PIFINDEX: 123},
                        {UFLOW_PROUTER: '192.168.1.1', UFLOW_PIFINDEX: 1}
                    ]
                }
            },
            {
                # Query with simple filter clause.
                # Where clause should not have impact on the UFlowData query.
                'input': {
                    'overlay_to_underlay_map_query': {
                        'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                        'start_time': 'now-20m',
                        'end_time': 'now-10m',
                        'select_fields': [U_PROTOCOL, U_DPORT],
                        'where': [
                            [
                                {'name': O_SVN, 'value': 'contrail', 'op': 1}
                            ]
                        ],
                        'limit': 5,
                        'filter': [
                            {'name': U_PROTOCOL, 'value': 6, 'op': 1}
                        ]
                    },
                    'flow_record_data': [
                        {'vrouter_ip': '6.6.6.7',
                         'other_vrouter_ip': '3.3.3.1',
                         'underlay_source_port': 45671, 'underlay_proto': 3},
                    ]
                },
                'output': {
                    'uflow_data_query': {
                        'table': 'StatTable.UFlowData.flow',
                        'start_time': 1423462400000000,
                        'end_time': 1423463400000000,
                        'select_fields': [UFLOW_PROTOCOL, UFLOW_DPORT],
                        'where': [
                            [
                                {'name': UFLOW_SIP, 'value': '6.6.6.7',
                                 'op': 1, 'value2': None, 'suffix': None},
                                {'name': UFLOW_DIP, 'value': '3.3.3.1',
                                 'op': 1, 'value2': None, 'suffix': None},
                                {'name': UFLOW_PROTOCOL, 'value': 17, 'op': 1,
                                 'value2': None, 'suffix': {
                                 'name': UFLOW_SPORT, 'value': 45671, 'op': 1,
                                 'value2': None, 'suffix': None}}
                            ]
                        ],
                        'limit': 5,
                        'filter': [
                            [
                                {'name': UFLOW_PROTOCOL, 'value': 6, 'op': 1}
                            ]
                        ]
                    },
                    'uflow_data': [
                        {UFLOW_PROTOCOL: 6, UFLOW_DPORT: 6788}
                    ]
                }
            },
            {
                # Query with multiple match clauses in the 'filter'.
                'input': {
                    'overlay_to_underlay_map_query': {
                        'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                        'start_time': 'now-20m',
                        'end_time': 'now-10m',
                        'select_fields': [U_PROTOCOL, U_DPORT],
                        'where': [],
                        'limit': 5,
                        'filter': [
                            [
                                {'name': U_PROTOCOL, 'value': 6, 'op': 1}
                            ],
                            [
                                {'name': U_PROTOCOL, 'value': 17, 'op': 1},
                                {'name': U_DPORT, 'value': 8080, 'op': 2}
                            ]
                        ]
                    },
                    'flow_record_data': [
                        {'vrouter_ip': '6.6.6.7',
                         'other_vrouter_ip': '3.3.3.1',
                         'underlay_source_port': 45671, 'underlay_proto': 3},
                    ]
                },
                'output': {
                    'uflow_data_query': {
                        'table': 'StatTable.UFlowData.flow',
                        'start_time': 1423453412300000,
                        'end_time': 1423463400000000,
                        'select_fields': [UFLOW_PROTOCOL, UFLOW_DPORT],
                        'where': [
                            [
                                {'name': UFLOW_SIP, 'value': '6.6.6.7',
                                 'op': 1, 'value2': None, 'suffix': None},
                                {'name': UFLOW_DIP, 'value': '3.3.3.1',
                                 'op': 1, 'value2': None, 'suffix': None},
                                {'name': UFLOW_PROTOCOL, 'value': 17, 'op': 1,
                                 'value2': None, 'suffix': {
                                 'name': UFLOW_SPORT, 'value': 45671, 'op': 1,
                                 'value2': None, 'suffix': None}}
                            ]
                        ],
                        'limit': 5,
                        'filter': [
                            [
                                {'name': UFLOW_PROTOCOL, 'value': 6, 'op': 1}
                            ],
                            [
                                {'name': UFLOW_PROTOCOL, 'value': 17, 'op': 1},
                                {'name': UFLOW_DPORT, 'value': 8080, 'op': 2}
                            ]
                        ]
                    },
                    'uflow_data': [
                        {UFLOW_PROTOCOL: 6, UFLOW_DPORT: 6788}
                    ]
                }
            }
        ]

        mock_send_query.side_effect = [item['output']['uflow_data'] \
            for item in input_output_list \
                if item['output']['uflow_data_query'] is not None]

        for item in input_output_list:
            if not isinstance(
                item['input']['overlay_to_underlay_map_query']['start_time'],
                int):
                mock_convert_to_utc_timestamp_usec.side_effect = \
                    [item['output']['uflow_data_query']['start_time'],
                     item['output']['uflow_data_query']['end_time']]
            overlay_to_underlay_mapper = \
                OverlayToUnderlayMapper(
                    item['input']['overlay_to_underlay_map_query'],
                    None, None, None, None, logging)
            self.assertEqual(item['output']['uflow_data'],
                overlay_to_underlay_mapper._get_underlay_flow_data(
                    item['input']['flow_record_data']))
            if item['output']['uflow_data_query'] is not None:
                args, _ = overlay_to_underlay_mapper._send_query.call_args
                self.assertEqual(json.loads(args[0]),
                    item['output']['uflow_data_query'])
    # end test_get_underlay_flow_data_noerror

    def test_get_underlay_flow_data_raise_exception(self):
        queries = [
            {
                # Invalid field < O_VROUTER > in select_fields.
                'overlay_to_underlay_map_query': {
                    'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                    'start_time':  1416275005000000,
                    'end_time': 1416278605000000,
                    'select_fields': [U_PROUTER, O_VROUTER]
                },
                'flow_record_data': [
                    {'vrouter_ip': '1.2.3.4', 'other_vrouter_ip': '1.1.1.1',
                     'underlay_source_port': 12345, 'underlay_proto': 1}
                ]
            },
            {
                # Invalid sort field < O_SIP >
                'overlay_to_underlay_map_query': {
                    'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                    'start_time':  1414275005000000,
                    'end_time': 1414278605000000,
                    'select_fields': [U_PROUTER],
                    'sort_fields': [O_SIP]
                },
                'flow_record_data': [
                    {'vrouter_ip': '1.2.3.4', 'other_vrouter_ip': '1.1.1.1',
                     'underlay_source_port': 12345, 'underlay_proto': 1},
                    {'vrouter_ip': '2.2.2.1', 'other_vrouter_ip': '1.2.2.1',
                     'underlay_source_port': 3456, 'underlay_proto': 2}
                ]
            },
            {
                # Invalid name < O_DIP > in filter match clause.
                'overlay_to_underlay_map_query': {
                    'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                    'start_time':  1414275005000000,
                    'end_time': 1414278605000000,
                    'select_fields': [U_PROUTER, U_PIFINDEX],
                    'sort_fields': [],
                    'filter': [
                        {'name': O_DIP, 'value': '1.1.1.20', 'op': 1}
                    ]
                },
                'flow_record_data': [
                    {'vrouter_ip': '1.2.3.4', 'other_vrouter_ip': '1.1.1.1',
                     'underlay_source_port': 12345, 'underlay_proto': 1},
                    {'vrouter_ip': '2.2.2.1', 'other_vrouter_ip': '1.2.2.1',
                     'underlay_source_port': 3456, 'underlay_proto': 2}
                ]
            }
        ]

        for query in queries:
            overlay_to_underlay_mapper = \
                OverlayToUnderlayMapper(query['overlay_to_underlay_map_query'],
                    None, None, None, None, logging)
            self.assertRaises(_UnderlayToUFlowDataFieldsNameError,
               overlay_to_underlay_mapper._get_underlay_flow_data,
                    query['flow_record_data'])
    # end test_get_underlay_flow_data_raise_exception

    @mock.patch('opserver.overlay_to_underlay_mapper.OpServerUtils.post_url_http')
    def test_send_query_no_error(self, mock_post_url_http):
        input_output_list = [
            {
                'input': {
                    'analytics_api_ip': '10.10.10.1',
                    'analytics_api_port': 8081,
                    'username': 'admin',
                    'password': 'admin123',
                    'query': {
                        'table': FLOW_TABLE,
                        'start_time': 'now-10m', 'end_time': 'now-5m',
                        'select_fields': ['vrouter_ip', 'other_vouter_ip'],
                        'where': [], 'dir': 1
                    }
                },
                'output': {
                    'query_url': 'http://10.10.10.1:8081/analytics/query',
                    'response': {
                        'value': []
                    }
                }
            },
            {
                'input': {
                    'analytics_api_ip': '192.168.10.1',
                    'analytics_api_port': 8090,
                    'username': 'admin',
                    'password': 'admin123',
                    'query': {
                        'table': 'StatTable.UFlowData.flow',
                        'start_time': 1416275005000000,
                        'end_time': 1416278605000000,
                        'select_fields': [UFLOW_PROUTER],
                        'where': [
                            [
                                {'name': UFLOW_SIP, 'value': '1.2.3.4', 'op': 1,
                                 'value2': None, 'suffix': None},
                                {'name': UFLOW_DIP, 'value': '1.1.1.1', 'op': 1,
                                 'value2': None, 'suffix': None},
                                {'name': UFLOW_PROTOCOL, 'value': 47, 'op': 1,
                                 'value2': None, 'suffix': {'name': UFLOW_SPORT,
                                 'value': 12345, 'op': 1, 'value2': None,
                                 'suffix': None}}
                            ]
                        ]
                    }
                },
                'output': {
                    'query_url': 'http://192.168.10.1:8090/analytics/query',
                    'response': {
                        'value': [
                            {UFLOW_PROUTER: '30.10.22.11'},
                            {UFLOW_PROUTER: '10.12.34.56'}
                        ]
                    }
                }
            }
        ]

        mock_post_url_http.side_effect = \
            [json.dumps(item['output']['response']) \
                for item in input_output_list]
        for item in input_output_list:
            overlay_to_underlay_mapper = \
                OverlayToUnderlayMapper(None, item['input']['analytics_api_ip'],
                    item['input']['analytics_api_port'],
                    item['input']['username'], item['input']['password'],
                    logging)
            self.assertEqual(overlay_to_underlay_mapper._send_query(
                item['input']['query']), item['output']['response']['value'])
            OpServerUtils.post_url_http.assert_called_with(
                item['output']['query_url'], item['input']['query'],
                item['input']['username'], item['input']['password'], True)
    # end test_send_query_no_error

    @mock.patch('opserver.overlay_to_underlay_mapper.OpServerUtils.post_url_http')
    def test_send_query_raise_exception(self, mock_post_url_http):
        queries = [
            {
                # Invalid response (TypeError)
                'analytics_api_ip': '10.10.10.1',
                'analytics_api_port': 8081,
                'query': {
                    'table': FLOW_TABLE,
                    'start_time': 'now-10m', 'end_time': 'now-5m',
                    'select_fields': ['vrouter_ip', 'other_vouter_ip'],
                    'where': [], 'dir': 1
                },
                'response': None
            },
            {
                # Invalid response (ValueError)
                'analytics_api_ip': '10.10.20.1',
                'analytics_api_port': 8084,
                'query': {
                    'table': FLOW_TABLE,
                    'start_time': 'now-10m', 'end_time': 'now',
                    'select_fields': ['other_vouter_ip'],
                    'where': [], 'dir': 1
                },
                'response': '{[]}'
            },
            {
                # Invalid response (KeyError)
                'analytics_api_ip': '192.168.10.1',
                'analytics_api_port': 8090,
                'query': {
                    'table': 'StatTable.UFlowData.flow',
                    'start_time': 1416275005000000,
                    'end_time': 1416278605000000,
                    'select_fields': [UFLOW_PROUTER],
                    'where': [
                        [
                            {'name': UFLOW_SIP, 'value': '1.2.3.4', 'op': 1,
                             'value2': None, 'suffix': None}
                        ]
                    ]
                },
                'response': '{"test": []}'
            }
        ]

        mock_post_url_http.side_effect = \
            [item['response'] for item in queries]
        for item in queries:
            overlay_to_underlay_mapper = \
                OverlayToUnderlayMapper(None, item['analytics_api_ip'],
                    item['analytics_api_port'], None, None, logging)
            self.assertRaises(_QueryError,
                overlay_to_underlay_mapper._send_query, item['query'])
    # end test_send_query_raise_exception

    def test_send_response_no_error(self):
        input_output_list = [
            {
                # Empty uflow_data
                'input': {
                    'overlay_to_underlay_map_query': {
                        'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                        'start_time':  1416275005000000,
                        'end_time': 1416278605000000,
                        'select_fields': [U_PROUTER]
                    },
                    'uflow_data': []
                },
                'output': {
                    'underlay_response': {
                        'value': []
                    }
                }
            },
            {
                # uflow_data with non-empty result
                'input': {
                    'overlay_to_underlay_map_query': {
                        'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                        'start_time': 1516275004000000,
                        'end_time': 1516278604000000,
                        'select_fields': [U_PROUTER, U_PIFINDEX],
                        'sort_fields': [U_PROUTER],
                        'sort': 2,
                        'limit': 10,
                    },
                    'uflow_data': [
                        {UFLOW_PROUTER: '10.10.10.1', UFLOW_PIFINDEX: 123},
                        {UFLOW_PROUTER: '192.168.1.1', UFLOW_PIFINDEX: 1}
                    ]
                },
                'output': {
                    'underlay_response': {
                        'value': [
                            {U_PROUTER: '10.10.10.1', U_PIFINDEX: 123},
                            {U_PROUTER: '192.168.1.1', U_PIFINDEX: 1}
                        ]
                    }
                }
            },
            {
                # uflow_data with columns not requested in the select_fields
                'input': {
                    'overlay_to_underlay_map_query': {
                        'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                        'start_time':  'now-10m', 'end_time': 'now',
                        'select_fields': [U_PROUTER]
                    },
                    'uflow_data': [
                        {UFLOW_PROUTER: '10.10.20.1', UFLOW_PIFINDEX: 123},
                        {UFLOW_PROUTER: '192.169.1.1', UFLOW_PIFINDEX: 1}
                    ]
                },
                'output': {
                    'underlay_response': {
                        'value': [
                            {U_PROUTER: '10.10.20.1'},
                            {U_PROUTER: '192.169.1.1'}
                        ]
                    }
                }
            }
        ]

        for item in input_output_list:
            overlay_to_underlay_mapper = \
                OverlayToUnderlayMapper(
                    item['input']['overlay_to_underlay_map_query'],
                    None, None, None, None, logging)
            self.assertEqual(item['output']['underlay_response'],
                json.loads(overlay_to_underlay_mapper._send_response(
                    item['input']['uflow_data'])))
    # end test_send_response_no_error

    def test_send_response_raise_exception(self):
        input_list = [
            {
                'overlay_to_underlay_map_query': {
                    'table': OVERLAY_TO_UNDERLAY_FLOW_MAP,
                    'start_time':  'now-10m', 'end_time': 'now',
                    'select_fields': [U_PROUTER, 'invalid_field']
                },
                'uflow_data': [
                    {UFLOW_PROUTER: '10.10.10.1'}
                ]
            }
        ]

        for item in input_list:
            overlay_to_underlay_mapper = \
                OverlayToUnderlayMapper(item['overlay_to_underlay_map_query'],
                    None, None, None, None, logging)
            self.assertRaises(_UnderlayToUFlowDataFieldsNameError,
                overlay_to_underlay_mapper._send_response, item['uflow_data'])
    # end test_send_response_raise_exception

    @mock.patch.object(OverlayToUnderlayMapper, '_send_response')
    @mock.patch.object(OverlayToUnderlayMapper, '_get_underlay_flow_data')
    @mock.patch.object(OverlayToUnderlayMapper, '_get_overlay_flow_data')
    def test_process_query(self, mock_get_overlay_flow_data,
                           mock_get_underlay_flow_data,
                           mock_send_response):
        test_data = [
            {
                'flow_record_data': [],
                'uflow_data': [],
                'response': {
                    'value': []
                }
            },
            {
                'flow_record_data': [
                    {'vrouter_ip': '1.1.1.1', 'other_vrouter_ip': '2.2.2.2',
                     'underlay_source_port': 1234, 'underlay_proto': 1}
                ],
                'uflow_data': [
                    {UFLOW_PROUTER: '30.10.20.1'},
                    {UFLOW_PROUTER: '10.12.34.56'}
                ],
                'response': {
                    'value': [
                        {U_PROUTER: '30.10.20.1'},
                        {U_PROUTER: '10.12.34.56'}
                    ]
                }
            }
        ]

        mock_get_overlay_flow_data.side_effect = \
            [item['flow_record_data'] for item in test_data]
        mock_get_underlay_flow_data.side_effect = \
            [item['uflow_data'] for item in test_data]
        mock_send_response.side_effect = \
            [json.dumps(item['response']) for item in  test_data]
        for item in test_data:
            overlay_to_underlay_mapper = \
                OverlayToUnderlayMapper(None, None, None, None, None, logging)
            self.assertEqual(item['response'],
                json.loads(overlay_to_underlay_mapper.process_query()))
            overlay_to_underlay_mapper._get_overlay_flow_data.called_with()
            overlay_to_underlay_mapper._get_underlay_flow_data.called_with(
                item['flow_record_data'])
            overlay_to_underlay_mapper._send_response.called_with(
                item['uflow_data'])
    # end test_process_query

# end class TestOverlayToUnderlayMapper


def _term_handler(*_):
    raise IntSignal()

if __name__ == '__main__':
    gevent.signal(signal.SIGINT, _term_handler)
    unittest.main(verbosity=2, catchbreak=True)
