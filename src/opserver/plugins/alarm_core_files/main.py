from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class CoreFiles(AlarmBase):
    """This alarm is raised when a core file is generated."""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'NodeStatus.all_core_file_list',
                        'operation': '!=',
                        'operand2': {
                            'json_value': 'null'
                        }
                    },
                    {
                        'operand1': 'NodeStatus.all_core_file_list',
                        'operation': 'size!=',
                        'operand2': {
                            'json_value': '0'
                        }
                    }
                ]
            }
        ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_CRITICAL)
