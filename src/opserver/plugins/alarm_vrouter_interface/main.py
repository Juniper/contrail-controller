from  opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class VrouterInterface(AlarmBase):
    """Vrouter interface(s) in error state.
       VrouterAgent has interfaces in error state in VrouterAgent.error_intf_list"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'VrouterAgent.error_intf_list',
                        'operation': '!=',
                        'operand2': 'null'
                    },
                    {
                        'operand1': 'VrouterAgent.error_intf_list',
                        'operation': '!=',
                        'operand2': '[]'
                    }
                ]
            }
        ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.SYS_WARN)
