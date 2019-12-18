#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation plugin base class for device config module.

The specific plugins should inherit from this class.
"""

import abc
from builtins import object
from builtins import str

from .imports import import_ansible_plugins


#
# Base Class for all plugins. pluigns must implement all abstract methods
#
class AnsibleBase(object):
    _plugins = {}

    class PluginError(Exception):
        """Exception class to indicate plugin error."""

        def __init__(self, plugin_info):
            """Initialize the exception with plugin info."""
            self.plugin_info = plugin_info
        # end __init__

        def __str__(self):
            """Provide plugin info in exception details."""
            return "Ansible Plugin Error, Configuration = %s" % \
                str(self.plugin_info)
        # end __str__
    # end PluginError

    class PluginsRegistrationFailed(Exception):
        """Exception class to indicate plugin registration error."""

        def __init__(self, exceptions):
            """Initialize the exception with nested exceptions."""
            self.exceptions = exceptions
        # end __init__

        def __str__(self):
            """Provide details of nested exception in exception message."""
            ex_mesg = "Plugin Registrations Failed:\n"
            for ex in self.exceptions or []:
                ex_mesg += ex + "\n"
            return ex_mesg
        # end __str__
    # end PluginsRegistrationFailed

    def __init__(self, logger):
        """Initialize the plugin."""
        self._logger = logger
        self.commit_stats = {
            'last_commit_time': '',
            'last_commit_duration': '',
            'commit_status_message': '',
            'total_commits_sent_since_up': 0,
        }
        self.initialize()
        self.device_connect()
    # end __init__

    # instantiate a plugin dynamically
    @classmethod
    def plugin(cls, vendor, product, params, logger):
        pr = params.get("physical_router")
        name = str(pr.physical_router_role) + ":" + \
            str(vendor) + ":" + str(product)
        if pr.physical_router_role and vendor and product:
            pconf = AnsibleBase._plugins.get(pr.physical_router_role)
            if pconf:
                logger.info(
                    "Found ansible plugin pr=%s, role/vendor/product=%s" %
                    (pr.uuid, name))
                pconf = pconf[0]  # for now one only
                inst_cls = pconf.get('class')
                return inst_cls(logger, params)
        logger.warning(
            "No ansible plugin pr=%s, role/vendor/product=%s" %
            (pr.uuid, name))
        return None
    # end plugin

    # validate plugin name
    def verify_plugin(self, vendor, product, role):
        return self.is_role_supported(role)
    # end verify_plugin

    # register all plugins with device manager
    @classmethod
    def register_plugins(cls):
        # make sure modules are loaded
        import_ansible_plugins()
        # register plugins, find all leaf implementation classes derived from
        # this class
        subclasses = set()
        work = [cls]
        while work:
            parent = work.pop()
            if not parent.__subclasses__():
                subclasses.add(parent)
                continue
            for child in parent.__subclasses__():
                if child not in subclasses:
                    work.append(child)
        # register all plugins,
        # if there is any exception, continue to register all other plugins,
        # finally throw one single exception to the caller
        exceptions = []
        for scls in subclasses or []:
            try:
                scls.register()
            except AnsibleBase.PluginError as e:
                exceptions.append(str(e))
        if exceptions:
            raise cls.PluginsRegistrationFailed(exceptions)
    # end register_plugins

    @classmethod
    def register(cls, plugin_info):
        if not plugin_info or not plugin_info.get("roles"):
            raise AnsibleBase.PluginError(plugin_info)
        for role in plugin_info.get("roles"):
            AnsibleBase._plugins.setdefault(
                role.lower(), []).append(plugin_info)
    # end register

    @classmethod
    def is_role_supported(cls, role):
        """Check if plugin is capable of supporting role."""
        return False
    # end is_role_supported

    @abc.abstractmethod
    def plugin_init(self, is_delete=False):
        """Initialize plugin."""
    # end plugin_init

    @abc.abstractmethod
    def initialize(self):
        """Initialize local data structures."""
    # end initialize

    def validate_device(self):
        return True
    # def validate_device

    @abc.abstractmethod
    def update(self, params):
        """Update plugin intialization params."""
    # end update

    def clear(self):
        """Clear connections and data structures."""
        self.initialize()
        self.device_disconnect()
    # end clear

    @abc.abstractmethod
    def device_connect(self):
        """Initialize the device connection and get the handle."""
        pass
    # end device_connect

    @abc.abstractmethod
    def device_disconnect(self):
        """Delete the device connection and and reset the handle."""
        pass
    # end device_disconnect

    @abc.abstractmethod
    def retry(self):
        """Retry send conf or not."""
        return False
    # end retry

    @abc.abstractmethod
    def device_get(self, filters={}):
        """Retrieve configuration from device for given filter parameters."""
        return {}
    # end device_get

    def device_get_config(self, filters={}):
        """Retrieve entire device current configuration."""
        return {}
    # end device_get_config

    @abc.abstractmethod
    def get_commit_stats(self):
        """Return Commit Statistics if any."""
        return self.commit_stats
    # end device_get

    @abc.abstractmethod
    def push_conf(self, feature_configs=None, is_delete=False):
        """Push config to device."""
        return 0
    # end push_conf

    @abc.abstractmethod
    def get_service_status(self, service_params={}):
        """Get service status for a given service."""
        return {}
    # end get_service_status

# end AnsibleBase
