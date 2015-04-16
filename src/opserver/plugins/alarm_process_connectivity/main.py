
from  opserver.plugins.alarm_base import AlarmBase

class ProcessConnectivity(AlarmBase):
    """Process(es) are reporting non-functional components in NodeStatus.process_status"""

    def __call__(self, uve_key, uve_data):
        err_list = []
        if not uve_data.has_key("NodeStatus"):
            err_list.append(("NodeStatus != None","None"))
            return self.__class__.__name__, AlarmBase.SYS_ERR, err_list
        
        if not uve_data["NodeStatus"].has_key("process_status"):
            err_list.append(("NodeStatus.process_status != None","None"))
            return self.__class__.__name__, AlarmBase.SYS_ERR, err_list
        
        value = None
        try:
            proc_status_list = uve_data["NodeStatus"]["process_status"]
            for proc_status in proc_status_list:
                value = str(proc_status)
                if proc_status["state"] != "Functional":
                    err_list.append(("%s[].%s != %s" % ( "NodeStatus.process_status",
                                       "state",
                                       "Functional"),
                                    value)) 
        except Exception as ex:
            err_list.append((str(ex), value))

        return self.__class__.__name__, AlarmBase.SYS_ERR, err_list 
