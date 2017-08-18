#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#


from pysandesh.sandesh_logger import SandeshLogger
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel


class AnalyticsLogger(object):

    def __init__(self, sandesh):
        self._sandesh = sandesh
        self._logger = sandesh.logger()
    # end __init__

    def log(self, msg, level):
        self._logger.log(SandeshLogger.get_py_logger_level(level), msg)
    # end log

    def emergency(self, msg):
        self.log(msg, SandeshLevel.SYS_EMERG)
    # end emergency

    def alert(self, msg):
        self.log(msg, SandeshLevel.SYS_ALERT)
    # end alert

    def critical(self, msg):
        self.log(msg, SandeshLevel.SYS_CRIT)
    # end critical

    def error(self, msg):
        self.log(msg, SandeshLevel.SYS_ERR)
    # end error

    def warning(self, msg):
        self.log(msg, SandeshLevel.SYS_WARN)
    # end warning

    def notice(self, msg):
        self.log(msg, SandeshLevel.SYS_NOTICE)
    # end notice

    def info(self, msg):
        self.log(msg, SandeshLevel.SYS_INFO)
    # end info

    def debug(self, msg):
        self.log(msg, SandeshLevel.SYS_DEBUG)
    # end debug


# end class AnalyticsLogger
