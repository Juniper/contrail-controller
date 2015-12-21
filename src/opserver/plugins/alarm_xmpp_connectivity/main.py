from  opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json

class XmppConnectivity(AlarmBase):
    """XMPP peer mismatch.
       Not enough XMPP peers are up in BgpRouterState.num_up_xmpp_peer"""

    def __init__(self):
	AlarmBase.__init__(self, AlarmBase.SYS_WARN)

    def __call__(self, uve_key, uve_data):
        or_list = []
        v2 = None
        v1 = uve_data.get("BgpRouterState", None)
        if v1 is not None:
            and_list = []
            and_list.append(AlarmElement(\
                rule=AlarmTemplate(oper="!=",
                    operand1=Operand1(keys=["BgpRouterState"]),
                    operand2=Operand2(json_value="null")),
                json_operand1_value=json.dumps({})))
            v2 = v1.get("num_up_xmpp_peer", None)
            if v2 is None:
		and_list.append(AlarmElement(\
		    rule=AlarmTemplate(oper="==",
			operand1=Operand1(keys=["BgpRouterState","num_up_xmpp_peer"]),
			operand2=Operand2(json_value="null")),
		    json_operand1_value=json.dumps(None)))
                or_list.append(AllOf(all_of=and_list))

        if v1 is not None:
	    lval = v1.get("num_up_xmpp_peer",None)
	    rval = v1.get("num_xmpp_peer",None)
        else:
            lval = None
            rval = None

	if lval != rval:
	    and_list = []
	    and_list.append(AlarmElement(\
		rule=AlarmTemplate(oper="!=",
		    operand1=Operand1(keys=["BgpRouterState","num_up_xmpp_peer"]),
		    operand2=Operand2(keys=["BgpRouterState","num_xmpp_peer"])),
		json_operand1_value=json.dumps(lval),
		json_operand2_value=json.dumps(rval)))
            or_list.append(AllOf(all_of=and_list))
        if len(or_list):
            return or_list
        else:
	    return None
