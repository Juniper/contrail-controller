from opserver.plugins.alarm_base import AlarmBase


class ProcessStatus(AlarmBase):
    """NodeMgr reports abnormal status for process(es) in NodeStatus.process_info"""

    def __call__(self, uve_key, uve_data):
        err_list = []
        if not uve_data.has_key("NodeStatus"):
            err_list.append(("NodeStatus != None","None"))
            return self.__class__.__name__, AlarmBase.SYS_ERR, err_list
        
        if not uve_data["NodeStatus"].has_key("process_info"):
            err_list.append(("NodeStatus.process_info != None","None"))
            return self.__class__.__name__, AlarmBase.SYS_ERR, err_list
        
        value = None
        try:
            proc_status_list = uve_data["NodeStatus"]["process_info"]
            for proc_status in proc_status_list:
                value = str(proc_status)
                if proc_status["process_state"] != "PROCESS_STATE_RUNNING":
                    err_list.append(("%s[].%s != %s" % ( "NodeStatus.process_info",
                                       "process_state",
                                       "PROCESS_STATE_RUNNING"),
                                    value)) 
        except Exception as ex:
            err_list.append((str(ex), value))

        return self.__class__.__name__, AlarmBase.SYS_ERR, err_list 
