from  opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json

class PhyifBandwidth(AlarmBase):
    """Physical Bandwidth usage anomaly
       Physical Bandwidth usage anomaly as per VrouterStatsAgent.out_bps_ewm or VrouterStatsAgent.in_bps_ewm"""

    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.ALARM_MINOR)

    def checkbw(self, stat, sname, vname, thresh):
        alm = []
        match_list_u = []
        match_list_o = []
        for k,val in stat.iteritems(): 
            if val["sigma"] >= thresh:
                match_list_o.append(AlarmMatch(json_operand1_value=json.dumps(
                    val["sigma"]), json_variables={
                    vname:json.dumps(k)}))
            if val["sigma"] <= (-thresh):
                match_list_u.append(AlarmMatch(json_operand1_value=json.dumps(
                    val["sigma"]), json_variables={
                    vname:json.dumps(k)}))

        if len(match_list_o):
            alm.append(AlarmAndList(and_list=[AlarmConditionMatch(
                condition=AlarmCondition(operation=">=",
                    operand1=sname, operand2=AlarmOperand2(
                        json_value=json.dumps(thresh)),
                    variables=[vname]),
                match=match_list_o)]))
        if len(match_list_u):
            alm.append(AlarmAndList(and_list=[AlarmConditionMatch(
                condition=AlarmCondition(operation="<=",
                    operand1=sname, operand2=AlarmOperand2(
                        json_value=json.dumps(-thresh)),
                    variables=[vname]),
                match=match_list_u)]))
        return alm
 
    def __call__(self, uve_key, uve_data):
        or_list = [] 

        v0 = uve_data.get("VrouterStatsAgent")
        if v0 is None:
            return None

        v1 = v0.get("out_bps_ewm",None)
        if not v1:
            v1 = {}
        
        outlist = self.checkbw(v1, "VrouterStatsAgent.out_bps_ewm.*.sigma",
            "VrouterStatsAgent.out_bps_ewm.__key", 2) 

        v2 = v0.get("in_bps_ewm",None)
        if not v2:
            v2 = {}
        
        inlist = self.checkbw(v2, "VrouterStatsAgent.in_bps_ewm.*.sigma",
            "VrouterStatsAgent.in_bps_ewm.__key", 2) 
        
        total_list = outlist + inlist
        if len(total_list):
            return total_list
        else:
            return None

