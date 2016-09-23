from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class PartialSysinfo(AlarmBase):

    def __init__(self):
	AlarmBase.__init__(self, AlarmBase.ALARM_MAJOR)


class PartialSysinfoCompute(PartialSysinfo):
    """System Info Incomplete.
       Basic System Information is absent for this node in VrouterAgent.build_info"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'VrouterAgent.build_info',
                        'operation': '==',
                        'operand2': {
                            'json_value': 'null'
                        }
                    }
                ]
            }
        ]
    }


class PartialSysinfoAnalytics(PartialSysinfo):
    """System Info Incomplete.
       Basic System Information is absent for this node in CollectorState.build_info"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'CollectorState.build_info',
                        'operation': '==',
                        'operand2': {
                            'json_value': 'null'
                        }
                    }
                ]
            }
        ]
    }


class PartialSysinfoConfig(PartialSysinfo):
    """System Info Incomplete.
       Basic System Information is absent for this node in ModuleCpuState.build_info"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'ModuleCpuState.build_info',
                        'operation': '==',
                        'operand2': {
                            'json_value': 'null'
                        }
                    }
                ]
            }
        ]
    }


class PartialSysinfoControl(PartialSysinfo):
    """System Info Incomplete.
       Basic System Information is absent for this node in BgpRouterState.build_info"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'BgpRouterState.build_info',
                        'operation': '==',
                        'operand2': {
                            'json_value': 'null'
                        }
                    }
                ]
            }
        ]
    }
