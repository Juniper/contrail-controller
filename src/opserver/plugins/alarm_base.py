import abc


from collections import namedtuple

AlarmRule = namedtuple("AlarmRule", ["operand1","operand2","oper"])
AlarmOperand = namedtuple("AlarmOperand",["name","value"])

class AlarmBase(object):
    """Base class for Alarms
    """
    __metaclass__ = abc.ABCMeta

    SYS_EMERG, SYS_ALERT, SYS_CRIT, SYS_ERR,\
        SYS_WARN, SYS_NOTICE, SYS_INFO, SYS_DEBUG = range(8)

    def __init__(self):
        pass
    @abc.abstractmethod
    def __call__(self, uve_key, uve_data):
        """Evaluate whether alarm should be raised
        :param uve_key: Key of the UVE (a string) 
        :type uve_key: string of the form <Table>:<Key> 
        :param uve_data: UVE Contents - dict of dict
        :param uve_data: dict of dict 
            1st level dict has the UVE Type as key
            2nd level dict has the UVE Attr as key 
        :returns: tuple with alarm name, severity and list of errors as 
            <string rule>,<severity>,<string contents>
        """
