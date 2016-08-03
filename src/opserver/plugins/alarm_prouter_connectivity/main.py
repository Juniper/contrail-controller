#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from  opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class ProuterConnectivity(AlarmBase):
    """Prouter connectivity to controlling tor agent does not exist
       we look for non-empty value for connected_agent_list"""

    _RULES = {
        'or_list': [
            {
                'and_list': [
                    {
                        'operand1': 'ContrailConfig.elements.' + \
                            'virtual_router_refs',
                        'operation': '!=',
                        'operand2': {
                            'json_value': 'null'
                        }
                    },
                    {
                        'operand1': 'ProuterData.connected_agent_list',
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
                        'operand1': 'ContrailConfig.elements.' + \
                            'virtual_router_refs',
                        'operation': '!=',
                        'operand2': {
                            'json_value': 'null'
                        }
                    },
                    {
                        'operand1': 'ProuterData.connected_agent_list',
                        'operation': 'size!=',
                        'operand2': {
                            'json_value': '1'
                        }
                    }
                ]
            }
        ]
    }

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_MAJOR)
