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

        v2 = None 
        v1 = uve_data.get("NodeStatus",None)
        if v1 is not None:
            v2 = v1.get("process_status",None)
        if v2 is None:
            or_list.append(AllOf(all_of=[AlarmElement(\
                rule=AlarmTemplate(oper="==",
                    operand1=Operand1(keys=["NodeStatus","process_status"]),
                    operand2=Operand2(json_value="null")),
                json_operand1_value="null")]))
            return or_list

        value = None
	proc_status_list = v2
	for proc_status in proc_status_list:
	    value = str(proc_status)
	    if proc_status["state"] != "Functional":
		or_list.append(AllOf(all_of=[AlarmElement(\
		    rule=AlarmTemplate(oper="!=",
			operand1=Operand1(\
                            keys=["NodeStatus","process_status","state"]),
			operand2=Operand2(json_value=json.dumps("Functional"))),
		    json_operand1_value=json.dumps(proc_status["state"]),
                    json_vars={\
                        "NodeStatus.process_status.module_id":\
                            proc_status["module_id"],
                        "NodeStatus.process_status.instance_id":\
                            proc_status["instance_id"]})])) 
        if len(or_list):
            return or_list
        else:
            return None
