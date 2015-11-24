from  opserver.plugins.alarm_base import *

class XmppConnectivity(AlarmBase):
    """XMPP peer mismatach.
       Not enough XMPP peers are up in BgpRouterState.num_up_xmpp_peer"""

    def __call__(self, uve_key, uve_data):
        err_list = []
        if not uve_data.has_key("BgpRouterState"):
            return self.__class__.__name__, AlarmBase.SYS_WARN, err_list
        
        ust = uve_data["BgpRouterState"]

        l,r = ("num_up_xmpp_peer","num_xmpp_peer")
        cm = True
        if not ust.has_key(l):
            err_list.append(AlarmRule(oper="!",
                operand1=AlarmOperand(\
                    name="BgpRouterState.num_up_xmpp_peer", value=None),
                operand2=None))
            cm = False
        if not ust.has_key(r):
            err_list.append(AlarmRule(oper="!",
                operand1=AlarmOperand(\
                    name="BgpRouterState.num_xmpp_peer", value=None),
                operand2=None))
            cm = False
        if cm:
            if not ust[l] == ust[r]:
                err_list.append(AlarmRule(oper="!=",
                    operand1=AlarmOperand(\
                        name="BgpRouterState.num_up_xmpp_peer", value=ust[l]),
                    operand2=AlarmOperand(\
                        name="BgpRouterState.num_xmpp_peer", value=ust[r])))

        return self.__class__.__name__, AlarmBase.SYS_WARN, err_list 
