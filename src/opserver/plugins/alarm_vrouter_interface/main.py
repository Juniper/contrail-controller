
from  opserver.plugins.alarm_base import AlarmBase

class VrouterInterface(AlarmBase):

    def __call__(self, uve_key, uve_data):
        err_list = []
        if not uve_data.has_key("VrouterAgent"):
            return self.__class__.__name__, err_list
        
        ust = uve_data["VrouterAgent"]

        if not ust.has_key("error_intf_list"):
            return self.__class__.__name__, err_list

        if len(ust["error_intf_list"]):
            err_list.append(("VrouterAgent.error_intf_list == None",
                             str(ust["error_intf_list"])))

        return self.__class__.__name__, err_list 
