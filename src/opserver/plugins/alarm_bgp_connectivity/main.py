
from  opserver.plugins.alarm_base import AlarmBase

class BgpConnectivity(AlarmBase):
    """Not enough BGP peers are up in BgpRouterState.num_up_bgp_peer"""

    def __call__(self, uve_key, uve_data):
        err_list = []
        if not uve_data.has_key("BgpRouterState"):
            return self.__class__.__name__, AlarmBase.SYS_WARN, err_list
        
        ust = uve_data["BgpRouterState"]

        l,r = ("num_up_bgp_peer","num_bgp_peer")
        cm = True
        if not ust.has_key(l):
            err_list.append(("BgpRouterState.%s != None" % l,"None"))
            cm = False
        if not ust.has_key(r):
            err_list.append(("BgpRouterState.%s != None" % r,"None"))
            cm = False
        if cm:
            if not ust[l] == ust[r]:
                err_list.append(("BgpRouterState.%s != BgpRouterState.%s" % (l,r),
                             "%s != %s" % (str(ust[l]), str(ust[r]))))

        return self.__class__.__name__, AlarmBase.SYS_WARN, err_list 
