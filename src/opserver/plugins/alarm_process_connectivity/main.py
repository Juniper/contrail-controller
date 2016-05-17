from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json

class ProcessConnectivity(AlarmBase):
    """Process(es) reporting as non-functional.
       Process(es) are reporting non-functional components in NodeStatus.process_status"""
    def __init__(self):
	AlarmBase.__init__(self, AlarmBase.SYS_ERR)

    def __call__(self, uve_key, uve_data):
        or_list = [] 

        v1 = uve_data.get("NodeStatus",None)
        if v1 is None:
            return None
        v2 = v1.get("process_status",None)
        if v2 is None:
            and_list = []
            and_list.append(AlarmConditionMatch(
                condition=AlarmCondition(operation="==",
                    operand1="NodeStatus.process_status", operand2="null"),
                match=[AlarmMatch(json_operand1_value="null")]))
            or_list.append(AlarmRuleMatch(rule=and_list))
            return or_list

        proc_status_list = v2
        match_list = []
        for proc_status in proc_status_list:
	    if proc_status["state"] != "Functional":
                match_list.append(AlarmMatch(json_operand1_value=json.dumps(
                    proc_status["state"]), json_vars={
                    "NodeStatus.process_status.module_id":\
                        json.dumps(proc_status["module_id"]),
                    "NodeStatus.process_status.instance_id":\
                        json.dumps(proc_status["instance_id"])}))
        if len(match_list):
            or_list.append(AlarmRuleMatch(rule=[AlarmConditionMatch(
                condition=AlarmCondition(operation="!=",
                    operand1="NodeStatus.process_status.state",
                    operand2=json.dumps("Functional"),
                    vars=["NodeStatus.process_status.module_id",
                        "NodeStatus.process_status.instance_id"]),
                match=match_list)]))
        if len(or_list):
            return or_list
        else:
            return None
