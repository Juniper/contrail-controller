from opserver.plugins.alarm_base import *

class ConfIncorrect(AlarmBase):
 
    def __call__(self, uve_key, uve_data):
        err_list = []
        if not uve_data.has_key("ContrailConfig"):
            err_list.append(AlarmRule(oper="!",
                operand1=AlarmOperand(\
                    name="ContrailConfig", value=None),
                operand2=None))

        return self.__class__.__name__, AlarmBase.SYS_ERR, err_list

class ConfIncorrectCompute(ConfIncorrect):
    """Compute Node config missing or incorrect.
       Compute Node configuration pushed to Ifmap as ContrailConfig is unavailable/incorrect"""
    def __call__(self, uve_key, uve_data):
       _, _, err_list = super(ConfIncorrectCompute,self).__call__(uve_key, uve_data)
       return self.__class__.__name__, AlarmBase.SYS_WARN, err_list

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
