from  opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json

class BgpConnectivity(AlarmBase):
    """BGP peer mismatch.
       Not enough BGP peers are up in BgpRouterState.num_up_bgp_peer"""

    def __init__(self):
	AlarmBase.__init__(self, AlarmBase.SYS_WARN)

    def __call__(self, uve_key, uve_data):
        or_list = []
        v2 = None
        v1 = uve_data.get("BgpRouterState", None)
        if v1 is not None:
            and_list = []
            v2 = v1.get("num_up_bgp_peer", None)
            if v2 is None:
                and_list.append(AlarmConditionMatch(
                    condition=AlarmCondition(operation="==",
                        operand1="BgpRouterState.num_up_bgp_peer",
                        operand2="null"),
                    match=[AlarmMatch(json_operand1_value="null")]))
                or_list.append(AlarmRuleMatch(rule=and_list))

        if v1 is not None:
            lval = v1.get("num_up_bgp_peer", None)
            rval = v1.get("num_bgp_peer", None)
        else:
            lval = None
            rval = None

        if lval != rval:
            and_list = []
            and_list.append(AlarmConditionMatch(
                condition=AlarmCondition(operation="!=",
                    operand1="BgpRouterState.num_up_bgp_peer",
                    operand2="BgpRouterState.num_bgp_peer"),
                match=[AlarmMatch(json_operand1_value=json.dumps(lval),
                    json_operand2_value=json.dumps(rval))]))
            or_list.append(AlarmRuleMatch(rule=and_list))
        if len(or_list):
            return or_list
        else:
            return None
