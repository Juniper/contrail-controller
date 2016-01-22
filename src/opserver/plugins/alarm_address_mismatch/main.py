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
        and_list = []
        trigger = True

        if trigger:
            if "ContrailConfig" not in uve_data:
                trigger = False
            else:
                and_list.append(AlarmElement(\
                    rule=AlarmTemplate(oper="!=",
                        operand1=Operand1(keys=["ContrailConfig"]),
                        operand2=Operand2(json_value="null")),
                    json_operand1_value=json.dumps({})))

                try:
                    uattr = uve_data["ContrailConfig"]["elements"]
                    if isinstance(uattr,list):
                        uattr = uattr[0][0]
                    lval = json.loads(uattr["virtual_router_ip_address"])
                except KeyError:
                    lval = None

        if trigger:
            if "VrouterAgent" not in uve_data:
                trigger = False
            else:
                and_list.append(AlarmElement(\
                    rule=AlarmTemplate(oper="!=",
                        operand1=Operand1(keys=["VrouterAgent"]),
                        operand2=Operand2(json_value="null")),
                    json_operand1_value=json.dumps({})))

        if trigger:
            try:
                rval1 = uve_data["VrouterAgent"]["self_ip_list"]
            except KeyError:
                rval1 = None

            if not isinstance(rval1,list) or lval not in rval1:
                and_list.append(AlarmElement(\
                    rule=AlarmTemplate(oper="not in",
                        operand1=Operand1(keys=\
                            ["ContrailConfig","elements","virtual_router_ip_address"],
                            json=2),
                        operand2=Operand2(keys=["VrouterAgent","self_ip_list"])),
                    json_operand1_value=json.dumps(lval),
                    json_operand2_value=json.dumps(rval1)))
            else:
                trigger = False

        if trigger:
            try:
                rval2 = uve_data["VrouterAgent"]["control_ip"]
            except KeyError:
                rval2 = None

            if lval != rval2:
                and_list.append(AlarmElement(\
                    rule=AlarmTemplate(oper="!=",
                        operand1=Operand1(keys=\
                            ["ContrailConfig","elements",
                            "virtual_router_ip_address"],
                            json=2),
                        operand2=Operand2(keys=["VrouterAgent","control_ip"])),
                    json_operand1_value=json.dumps(lval),
                    json_operand2_value=json.dumps(rval2)))
            else:
                trigger = False

        if trigger:    
            or_list.append(AllOf(all_of=and_list))
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

        if "ContrailConfig" not in uve_data:
            return None

        try:
            uattr = uve_data["ContrailConfig"]["elements"]
            if isinstance(uattr,list):
                uattr = uattr[0][0]
            lval = json.loads(uattr["bgp_router_parameters"])["address"]
        except KeyError:
            lval = None

        if "BgpRouterState" not in uve_data:
            return None

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
