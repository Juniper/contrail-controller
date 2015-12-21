import abc
from collections import namedtuple

class AlarmBase(object):
    """Base class for Alarms
    """
    __metaclass__ = abc.ABCMeta

    SYS_EMERG, SYS_ALERT, SYS_CRIT, SYS_ERR,\
        SYS_WARN, SYS_NOTICE, SYS_INFO, SYS_DEBUG = range(8)

    def __init__(self, sev):
        self._sev = sev

    def severity(self):
        """Return the severity of the alarm
           This should not depend on UVE contents
        """
        return self._sev

    @abc.abstractmethod
    def __call__(self, uve_key, uve_data):
        """Evaluate whether alarm should be raised
        :param uve_key: Key of the UVE (a string) 
        :param uve_data: UVE Contents
        :returns: tuple with list of list of AlarmElements (AND of ORs)
        """
