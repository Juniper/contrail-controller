from  opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class VrouterInterface(AlarmBase):
    """Vrouter interface(s) down.
       VrouterAgent has down interfaces in VrouterAgent.down_interface_count"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'VrouterAgent.down_interface_count',
                        'operation': '>=',
                        'operand2': {
                            'json_value': '1'
                        },
                        'variables': ['VrouterAgent.error_intf_list', \
                            'VrouterAgent.no_config_intf_list']
                    },
                ]
            }
        ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_MAJOR)
