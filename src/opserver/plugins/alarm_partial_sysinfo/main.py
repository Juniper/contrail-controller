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
        val = None       
        if sname in uve_data:
            if "build_info" in uve_data[sname]:
                val = uve_data[sname]["build_info"]

        if val is None:
	    and_list = []
	    and_list.append(AlarmElement(\
		rule=AlarmTemplate(oper="==",
		    operand1=Operand1(keys=[sname,"build_info"]),
		    operand2=Operand2(json_value="null")),
		json_operand1_value="null"))
            or_list.append(AllOf(all_of=and_list))

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
