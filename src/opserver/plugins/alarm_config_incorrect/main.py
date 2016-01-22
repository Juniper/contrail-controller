from opserver.plugins.alarm_base import *
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import *
import json

class ConfIncorrect(AlarmBase):
    def __init__(self, sev = AlarmBase.SYS_ERR):
	AlarmBase.__init__(self, sev)

    def __call__(self, uve_key, uve_data):
        or_list = []
        if not uve_data.has_key("ContrailConfig"):
            and_list = []
            and_list.append(AlarmElement(\
                rule=AlarmTemplate(oper="==",
                    operand1=Operand1(keys=["ContrailConfig"]),
                    operand2=Operand2(json_value="null")),
                json_operand1_value=json.dumps(None)))
            or_list.append(AllOf(all_of=and_list))

        if len(or_list):
            return or_list
        else:
	    return None

class ConfIncorrectCompute(ConfIncorrect):
    """Compute Node config missing or incorrect.
       Compute Node configuration pushed to Ifmap as ContrailConfig is unavailable/incorrect"""
    def __init__(self):
        ConfIncorrect.__init__(self, AlarmBase.SYS_WARN)

    def __call__(self, uve_key, uve_data):
       return super(ConfIncorrectCompute,self).__call__(uve_key, uve_data)

class ConfIncorrectAnalytics(ConfIncorrect):
    """Analytics Node config missing or incorrect.
       Analytics Node configuration pushed to Ifmap as ContrailConfig is unavailable/incorrect"""
    def __call__(self, uve_key, uve_data):
       return super(ConfIncorrectAnalytics,self).__call__(uve_key, uve_data)

class ConfIncorrectConfig(ConfIncorrect):
    """Config Node config missing or incorrect.
       Config Node configuration pushed to Ifmap as ContrailConfig is unavailable/incorrect"""
    def __call__(self, uve_key, uve_data):
       return super(ConfIncorrectConfig,self).__call__(uve_key, uve_data)

class ConfIncorrectControl(ConfIncorrect):
    """Control Node config missing or incorrect.
       Control Node configuration pushed to Ifmap as ContrailConfig is unavailable/incorrect"""
    def __call__(self, uve_key, uve_data):
       return super(ConfIncorrectControl,self).__call__(uve_key, uve_data)

class ConfIncorrectDatabase(ConfIncorrect):
    """Database Node config missing or incorrect.
       Database Node configuration pushed to Ifmap ContrailConfig is unavailable/incorrect"""
    def __call__(self, uve_key, uve_data):
       return super(ConfIncorrectDatabase,self).__call__(uve_key, uve_data)
