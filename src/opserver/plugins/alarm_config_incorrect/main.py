from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class ConfIncorrect(AlarmBase):
    """ContrailConfig missing or incorrect.
       Configuration pushed to Ifmap as ContrailConfig is missing/incorrect"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'ContrailConfig',
                        'operation': '==',
                        'operand2': {
                            'json_value': 'null'
                        }
                    }
                ]
            }
        ]
    }

    def __init__(self, sev = AlarmBase.ALARM_MAJOR):
        AlarmBase.__init__(self, sev)
