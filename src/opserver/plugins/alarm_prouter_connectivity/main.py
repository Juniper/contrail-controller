#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from  opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json

class ProuterConnectivity(AlarmBase):
    """Prouter connectivity to controlling tor agent does not exist
       we look for non-empty value for connected_agent_list"""

    def __init__(self):
	AlarmBase.__init__(self, AlarmBase.SYS_ERR)

    def __call__(self, uve_key, uve_data):
        #check to see if prouter is configured to be managed by vrouter-agent
        contrail_config = uve_data.get("ContrailConfig", None)
        if not contrail_config or\
           not "elements" in contrail_config:
            return None
        uattr = contrail_config["elements"]
        if not "virtual_router_refs" in uattr:
            return None
        
        or_list = []
        and_list = []
        and_list.append(AlarmConditionMatch(
            condition=AlarmCondition(operation="!=",
                operand1="ContrailConfig.elements.virtual_router_refs",
                operand2="null"),
            match=[AlarmMatch(json_operand1_value=json.dumps(
                uattr["virtual_router_refs"]))]))
        v1 = uve_data.get("ProuterData",None)
        if v1 is not None:
            v2 = v1.get("connected_agent_list",None)
            if v2 is None:
                and_list.append(AlarmConditionMatch(
                    condition=AlarmCondition(operation="==",
                        operand1="ProuterData.connected_agent_list",
                        operand2="null"),
                    match=[AlarmMatch(json_operand1_value="null")]))
                or_list.append(AlarmRuleMatch(rule=and_list))
                return or_list

            if len(v2) != 1:
                and_list.append(AlarmConditionMatch(
                    condition=AlarmCondition(operation="size!=",
                        operand1="ProuterData.connected_agent_list",
                        operand2=json.dumps(1)),
                    match=[AlarmMatch(json_operand1_value=json.dumps(v2))]))
                or_list.append(AlarmRuleMatch(rule=and_list))
                return or_list

	return None
