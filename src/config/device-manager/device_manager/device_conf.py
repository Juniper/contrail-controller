#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation plugin base class for device config module
"""

import abc
import sys
from imports import import_plugins
#
# Base Class for all plugins. pluigns must implement all abstract methods
#
class DeviceConf(object):
    _plugins = {}

    class PluginError(Exception):
        def __init__(self, plugin_info):
            self.plugin_info = plugin_info
        # end __init__

        def __str__(self):
            return "Plugin Error, Configuration = %s" % str(plugin_info)
        # end __str__
    # end PluginError

    class PluginsRegistrationFailed(Exception):
        def __init__(self, exceptions):
            self.exceptions = exceptions
        # end __init__

        def __str__(self):
            ex_mesg = "Plugin Registrations Failed:\n"
            for ex in exceptions or []:
                ex_mesg += ex + "\n"
            return ex_mesg
        # end __str__
    # end PluginsRegistrationFailed

    def __init__(self):
        self.initialize()
        self.device_config = {}
        self.commit_stats = {
            'last_commit_time': '',
            'last_commit_duration': '',
            'commit_status_message': '',
            'total_commits_sent_since_up': 0,
        }
        self.device_connect()
    # end __init__

    # instantiate a plugin dynamically
    @classmethod
    def plugin(cls, vendor, product, params, logger):
        name = vendor + ":" + product
        pconf = DeviceConf._plugins.get(name)
        if not pconf:
            pr = params.get("physical_router")
            logger.warning("No plugin found for pr=%s, vendor/product=%s"%(pr.uuid, name))
            return None
        inst_cls = pconf.get('class')
        return  inst_cls(logger, params)
    # end plugin

    # validate plugin name
    def verify_plugin(self, vendor, product):
        if vendor == self._vendor and product == self._product:
            return True
        return False
    # end verify_plugin

    # register all plugins with device manager
    @classmethod
    def register_plugins(cls):
        # make sure modules are loaded
        import_plugins()
        # register plugins, find all leaf implementation classes derived from this class
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
            except PluginError as e:
                exceptions.append(str(e))
        if exceptions:
            raise PluginsRegistrationFailed(exceptions)
    # end register_plugins

    @classmethod
    def register(cls, plugin_info):
        if not all (k in plugin_info for k in ("vendor", "product")):
            raise PluginError(plugin_info)
        name = plugin_info['vendor'] + ":" + plugin_info['product']
        DeviceConf._plugins[name] = plugin_info
    # end register

    @abc.abstractmethod
    def initialize(self):
        """Initialize local data structures"""
    # ene initialize

    def get_device_config(self):
        return self.device_config
    # end get_device_config

    def validate_device(self):
        if not self.device_config:
            self.device_config = self.device_get()
        model = self.device_config.get('product-model')
        if self._product.lower() not in model.lower():
            self._logger.error("product model mismatch: device model = %s,"
                      " plugin mode = %s" % (model, self._product))
            self.device_config = {}
            return False
        return True
    # def validate_device

    @abc.abstractmethod
    def update(self, params):
        """Update plugin intialization params"""
    # ene update

    def clear(self):
        """clear connections and data structures"""
        self.initialize()
        self.device_disconnect()
    # ene clear

    @abc.abstractmethod
    def device_connect(self):
        """Initialize the device connection and get the handle"""
        pass
    # end device_connect

    @abc.abstractmethod
    def device_disconnect(self):
        """delete the device connection and and reset the handle"""
        pass
    # end device_disconnect

    @abc.abstractmethod
    def retry(self):
        """Should I retry to send conf"""
        return False
    # end retry

    @abc.abstractmethod
    def device_get(self, filters={}):
        """Retrieve configuration from device for given filter parameters"""
        return {}
    # end device_get

    @abc.abstractmethod
    def get_commit_stats(self):
        """return Commit Statistics if any"""
        return self.commit_stats
    # end device_get

    @abc.abstractmethod
    def push_conf(self, is_delete=False):
        """push config to device"""
        return 0
    # end send_conf

# end DeviceConf
