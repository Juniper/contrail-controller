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
        if v1 is not None:
            v2 = v1.get("process_info",None)
        if v2 is None:
            or_list.append(AllOf(all_of=[AlarmElement(\
                rule=AlarmTemplate(oper="==",
                    operand1=Operand1(keys=["NodeStatus","process_info"]),
                    operand2=Operand2(json_value="null")),
                json_operand1_value="null")]))
            return or_list

        value = None
        proc_status_list = v2
        for proc_status in proc_status_list:
            value = str(proc_status)
            if proc_status["process_state"] != "PROCESS_STATE_RUNNING":
		or_list.append(AllOf(all_of=[AlarmElement(\
		    rule=AlarmTemplate(oper="!=",
			operand1=Operand1(\
                            keys=["NodeStatus","process_info","process_state"]),
			operand2=Operand2(json_value=\
                            json.dumps("PROCESS_STATE_RUNNING"))),
		    json_operand1_value=json.dumps(proc_status["process_state"]),
                    json_vars={\
                        "NodeStatus.process_info.process_name":\
                            proc_status["process_name"]})]))
        if len(or_list):
            return or_list
        else:
            return None
