#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
#
# authentication/authorization functionality for discovery server
#

try:
    from keystoneclient.middleware import auth_token
except ImportError:
    try:
        from keystonemiddleware import auth_token
    except Exception:
        pass
except Exception:
    pass

class AuthServiceKeystone(object):

    def __init__(self, conf):
        self._conf_info = conf
    # end __init__

    # gets called from keystone middleware after token check
    def token_valid(self, env, start_response):
        status = env.get('HTTP_X_IDENTITY_STATUS')
        return True if status != 'Invalid' else False

    def validate_user_token(self, request):
        # following config forces keystone middleware to always return the result
        # back in HTTP_X_IDENTITY_STATUS env variable
        conf_info = self._conf_info.copy()
        conf_info['delay_auth_decision'] = True

        auth_middleware = auth_token.AuthProtocol(self.token_valid, conf_info)
        return auth_middleware(request.headers.environ, None)

    def is_admin(self, request):
        if not self.validate_user_token(request):
            return False
        roles = request.headers.environ.get('HTTP_X_ROLE', '').split(",")
        return 'admin' in [x.lower() for x in roles]
# end class AuthServiceKeystone
