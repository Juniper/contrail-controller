from  opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json

class PhyifBandwidth(AlarmBase):
    """Physical Bandwidth usage anomaly
       Physical Bandwidth usage anomaly as per VrouterStatsAgent.out_bps_ewm or VrouterStatsAgent.in_bps_ewm"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'VrouterStatsAgent.out_bps_ewm.*.sigma',
                        'operation': '>=',
                        'operand2': {
                            'json_value': '2'
                        },
                        'variables': ['VrouterStatsAgent.out_bps_ewm.__key']
                    }
                ]
            },
            {
                'and_list': [
                    {
                        'operand1': 'VrouterStatsAgent.out_bps_ewm.*.sigma',
                        'operation': '<=',
                        'operand2': {
                            'json_value': '-2'
                        },
                        'variables': ['VrouterStatsAgent.out_bps_ewm.__key']
                    }
                ]
            },
            {
                'and_list': [
                    {
                        'operand1': 'VrouterStatsAgent.in_bps_ewm.*.sigma',
                        'operation': '>=',
                        'operand2': {
                            'json_value': '2'
                        },
                        'variables': ['VrouterStatsAgent.in_bps_ewm.__key']
                    }
                ]
            },
            {
                'and_list': [
                    {
                        'operand1': 'VrouterStatsAgent.in_bps_ewm.*.sigma',
                        'operation': '<=',
                        'operand2': {
                            'json_value': '-2'
                        },
                        'variables': ['VrouterStatsAgent.in_bps_ewm.__key']
                    }
                ]
            }
        ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_MINOR)
