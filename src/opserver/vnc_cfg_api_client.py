#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import time
import requests
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionType,\
    ConnectionStatus
from vnc_api import vnc_api

class VncCfgApiClient(object):

    def __init__(self, conf_info, sandesh_instance, logger):
        self._conf_info = conf_info
        self._sandesh_instance = sandesh_instance
        self._logger = logger
        self._vnc_api_client = None
    # end __init__

    def _update_connection_state(self, status, message = ''):
        server_addrs = ['%s:%d' % (self._conf_info['api_server_ip'], \
            self._conf_info['api_server_port'])]
        ConnectionState.update(conn_type=ConnectionType.APISERVER, name='',
            status=status, message=message, server_addrs=server_addrs)
    # end _update_connection_state

    def _get_user_token_info(self, user_token, uuid=None):
        if self._vnc_api_client:
             return self._vnc_api_client.obj_perms(user_token, uuid)
        else:
            self._logger.error('VNC Config API Client NOT FOUND')
            return None
    # end _get_user_token_info

    def connect(self):
        # Retry till API server is up
        connected = False
        self._update_connection_state(ConnectionStatus.INIT)
        while not connected:
            try:
                self._vnc_api_client = vnc_api.VncApi(
                    self._conf_info['admin_user'],
                    self._conf_info['admin_password'],
                    self._conf_info['admin_tenant_name'],
                    self._conf_info['api_server_ip'],
                    self._conf_info['api_server_port'],
                    api_server_use_ssl=self._conf_info['api_server_use_ssl'],
                    auth_host=self._conf_info['auth_host'],
                    auth_port=self._conf_info['auth_port'],
                    auth_protocol=self._conf_info['auth_protocol'])
                connected = True
                self._update_connection_state(ConnectionStatus.UP)
            except Exception as e:
                # Update connection info
                self._update_connection_state(ConnectionStatus.DOWN, str(e))
                time.sleep(3)
    # end connect

    def is_read_permission(self, user_token, uuid):
        result = self._get_user_token_info(user_token, uuid)
        if not result or 'permissions' not in result:
            self._logger.error('Permissions for token %s NOT FOUND' % \
                (str(user_token)))
            return False
        return 'R' in result['permissions']
    # end is_read_permission

    def is_role_cloud_admin(self, user_token):
        result = self._get_user_token_info(user_token)
        if not result or not result['token_info']:
            self._logger.error(
                'Token info for %s NOT FOUND' % str(user_token))
            return False
        # Handle v2 and v3 responses
        token_info = result['token_info']
        if 'access' in token_info:
            roles_list = [roles['name'] for roles in \
                token_info['access']['user']['roles']]
        elif 'token' in token_info:
            roles_list = [roles['name'] for roles in \
                token_info['token']['roles']]
        else:
            self._logger.error('Role info for %s NOT FOUND: %s' % \
                (str(user_token), str(token_info)))
            return False
        return self._conf_info['cloud_admin_role'] in roles_list
    # end is_role_cloud_admin

# end class VncCfgApiServer
