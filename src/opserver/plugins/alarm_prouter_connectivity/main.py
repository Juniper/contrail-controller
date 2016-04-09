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
        if isinstance(uattr,list):
            uattr = uattr[0][0]
        if not "virtual_router_refs" in uattr:
            return None
        
        or_list = []
        v2 = None 
        v1 = uve_data.get("ProuterData",None)
        if v1 is not None:
            v2 = v1.get("connected_agent_list",None)
        if v2 is None:
            or_list.append(AllOf(all_of=[AlarmElement(\
                rule=AlarmTemplate(oper="==",
                    operand1=Operand1(keys=["ProuterData","connected_agent_list"]),
                    operand2=Operand2(json_value="null")),
                json_operand1_value="null")]))
            return or_list

        if not (len(v2) == 1):
            or_list.append(AllOf(all_of=[AlarmElement(\
                rule=AlarmTemplate(oper="size!=",
                    operand1=Operand1(keys=["ProuterData","connected_agent_list"]),
                    operand2=Operand2(json_value=json.dumps(1))),
                json_operand1_value=json.dumps(v2))]))
            return or_list

	return None
