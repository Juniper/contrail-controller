from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class NodeStatus(AlarmBase):
    """Node Failure.
       NodeStatus UVE not present"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'NodeStatus',
                        'operation': '==',
                        'operand2': {
                            'json_value': 'null'
                        }
                    }
                ]
            }
        ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_CRITICAL)
