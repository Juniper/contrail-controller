from  opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class BgpConnectivity(AlarmBase):
    """BGP peer mismatch.
       Not enough BGP peers are up in BgpRouterState.num_up_bgp_peer"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'BgpRouterState.num_up_bgp_peer',
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
                        'operand1': 'BgpRouterState.num_up_bgp_peer',
                        'operation': '!=',
                        'operand2': {
                            'uve_attribute': 'BgpRouterState.num_bgp_peer'
                        }
                    }
                ]
            }
        ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_MAJOR)
