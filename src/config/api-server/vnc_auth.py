#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
#
# This file contains authentication/authorization functionality for VNC-CFG
# subsystem.
#

import ConfigParser
import bottle


class AuthService(object):

    def __init__(self, server_mgr, args):
        self._conf_info = {
            'auth_host': args.auth_host,
            'auth_protocol': args.auth_protocol,
            'admin_user': args.admin_user,
            'admin_password': args.admin_password,
        }
        self._server_mgr = server_mgr
        self._auth_method = args.auth
        self._auth_token = None
        self._auth_middleware = None
    # end __init__

    def json_request(self, method, path):
        return {}
    # end json_request

    def get_projects(self):
        return {}
    # end get_projects

    def get_middleware_app(self):
        return None
    # end get_middleware_app

# end class AuthService
