# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Service Monitor Module Logger

This module is a "decorator" over "service-monitor logger" infrastructure
(though not tightly bound to it).

Service Monitor Logger
----------------------
Service-monitor logger provides the ability to log at a process level
(using service monitor Sandesh messages). This in most cases ends up
being too generalized / not-granualar for individual modules within
service-monitor process. Also, each Sandesh message carries with
it different recommendations and instructions on interpretation/action
for those logs. These attributes tend to vary across different modules
within service-monitor.

Service Monitor Module Logger
-----------------------------
ServiceMonitorModuleLogger is a decorator class exposed over service monitor
logging module. This decorator class provides these following capabilities:

1. Ability to use under lying service monitor log messages as-is.
2. Ability to "selectively" override any/all of the service monitor messages.
3. Ability to specify custom Sandesh messages to use for any of the log
   instances.
4. Ability to register identifiers to specify Sandesh messages for log message
   instances.

Each module in service-monitor that needs logging would get an instance of
this decorator class. The modules can then register new Sandesh messages or
override service monitor logger messages.

Glossary
--------
Message Log Function --> Sandesh message function that is to be used to send
                         the log meesage out.

Log Function Identifier/ID --> String that is mapped to a Message Log Function.
                               This ID can be used by callers to identify
                               Sandesh message function to be used.


"""

from builtins import object
class MessageID(object):
    """
    Default log function identifiers.

    These are  standard error messages for which a module may want to
    create and register Sandesh messages.

    If any of these identifiers do have not a Sandesh message function
    registered, the underlying Service Monitor Logger message will be used.

    """
    CRITICAL  = "critical"
    EMERGENCY = "emergency"
    ALERT     = "alert"
    ERROR     = "error"
    WARNING   = "warning"
    NOTICE    = "notice"
    INFO      = "info"
    DEBUG     = "debug"

class ServiceMonitorModuleLogger(object):

    def __init__(self, logger, log_func_dict = None):
        """
        Initialize ServiceMonitorModuleLogger decorator instance.

        This decorator is initialized with the referrence to underlying
        logging module used by service monitor.

        Caller can optionally pass a dictionary of message functions that
        this decorator starts with.

        Parameters:
            logger        - Instance of the underlying service monitor logger.
            log_func_dict - Message functions to be registered with decorator.

        """

        # Cache logging infrastructure to be used.
        self.logger = logger

        # Create a dictionary to track log-function-id and its
        # message log function.
        self.logger_functions = {}

        # Initiliaze dictionary with an input cached, if provided.
        if log_func_dict and isinstance(log_func_dict, dict):
            self.logger_functions.update(log_func_dict)

    def __get_msg_func_id(self, default_msg_id, msg_func_id = None):
        """
        Get the message function identifier.

        Given possible message function identifiers, this method returns
        the selected message function identifier.

        Parameters:
            default_msg_id - Default ID to be returned if no valid function ID
                             was selected.
            msg_func_id    - Message function ID requested by caller.

        """

        # Use default message function id, if user did not provide one.
        if not msg_func_id:
            return default_msg_id

        return msg_func_id

    def __get_msg_func(self, default_msg_id, \
                       msg_func = None, user_msg_func_id = None):
        """
        Given a list of paramerters, determine the appropriate message function
        to be used.

        Parameters:
            default_msg_id - Default ID to be returned if no valid function ID
                             was selected.
            msg_func       - Message function requested by caller.
            msg_func_id    - Message function ID requested by caller.
        """

        # Evaluate input parameters to determine appropriate message function
        # to be returned to caller.
        if not msg_func:
            try:
                # First determine the message function ID to be used.
                msg_func_id = self.__get_msg_func_id(default_msg_id, user_msg_func_id)

                # Use message function ID to determine the message function
                # to be used.
                msg_func = self.logger_functions[msg_func_id]

            except KeyError:
                # No appropriate message function was found.
                msg_func = None

        return msg_func

    def add_messages(self, **kwargs):
        """
        Add custom message functions and their identifiers to message function
        cache.

        Parameters:
            **kwargs - message function keyword arguments.
        """
        for msg_id, msg_func in list(kwargs.items()):
            self.logger_functions[msg_id] = msg_func

    def emergency(self, log_msg, msg_func = None, id = None):
        """
        Emergency Logs.
        """
        self.logger.emergency(log_msg,
                              self.__get_msg_func(MessageID.CRITICAL, msg_func, id))

    def alert(self, log_msg, msg_func = None, id = None):
        """
        Alert Logs.
        """
        self.logger.alert(log_msg,
                          self.__get_msg_func(MessageID.ALERT, msg_func, id))

    def critical(self, log_msg, msg_func = None, id = None):
        """
        Critical Logs.
        """
        self.logger.critical(log_msg,
                          self.__get_msg_func(MessageID.CRITICAL, msg_func, id))

    def error(self, log_msg, msg_func = None, id = None):
        """
        Error Logs.
        """
        self.logger.error(log_msg,
                        self.__get_msg_func(MessageID.ERROR, msg_func, id))

    def warning(self, log_msg, msg_func = None, id = None):
        """
        Warning Logs.
        """
        self.logger.warning(log_msg,
                          self.__get_msg_func(MessageID.WARNING, msg_func, id))

    def notice(self, log_msg, msg_func = None, id = None):
        """
        Notice Logs.
        """
        self.logger.notice(log_msg,
                        self.__get_msg_func(MessageID.NOTICE, msg_func, id))

    def info(self, log_msg, msg_func = None, id = None):
        """
        Info Logs.
        """
        self.logger.info(log_msg,
                      self.__get_msg_func(MessageID.INFO, msg_func, id))

    def debug(self, log_msg, msg_func = None, id = None):
        """
        Debug Logs.
        """
        self.logger.debug(log_msg,
                       self.__get_msg_func(MessageID.DEBUG, msg_func, id))
