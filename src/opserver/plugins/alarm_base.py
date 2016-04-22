import abc
from collections import namedtuple

class AlarmBase(object):
    """Base class for Alarms
    """
    __metaclass__ = abc.ABCMeta

    SYS_EMERG, SYS_ALERT, SYS_CRIT, SYS_ERR,\
        SYS_WARN, SYS_NOTICE, SYS_INFO, SYS_DEBUG = range(8)

    def __init__(self, sev, at=0, it=0, fec=False, fcs=0, fct=0):
        self._sev = sev
	self._ActiveTimer = at
	self._IdleTimer = it
	self._FreqExceededCheck = fec
	self._FreqCheck_Times = fct
	self._FreqCheck_Seconds = fcs

    def severity(self):
        """Return the severity of the alarm
           This should not depend on UVE contents
        """
        return self._sev

    def FreqCheck_Times(self):
        """Return the number of times an alarm should be enabled
	for FreqExceededCheck
        """
        return self._FreqCheck_Times

    def FreqCheck_Seconds(self):
        """Return the number of seconds in which FreqExceededCheck
	should be checked
        """
        return self._FreqCheck_Seconds

    def FreqExceededCheck(self):
        """Return whether FreqExceededCheck is enabled
        """
        return self._FreqExceededCheck

    def IdleTimer(self):
        """Return the soak time value for clearing the alarm
	   This should be 0 if there is no need of soak time
        """
        return self._IdleTimer

    def ActiveTimer(self):
        """Return the soak time value for setting the alarm
	   This should be 0 if there is no need of soak time
        """
        return self._ActiveTimer

    @abc.abstractmethod
    def __call__(self, uve_key, uve_data):
        """Evaluate whether alarm should be raised
        :param uve_key: Key of the UVE (a string) 
        :param uve_data: UVE Contents
        :returns: tuple with list of list of AlarmElements (AND of ORs)
        """
