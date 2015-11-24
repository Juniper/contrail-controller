from opserver.plugins.alarm_base import *
import json

class AddressMismatchCompute(AlarmBase):
    """Compute Node IP Address mismatch.
       Compute Node has IP Address mismatch between ContrailConfig.virtual_router_ip_address
       and Operational State in VrouterAgent"""
    def __call__(self, uve_key, uve_data):
        err_list = []
        try:
            confip = json.loads(uve_data["ContrailConfig"]["elements"][\
                "virtual_router_ip_address"])
            mismatch = True
            vlist = uve_data["VrouterAgent"]["self_ip_list"]
            for ipaddr in vlist:
                if ipaddr == confip:
                    mismatch = False
            vac = uve_data["VrouterAgent"]["control_ip"]
            if vac == confip:
                mismatch = False

            if mismatch:
                err_list.append(AlarmRule(oper="not in",
                    operand1=AlarmOperand(\
                            name="ContrailConfig.virtual_router_ip_address",
                            value=confip),
                    operand2=AlarmOperand(\
                            name="VrouterAgent.self_ip_list",
                            value=vlist)))
                err_list.append(AlarmRule(oper="!=",
                    operand1=AlarmOperand(\
                            name="ContrailConfig.virtual_router_ip_address",
                            value=confip),
                    operand2=AlarmOperand(\
                            name="VrouterAgent.control_ip",
                            value=vac)))
        except:
            pass
        finally:
            return self.__class__.__name__, AlarmBase.SYS_ERR, err_list
       
class AddressMismatchControl(AlarmBase):
    """Control Node IP Address mismatch.
       Control Node has IP Address mismatch between ContrailConfig.bgp_router_parameters.address
       and Operational State in BgpRouterState"""
    def __call__(self, uve_key, uve_data):
        err_list = []

        try:
            confip = json.loads(uve_data["ContrailConfig"]["elements"][\
                "bgp_router_parameters"])["address"]
            mismatch = True
            blist = uve_data["BgpRouterState"]["bgp_router_ip_list"]
            for ipaddr in blist:
                if ipaddr == confip:
                    mismatch = False
            if mismatch:
                err_list.append(AlarmRule(oper="not in",
                    operand1=AlarmOperand(\
                            name="ContrailConfig.bgp_router_parameters.address",
                            value=confip),
                    operand2=AlarmOperand(\
                            name="BgpRouterState.bgp_router_ip_list",
                            value=blist)))
        except:
            pass
        finally:
            return self.__class__.__name__, AlarmBase.SYS_ERR, err_list
       
