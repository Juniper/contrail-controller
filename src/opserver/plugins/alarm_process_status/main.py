from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class ProcessStatus(AlarmBase):
    """Process Failure.
       NodeMgr reports abnormal status for process(es) in NodeStatus.process_info"""
   
    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'NodeStatus.process_info',
                        'operation': '==',
                        'operand2': {
                            'json_value': 'null'
                        }
                    }
                ]
            },
            {
                'and_list': [
                    {
                        'operand1': 'NodeStatus.process_info.process_state',
                        'operation': '!=',
                        'operand2': {
                            'json_value': '"PROCESS_STATE_RUNNING"'
                        },
                        'variables': ['NodeStatus.process_info.process_name']
                    }
                ]
            }
        ]
    }

    def __init__(self):
	AlarmBase.__init__(self, AlarmBase.ALARM_CRITICAL, at=10, it=10, fec=True,
                fcs=300, fct=4)
