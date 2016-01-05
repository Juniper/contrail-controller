from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json

class AddressMismatchCompute(AlarmBase):
    """Compute Node IP Address mismatch.
       Compute Node has IP Address mismatch between ContrailConfig.virtual_router_ip_address
       and Operational State in VrouterAgent"""
    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.SYS_ERR)

    def __call__(self, uve_key, uve_data):
        or_list = []
        try:
            lval = json.loads(uve_data["ContrailConfig"]["elements"][\
                "virtual_router_ip_address"])
        except KeyError:
            lval = None

        try:
            rval1 = uve_data["VrouterAgent"]["self_ip_list"]
        except KeyError:
            rval1 = None

        try:
            rval2 = uve_data["VrouterAgent"]["control_ip"]
        except KeyError:
            rval2 = None

        if not isinstance(rval1,list) or lval not in rval1:
            and_list = []
            and_list.append(AlarmElement(\
                rule=AlarmTemplate(oper="not in",
                    operand1=Operand1(keys=\
                        ["ContrailConfig","elements","virtual_router_ip_address"],
                        json=2),
                    operand2=Operand2(keys=["VrouterAgent","self_ip_list"])),
                json_operand1_value=json.dumps(lval),
                json_operand2_value=json.dumps(rval1)))

            if len(and_list) > 0 and lval != rval2:
                and_list.append(AlarmElement(\
                    rule=AlarmTemplate(oper="!=",
                        operand1=Operand1(keys=\
                            ["ContrailConfig","elements",
                            "virtual_router_ip_address"],
                            json=2),
                        operand2=Operand2(keys=["VrouterAgent","control_ip"])),
                    json_operand1_value=json.dumps(lval),
                    json_operand2_value=json.dumps(rval2)))
                or_list.append(AllOf(all_of=and_list))

        if len(or_list):
            return or_list
        else:
            return None
       
class AddressMismatchControl(AlarmBase):
    """Control Node IP Address mismatch.
       Control Node has IP Address mismatch between ContrailConfig.bgp_router_parameters.address
       and Operational State in BgpRouterState"""
    def __init__(self):
        AlarmBase.__init__(self, AlarmBase.SYS_ERR)

    def __call__(self, uve_key, uve_data):

        try:
            lval = json.loads(uve_data["ContrailConfig"]["elements"][\
                "bgp_router_parameters"])["address"]
        except KeyError:
            lval = None

        try:
            rval = uve_data["BgpRouterState"]["bgp_router_ip_list"]
        except KeyError:
            rval = None

        and_list = []
        if not isinstance(rval,list) or lval not in rval:
            and_list.append(AnyOf(any_of=[AlarmElement(\
                rule=AlarmTemplate(oper="not in",
                    operand1=Operand1(keys=["ContrailConfig",\
                        "elements","bgp_router_parameters","address"],
                        json=2),
                    operand2=Operand2(keys=\
                        ["BgpRouterState","bgp_router_ip_list"])),
                json_operand1_value=json.dumps(lval),
                json_operand2_value=json.dumps(rval))]))
            return and_list

        return None
