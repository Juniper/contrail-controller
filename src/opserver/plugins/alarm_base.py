import abc
from collections import namedtuple

class AlarmBase(object):
    """Base class for Alarms
    """

    ALARM_CRITICAL, ALARM_MAJOR, ALARM_MINOR = range(3)

    _RULES = None

    def __init__(self, sev=None, at=0, it=0, fec=False,
                 fcs=0, fct=0, config=None):
        self._sev = sev or self.ALARM_MAJOR
        self._ActiveTimer = at
        self._IdleTimer = it
        self._FreqExceededCheck = fec
        self._FreqCheck_Times = fct
        self._FreqCheck_Seconds = fcs
        self._config = config

    def rules(self):
        """Return the rules for this alarm
        """
        return self._RULES

    def config(self):
        """Return the config object for this alarm
        """
        return self._config

    def description(self):
        """Return alarm description
        """
        return self._config.get_id_perms().get_description()

    def severity(self):
        """Return the severity of the alarm
           This should not depend on UVE contents
        """
        if self._config:
            return self._config.alarm_severity
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

    def set_config(self, alarm_cfg_obj):
        """Set the alarm config object for this alarm
        """
        self._config = alarm_cfg_obj

    def is_enabled(self):
        if self._config:
            if self._config.id_perms.enable is not None:
                return self._config.id_perms.enable
        return True

    #def __call__(self, uve_key, uve_data):
        """Evaluate whether alarm should be raised.
        Implement this method if you want to override the generic
        alarm processing engine.
        :param uve_key: Key of the UVE (a string)
        :param uve_data: UVE Contents
        :returns: list of AlarmAndList
        """
