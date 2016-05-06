from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json

class PartialSysinfo(AlarmBase):

    def __init__(self):
	AlarmBase.__init__(self, AlarmBase.SYS_WARN)
 
    def __call__(self, uve_key, uve_data):
        or_list = []
        tab = uve_key.split(":")[0]
        
        smap = { 'ObjectCollectorInfo':"CollectorState",
                 'ObjectConfigNode':"ModuleCpuState",
                 'ObjectBgpRouter':"BgpRouterState",
                 'ObjectVRouter':"VrouterAgent", }
        sname = smap[tab]
        if sname in uve_data:
            try:
                build_info = uve_data[sname]["build_info"]
            except KeyError:
                and_list = []
                and_list.append(AlarmConditionMatch(
                    condition=AlarmCondition(operation="==",
                        operand1=sname+".build_info", operand2="null"),
                    match=[AlarmMatch(json_operand1_value="null")]))
                or_list.append(AlarmRuleMatch(rule=and_list))

        if len(or_list):
            return or_list
        else:
            return None

class PartialSysinfoCompute(PartialSysinfo):
    """System Info Incomplete.
       Basic System Information is absent for this node in VrouterAgent.build_info"""
    def __call__(self, uve_key, uve_data):
       return super(PartialSysinfoCompute,self).__call__(uve_key, uve_data)

class PartialSysinfoAnalytics(PartialSysinfo):
    """System Info Incomplete.
       Basic System Information is absent for this node in CollectorState.build_info"""
    def __call__(self, uve_key, uve_data):
       return super(PartialSysinfoAnalytics,self).__call__(uve_key, uve_data)

class PartialSysinfoConfig(PartialSysinfo):
    """System Info Incomplete.
       Basic System Information is absent for this node in ModuleCpuState.build_info"""
    def __call__(self, uve_key, uve_data):
       return super(PartialSysinfoConfig,self).__call__(uve_key, uve_data)

class PartialSysinfoControl(PartialSysinfo):
    """System Info Incomplete.
       Basic System Information is absent for this node in BgpRouterState.build_info"""
    def __call__(self, uve_key, uve_data):
       return super(PartialSysinfoControl,self).__call__(uve_key, uve_data)
