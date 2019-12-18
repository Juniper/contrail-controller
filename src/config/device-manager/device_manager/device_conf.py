#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation plugin base class for device config module
"""

from builtins import str
from builtins import object
import abc
import sys
from .imports import import_plugins
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
        pr = params.get("physical_router")
        if not vendor or not product:
            name = str(vendor) + ":" + str(product)
            logger.warning("No plugin pr=%s, vendor/product=%s"%(pr.uuid, name))
            return None
        vendor = vendor.lower()
        product = product.lower()
        pconfs = DeviceConf._plugins.get(vendor)
        for pconf in pconfs or []:
            inst_cls = pconf.get('class')
            if inst_cls.is_product_supported(product, pr.physical_router_role):
                return  inst_cls(logger, params)
        name = vendor + ":" + product
        logger.warning("No plugin found for pr=%s, vendor/product=%s"%(pr.uuid, name))
        return None
    # end plugin

    # validate plugin name
    def verify_plugin(self, vendor, product, role):
        if not vendor or not product:
            return False
        vendor = vendor.lower()
        product = product.lower()
        if vendor == self._vendor.lower() and \
        self.is_product_supported(product, role):
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
            except DeviceConf.PluginError as e:
                exceptions.append(str(e))
        if exceptions:
            raise PluginsRegistrationFailed(exceptions)
    # end register_plugins

    @classmethod
    def register(cls, plugin_info):
        if not all (k in plugin_info for k in ("vendor", "products")):
            raise DeviceConf.PluginError(plugin_info)
        name = plugin_info['vendor']
        DeviceConf._plugins.setdefault(name.lower(), []).append(plugin_info)
    # end register

    @classmethod
    def is_product_supported(cls, product, role):
        """ check if plugin is capable of supporting product """
        return False
    # end is_product_supported

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
        if not self.device_config:
            self.device_config = {}
            return False
        model = self.device_config.get('product-model')
        if not self.is_product_supported(model, \
               self.physical_router.physical_router_role):
            self._logger.error("product model mismatch: device model = %s," \
                      " plugin mode = %s" % (model, str(self._products)))
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

    def device_get_config(self, filters={}):
        """Retrieve entire device current configuration """
        return {}
    # end device_get_config

    @abc.abstractmethod
    def get_commit_stats(self):
        """return Commit Statistics if any"""
        return self.commit_stats
    # end device_get

    @abc.abstractmethod
    def push_conf(self, is_delete=False):
        """push config to device"""
        return 0
    # end push_conf

    @abc.abstractmethod
    def get_service_status(self, service_params={}):
        """Get service status for a given service """
        return {}
    # end get_service_status

# end DeviceConf
