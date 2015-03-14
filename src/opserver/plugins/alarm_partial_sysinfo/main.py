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
            return self.__class__.__name__, err_list

        if not uve_data[sname].has_key("build_info"):
            err_list.append(("%s.build_info != None" % sname,"None"))
            return self.__class__.__name__, err_list
		
        return self.__class__.__name__, err_list 
        
