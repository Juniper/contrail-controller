import json
from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *

class AddressMismatchCompute(AlarmBase):
    """Compute Node IP Address mismatch.
       Compute Node has IP Address mismatch between ContrailConfig.virtual_router_ip_address
       and Operational State in VrouterAgent"""

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_MAJOR)

    def __call__(self, uve_key, uve_data):
        or_list = []

        if 'ContrailConfig' not in uve_data or \
            'elements' not in uve_data['ContrailConfig']:
            return None
        elts = uve_data['ContrailConfig']['elements']
        # If there is discrepancy in ContrailConfig sent by different
        # api-servers, then elements would be list of list
        if isinstance(elts, list):
            elts = elts[0][0]

        try:
            vrouter_ip_address = json.loads(elts['virtual_router_ip_address'])
        except KeyError:
            vrouter_ip_address = None

        if 'VrouterAgent' not in uve_data:
            return None
        try:
            vrouter_agent_self_ip_list = \
                uve_data['VrouterAgent']['self_ip_list']
        except KeyError:
            vrouter_agent_self_ip_list = None

        if not isinstance(vrouter_agent_self_ip_list, list) or \
            vrouter_ip_address not in vrouter_agent_self_ip_list:
            and_list = [AlarmConditionMatch(
                condition=AlarmCondition(operation='not in',
                    operand1='ContrailConfig.elements.'
                        'virtual_router_ip_address',
                    operand2=AlarmOperand2(
                        uve_attribute='VrouterAgent.self_ip_list'),
                    variables = []),
                match=[AlarmMatch(json_operand1_value=json.dumps(
                    vrouter_ip_address), json_operand2_value=json.dumps(
                    vrouter_agent_self_ip_list), json_variables={})])]
            or_list.append(AlarmAndList(and_list))

        try:
            vrouter_agent_control_ip = uve_data['VrouterAgent']['control_ip']
        except KeyError:
            vrouter_agent_control_ip = None

        if vrouter_ip_address != vrouter_agent_control_ip:
            and_list = [AlarmConditionMatch(
                condition=AlarmCondition(operation='!=',
                    operand1='ContrailConfig.elements.'
                        'virtual_router_ip_address',
                    operand2=AlarmOperand2(
                        uve_attribute='VrouterAgent.control_ip'),
                    variables=[]),
                match=[AlarmMatch(json_operand1_value=json.dumps(
                    vrouter_ip_address), json_operand2_value=json.dumps(
                    vrouter_agent_control_ip), json_variables={})])]
            or_list.append(AlarmAndList(and_list))

        if len(or_list):
            return or_list
        return None

# end class AddressMismatchCompute


class AddressMismatchControl(AlarmBase):
    """Control Node IP Address mismatch.
       Control Node has IP Address mismatch between ContrailConfig.bgp_router_parameters.address
       and Operational State in BgpRouterState"""

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_MAJOR)

    def __call__(self, uve_key, uve_data):
        or_list = []

        if 'ContrailConfig' not in uve_data or \
            'elements' not in uve_data['ContrailConfig']:
            return None
        elts = uve_data['ContrailConfig']['elements']
        # If there is discrepancy in ContrailConfig sent by different
        # api-servers, then elements would be list of list
        if isinstance(elts, list):
            elts = elts[0][0]
        try:
            bgp_router_address = \
                json.loads(elts['bgp_router_parameters'])['address']
        except KeyError:
            bgp_router_address = None

        if 'BgpRouterState' not in uve_data:
            return None
        try:
            bgp_router_ip_list = \
                uve_data['BgpRouterState']['bgp_router_ip_list']
        except KeyError:
            bgp_router_ip_list = None

        if not isinstance(bgp_router_ip_list, list) or \
            bgp_router_address not in bgp_router_ip_list:
            and_list = [AlarmConditionMatch(
                condition=AlarmCondition(operation='not in',
                    operand1='ContrailConfig.elements.'
                        'bgp_router_parameters.address',
                    operand2=AlarmOperand2(
                        uve_attribute='BgpRouterState.bgp_router_ip_list'),
                    variables=[]),
                match=[AlarmMatch(json_operand1_value=json.dumps(
                    bgp_router_address), json_operand2_value=json.dumps(
                    bgp_router_ip_list), json_variables={})])]
            or_list.append(AlarmAndList(and_list))

        if len(or_list):
            return or_list
        return None

# end class AddressMismatchControl
