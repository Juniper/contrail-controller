from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json

class AddressMismatchCompute(AlarmBase):
    """Compute Node IP Address mismatch.
       Compute Node has IP Address mismatch between ContrailConfig.virtual_router_ip_address
       and Operational State in VrouterAgent"""
    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.SYS_ERR)

    def __call__(self, uve_key, uve_data):
        or_list = []

        if "ContrailConfig" not in uve_data:
            return None

        try:
            uattr = uve_data["ContrailConfig"]["elements"]
        except KeyError:
            return None
        else:
            try:
                lval = json.loads(uattr["virtual_router_ip_address"])
            except KeyError:
                lval = None

        if "VrouterAgent" not in uve_data:
            return None

        try:
            rval1 = uve_data["VrouterAgent"]["self_ip_list"]
        except KeyError:
            rval1 = None

        if not isinstance(rval1,list) or lval not in rval1:
            and_list = []
            and_list.append(AlarmConditionMatch(
                condition=AlarmCondition(operation="not in",
                    operand1="ContrailConfig.elements." + \
                        "virtual_router_ip_address",
                    operand2="VrouterAgent.self_ip_list"),
                match=[AlarmMatch(json_operand1_value=json.dumps(lval),
                    json_operand2_value=json.dumps(rval1))]))
            or_list.append(AlarmRuleMatch(rule=and_list))

        try:
            rval2 = uve_data["VrouterAgent"]["control_ip"]
        except KeyError:
            rval2 = None

        if lval != rval2:
            and_list = []
            and_list.append(AlarmConditionMatch(
                condition=AlarmCondition(operation="!=",
                    operand1="ContrailConfig.elements." + \
                        "virtual_router_ip_address",
                    operand2="VrouterAgent.control_ip"),
                match=[AlarmMatch(json_operand1_value=json.dumps(lval),
                    json_operand2_value=json.dumps(rval2))]))
            or_list.append(AlarmRuleMatch(rule=and_list))

        if len(or_list):
            return or_list
        else:
            return None
       
class AddressMismatchControl(AlarmBase):
    """Control Node IP Address mismatch.
       Control Node has IP Address mismatch between ContrailConfig.bgp_router_parameters.address
       and Operational State in BgpRouterState"""
    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.SYS_ERR)

    def __call__(self, uve_key, uve_data):
        or_list = []

        if "ContrailConfig" not in uve_data:
            return None

        try:
            uattr = uve_data["ContrailConfig"]["elements"]
            bgp_router_param = uattr["bgp_router_parameters"]
        except KeyError:
            return None
        else:
            try:
                lval = json.loads(bgp_router_param)["address"]
            except KeyError:
                lval = None

        if "BgpRouterState" not in uve_data:
            return None

        try:
            rval = uve_data["BgpRouterState"]["bgp_router_ip_list"]
        except KeyError:
            rval = None

        if not isinstance(rval,list) or lval not in rval:
            and_list = []
            and_list.append(AlarmConditionMatch(
                condition=AlarmCondition(operation="not in",
                    operand1="ContrailConfig.elements." + \
                        "bgp_router_parameters.address",
                    operand2="BgpRouterState.bgp_router_ip_list"),
                match=[AlarmMatch(json_operand1_value=json.dumps(lval),
                    json_operand2_value=json.dumps(rval))]))
            or_list.append(AlarmRuleMatch(rule=and_list))

        if len(or_list):
            return or_list

        return None
