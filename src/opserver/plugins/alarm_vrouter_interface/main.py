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
            and_list.append(AlarmElement(\
                rule=AlarmTemplate(oper="!=",
                    operand1=Operand1(keys=["VrouterAgent"]),
                    operand2=Operand2(json_value="null")),
                json_operand1_value=json.dumps({})))
        
            ust = uve_data["VrouterAgent"]

	    if "error_intf_list" in ust:
		and_list.append(AlarmElement(\
		    rule=AlarmTemplate(oper="!=",
			operand1=Operand1(keys=["VrouterAgent","error_intf_list"]),
			operand2=Operand2(json_value="null")),
		    json_operand1_value=json.dumps({})))

		if ust["error_intf_list"] == []:
		    and_list.append(AlarmElement(\
			rule=AlarmTemplate(oper="!=",
			    operand1=Operand1(keys=["VrouterAgent",
                                "error_intf_list"]),
			    operand2=Operand2(json_value="[]")),
			json_operand1_value=json.dumps(\
			    uve_data["VrouterAgent"]["error_intf_list"])))
                    or_list.append(AllOf(all_of=and_list))

        if len(or_list):
            return or_list
        else:
	    return None
