from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class AddressMismatchCompute(AlarmBase):
    """Compute Node IP Address mismatch.
       Compute Node has IP Address mismatch between ContrailConfig.virtual_router_ip_address
       and Operational State in VrouterAgent"""

    _RULES = {
        'or_list' : [
            {
                'and_list': [
                    {
                        'operand1': 'ContrailConfig.elements.' + \
                            'virtual_router_ip_address',
                        'operation': 'not in',
                        'operand2': {
                            'uve_attribute': 'VrouterAgent.self_ip_list'
                        }
                    }
                ]
            },
            {
                'and_list': [
                    {
                        'operand1': 'ContrailConfig.elements.' + \
                            'virtual_router_ip_address',
                        'operation': '!=',
                        'operand2': {
                            'uve_attribute': 'VrouterAgent.control_ip'
                        }
                    }
                ]
            }
        ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_MAJOR)

       
class AddressMismatchControl(AlarmBase):
    """Control Node IP Address mismatch.
       Control Node has IP Address mismatch between ContrailConfig.bgp_router_parameters.address
       and Operational State in BgpRouterState"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'ContrailConfig.elements.' + \
                            'bgp_router_parameters.address',
                        'operation': 'not in',
                        'operand2': {
                            'uve_attribute':
                                'BgpRouterState.bgp_router_ip_list'
                        }
                    }
                ]
            }
        ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_MAJOR)
