from opserver.plugins.alarm_base import AlarmBase
import json

class AddressMismatchCompute(AlarmBase):
    """Compute Node has IP Address mismatch between ContrailConfig.virtual_router_ip_address
       and Operational State in VrouterAgent"""
    def __call__(self, uve_key, uve_data):
        err_list = []
        try:
            confip = json.loads(uve_data["ContrailConfig"]["elements"][\
                "virtual_router_ip_address"])
            mismatch = True
            for ipaddr in uve_data["VrouterAgent"]["self_ip_list"]:
                if ipaddr == confip:
                    mismatch = False
            if uve_data["VrouterAgent"]["control_ip"] == confip:
                mismatch = False

            if mismatch:
                err_list.append(("%s not in %s and %s != %s" % \
                       ("ContrailConfig.virtual_router_ip_address",\
                        "VrouterAgent.self_ip_list",\
                        "ContrailConfig.virtual_router_ip_address",\
                        "VrouterAgent.control_ip"), confip))
        except:
            pass
        finally:
            return self.__class__.__name__, AlarmBase.SYS_ERR, err_list
       
class AddressMismatchControl(AlarmBase):
    """Control Node has IP Address mismatch between ContrailConfig.bgp_router_parameters.address
       and Operational State in BgpRouterState"""
    def __call__(self, uve_key, uve_data):
        err_list = []

        try:
            confip = json.loads(uve_data["ContrailConfig"]["elements"][\
                "bgp_router_parameters"])["address"]
            mismatch = True
            for ipaddr in uve_data["BgpRouterState"]["bgp_router_ip_list"]:
                if ipaddr == confip:
                    mismatch = False
            if mismatch:
                err_list.append(("%s not in %s" % \
                       ("ContrailConfig.bgp_router_parameters.address",\
                        "BgpRouterState.bgp_router_ip_list"), confip))
        except:
            pass
        finally:
            return self.__class__.__name__, AlarmBase.SYS_ERR, err_list
       
