from  opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class XmppConnectivity(AlarmBase):
    """XMPP peer mismatch.
       Not enough XMPP peers are up in BgpRouterState.num_up_xmpp_peer"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'BgpRouterState.num_up_xmpp_peer',
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
                        'operand1': 'BgpRouterState.num_up_xmpp_peer',
                        'operation': '!=',
                        'operand2': {
                            'uve_attribute': 'BgpRouterState.num_xmpp_peer'
                        }
                    }
                ]
            }
        ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_MAJOR)
