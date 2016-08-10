from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class ProcessConnectivity(AlarmBase):
    """Process(es) reporting as non-functional.
       Process(es) are reporting non-functional components in NodeStatus.process_status"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'NodeStatus.process_status',
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
                        'operand1': 'NodeStatus.process_status.state',
                        'operation': '!=',
                        'operand2': {
                            'json_value': '"Functional"'
                        },
                        'variables': ['NodeStatus.process_status.module_id',
                            'NodeStatus.process_status.instance_id']
                    }
                ]
            }
        ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_CRITICAL)
