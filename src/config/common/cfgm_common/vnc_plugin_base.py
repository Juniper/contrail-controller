from __future__ import unicode_literals
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from builtins import object
import abc
from vnc_api.gen.vnc_api_extension_gen import ResourceApiGen
from future.utils import with_metaclass

class Resync(object):
    @abc.abstractmethod
    def __init__(self, api_server_ip, api_server_port, conf_sections):
        pass
    #end __init__

    @abc.abstractmethod
    def resync_domains_projects(self):
        """
        Method that implements auditing of projects between orchestration
        system and OpenContrail VNC
        """
        pass
    #end resync_projects

#end class Resync


class ResourceApi(ResourceApiGen):
    @abc.abstractmethod
    def __init__(self):
        pass
    #end __init__

    @abc.abstractmethod
    def transform_request(self, request):
        pass
    # end transform_request

    @abc.abstractmethod
    def validate_request(self, request):
        pass
    # end validate_request

    @abc.abstractmethod
    def transform_response(self, request, response):
        pass
    # end transform_response


class NeutronApi(object):
    @abc.abstractmethod
    def __init__(self):
        pass
    #end __init__


class AuthBase(with_metaclass(abc.ABCMeta, object)):
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
