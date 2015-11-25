from opserver.plugins.alarm_base import *


class ProcessStatus(AlarmBase):
    """Process Failure.
       NodeMgr reports abnormal status for process(es) in NodeStatus.process_info"""

    def __call__(self, uve_key, uve_data):
        err_list = []
        if not uve_data.has_key("NodeStatus"):
            err_list.append(AlarmRule(oper="!",
                operand1=AlarmOperand(\
                    name="NodeStatus", value=None),
                operand2=None))
            return self.__class__.__name__, AlarmBase.SYS_ERR, err_list
        
        if not uve_data["NodeStatus"].has_key("process_info"):
            err_list.append(AlarmRule(oper="!",
                operand1=AlarmOperand(\
                    name="NodeStatus.process_info", value=None),
                operand2=None))
            return self.__class__.__name__, AlarmBase.SYS_ERR, err_list
        
        value = None
        try:
            proc_status_list = uve_data["NodeStatus"]["process_info"]
            for proc_status in proc_status_list:
                value = str(proc_status)
                if proc_status["process_state"] != "PROCESS_STATE_RUNNING":
                    err_list.append(AlarmRule(oper="!=",
                        operand1=AlarmOperand(\
                            name="NodeStatus.process_info[].process_state",
                            value=proc_status),
                        operand2=AlarmOperand(\
                            name=None, value="PROCESS_STATE_RUNNING")))
        except Exception as ex:
            err_list.append((str(ex), value))

        return self.__class__.__name__, AlarmBase.SYS_ERR, err_list 
