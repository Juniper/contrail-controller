
from  opserver.plugins.alarm_base import AlarmBase

class BgpConnectivity(AlarmBase):

    def __call__(self, uve_key, uve_data):
        err_list = []
        if not uve_data.has_key("BgpRouterState"):
            return self.__class__.__name__, err_list
        
        ust = uve_data["BgpRouterState"]

        for attr in [("num_up_xmpp_peer","num_xmpp_peer"),
                     ("num_up_bgp_peer","num_bgp_peer")]: 
            l,r = attr
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

        return self.__class__.__name__, err_list 
