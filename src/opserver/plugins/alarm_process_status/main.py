from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json

class ProcessStatus(AlarmBase):
    """Process Failure.
       NodeMgr reports abnormal status for process(es) in NodeStatus.process_info"""
   
    def __init__(self):
	AlarmBase.__init__(self, AlarmBase.SYS_ERR)

    def __call__(self, uve_key, uve_data):
        or_list = []
       
        v2 = None 
        v1 = uve_data.get("NodeStatus",None)
        if v1 is None:
            return None

        v2 = v1.get("process_info",None)
        if v2 is None:
            or_list.append(AlarmRuleMatch(rule=[AlarmConditionMatch(
                condition=AlarmCondition(operation="==",
                    operand1="NodeStatus.process_info", operand2="null"),
                match=[AlarmMatch(json_operand1_value="null")])]))
            return or_list

        proc_status_list = v2
        match_list = []
        for proc_status in proc_status_list:
            if proc_status["process_state"] != "PROCESS_STATE_RUNNING":
                match_list.append(AlarmMatch(json_operand1_value=json.dumps(
                    proc_status["process_state"]), json_vars={
                    "NodeStatus.process_info.process_name":\
                        json.dumps(proc_status["process_name"])}))
        if len(match_list):
            or_list.append(AlarmRuleMatch(rule=[AlarmConditionMatch(
                condition=AlarmCondition(operation="!=",
                    operand1="NodeStatus.process_info.process_state",
                    operand2=json.dumps("PROCESS_STATE_RUNNING"),
                    vars=["NodeStatus.process_info.process_name"]),
                match=match_list)]))
        if len(or_list):
            return or_list
        else:
            return None
