from opserver.plugins.alarm_base import AlarmBase

class PartialSysinfo(AlarmBase):
 
    def __call__(self, uve_key, uve_data):
        err_list = []
        tab = uve_key.split(":")[0]
        
        smap = { 'ObjectCollectorInfo':"CollectorState",
                 'ObjectConfigNode':"ModuleCpuState",
                 'ObjectBgpRouter':"BgpRouterState",
                 'ObjectVRouter':"VrouterAgent", }
        sname = smap[tab]
        if not uve_data.has_key(sname):
            err_list.append(("%s != None" % sname,"None"))
            return self.__class__.__name__, AlarmBase.SYS_WARN, err_list

        if not uve_data[sname].has_key("build_info"):
            err_list.append(("%s.build_info != None" % sname,"None"))
            return self.__class__.__name__, AlarmBase.SYS_WARN, err_list
		
        return self.__class__.__name__, AlarmBase.SYS_WARN, err_list 

class PartialSysinfoCompute(PartialSysinfo):
    """Basic System Information is absent for this node in VrouterAgent.build_info"""
    def __call__(self, uve_key, uve_data):
       return super(PartialSysinfoCompute,self).__call__(uve_key, uve_data)

class PartialSysinfoAnalytics(PartialSysinfo):
    """Basic System Information is absent for this node in CollectorState.build_info"""
    def __call__(self, uve_key, uve_data):
       return super(PartialSysinfoAnalytics,self).__call__(uve_key, uve_data)

class PartialSysinfoConfig(PartialSysinfo):
    """Basic System Information is absent for this node in ModuleCpuState.build_info"""
    def __call__(self, uve_key, uve_data):
       return super(PartialSysinfoConfig,self).__call__(uve_key, uve_data)

class PartialSysinfoControl(PartialSysinfo):
    """Basic System Information is absent for this node in BgpRouterState.build_info"""
    def __call__(self, uve_key, uve_data):
       return super(PartialSysinfoControl,self).__call__(uve_key, uve_data)
