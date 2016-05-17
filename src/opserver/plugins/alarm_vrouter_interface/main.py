from  opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json

class VrouterInterface(AlarmBase):
    """Vrouter interface(s) in error state.
       VrouterAgent has interfaces in error state in VrouterAgent.error_intf_list"""

    def __init__(self):
	AlarmBase.__init__(self, AlarmBase.SYS_WARN)

    def __call__(self, uve_key, uve_data):
        or_list = []
        if "VrouterAgent" in uve_data:
            and_list = []
            ust = uve_data["VrouterAgent"]
            if "error_intf_list" in ust:
                and_list.append(AlarmConditionMatch(
                    condition=AlarmCondition(operation='!=',
                        operand1='VrouterAgent.error_intf_list',
                        operand2='null'),
                    match=[AlarmMatch(json_operand1_value=json.dumps(
                        ust["error_intf_list"]))]))
                if ust["error_intf_list"] != []:
                    and_list.append(AlarmConditionMatch(
                        condition=AlarmCondition(operation="!=",
                            operand1="VrouterAgent.error_intf_list",
                            operand2=json.dumps([])),
                        match=[AlarmMatch(json_operand1_value=json.dumps(
                            ust["error_intf_list"]))]))
                    or_list.append(AlarmRuleMatch(rule=and_list))

        if len(or_list):
            return or_list
        else:
            return None
