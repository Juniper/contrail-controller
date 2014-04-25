#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import abc
from vnc_api.gen.vnc_api_extension_gen import ResourceApiGen

class Resync(object):
    @abc.abstractmethod
    def __init__(self, api_server_ip, api_server_port, conf_sections):
        pass
    #end __init__

    @abc.abstractmethod
    def resync_projects(self):
        """
        Method that implements auditing of projects between orchestration
        system and Juniper Contrail VNS
        """
        pass
    #end resync_projects

#end class Resync


class ResourceApi(ResourceApiGen):
    @abc.abstractmethod
    def __init__(self):
        pass
    #end __init__


class NeutronApi(object):
    @abc.abstractmethod
    def __init__(self):
        pass
    #end __init__


class AuthBase(object):
    __metaclass__ = abc.ABCMeta

    @abc.abstractmethod
    def __init__(self, auth_method, auth_opts):
        pass
    #end __init__

    @abc.abstractmethod
    def get_request_auth_app(self):
        """
        Middleware to invoke for authentication on every request
        """
        pass
    #end get_request_auth_app

#end class AuthBase
