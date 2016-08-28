from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class BuildInfo(AlarmBase):

    def __init__(self):
	AlarmBase.__init__(self, AlarmBase.ALARM_MAJOR)

class BuildInfoNode(BuildInfo):
    """NodeStatus build information not present. """

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'NodeStatus.build_info',
                        'operation': '==',
                        'operand2': {
                            'json_value': 'null'
                        }
                    }
                ]
            }
        ]
    }


