from  opserver.plugins.alarm_base import *

class BgpConnectivity(AlarmBase):
    """BGP peer mismatach.
       Not enough BGP peers are up in BgpRouterState.num_up_bgp_peer"""

    def __call__(self, uve_key, uve_data):
        err_list = []
        if not uve_data.has_key("BgpRouterState"):
            return self.__class__.__name__, AlarmBase.SYS_WARN, err_list
        
        ust = uve_data["BgpRouterState"]

        l,r = ("num_up_bgp_peer","num_bgp_peer")
        cm = True
        if not ust.has_key(l):
            err_list.append(AlarmRule(oper="!",
                operand1=AlarmOperand(\
                    name="BgpRouterState.num_up_bgp_peer", value=None),
                operand2=None))
            cm = False
        if not ust.has_key(r):
            err_list.append(AlarmRule(oper="!",
                operand1=AlarmOperand(\
                    name="BgpRouterState.num_bgp_peer", value=None),
                operand2=None))
            cm = False
        if cm:
            if not ust[l] == ust[r]:
                err_list.append(AlarmRule(oper="!=",
                    operand1=AlarmOperand(\
                        name="BgpRouterState.num_up_bgp_peer", value=l),
                    operand2=AlarmOperand(\
                        name="BgpRouterState.num_bgp_peer", value=r)))

        return self.__class__.__name__, AlarmBase.SYS_WARN, err_list 
