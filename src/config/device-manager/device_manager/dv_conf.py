#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
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

    def __init__(self):
        self.initialize()
        self.commit_stats = {
            'last_commit_time': '',
            'last_commit_duration': '',
            'commit_status_message': '',
            'total_commits_sent_since_up': 0,
        }
        self.dv_connect()
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
        # register
        for scls in subclasses or []:
            scls.register()
    # end register_plugins

    @classmethod
    def register(cls, plugin_info={}):
        if not all (k in plugin_info for k in ("vendor", "product")):
            mesg = "Missing Configurations %s" % str(plugin_info)
            raise Exception("Plugin can not be registered : " + mesg)
        name = plugin_info['vendor'] + ":" + plugin_info['product']
        DeviceConf._plugins[name] = plugin_info
    # end register

    @abc.abstractmethod
    def initialize(self):
        """Initialize local data structures"""
    # ene initialize

    @abc.abstractmethod
    def update(self, params={}):
        """Update plugin intialization params"""
    # ene update

    def clear(self):
        """clear connections and data structures"""
        self.initialize()
        self.dv_disconnect()
    # ene clear 

    @abc.abstractmethod
    def dv_connect(self):
        """Initialize the device connection and get the handle"""
        pass
    # end dv_connect

    @abc.abstractmethod
    def dv_disconnect(self):
        """delete the device connection and and reset the handle"""
        pass
    # end dv_disconnect

    @abc.abstractmethod
    def is_connected(self):
        """Initialize the device connection and get the handle"""
        return False
    # end is_connected

    @abc.abstractmethod
    def dv_send(self):
        """Send configuration to device and return size of the config size sent"""
        return 0
    # end dv_send

    @abc.abstractmethod
    def retry(self):
        """Should I retry to send conf"""
        return False
    # end retry

    @abc.abstractmethod
    def dv_get(self, filters={}):
        """Retrieve configuration from device for given filter parameters"""
        return {}
    # end dv_get

    @abc.abstractmethod
    def get_commit_stats(self):
        """return Commit Statistics if any"""
        return self.commit_stats
    # end dv_get

    @abc.abstractmethod
    def push_conf(self, is_delete=False):
        """push config to device"""
        return 0
    # end send_conf

# end DeviceConf
