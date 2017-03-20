#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#


import gevent
import json
from pprint import pformat

from vnc_api.vnc_api import VncApi
from cfgm_common.vnc_kombu import VncKombuClient
from cfgm_common.exceptions import RefsExistError, NoIdError
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus, \
    ConnectionType
from sandesh_common.vns.constants import API_SERVER_DISCOVERY_SERVICE_NAME


class ConfigHandler(object):

    def __init__(self, service_id, logger, api_server_config,
                 keystone_info, rabbitmq_info, config_types=None):
        self._service_id = service_id
        self._logger = logger
        self._api_servers = [tuple(s.split(':')) \
            for s in api_server_config['api_server_list']]
        self._api_server_use_ssl = api_server_config['api_server_use_ssl']
        self._keystone_info = keystone_info
        self._rabbitmq_info = rabbitmq_info
        self._config_types = config_types
        self._active_api_server = None
        self._api_client_connection_task = None
        self._api_client = None
        self._rabbitmq_client = None
        self._config_sync_done = gevent.event.Event()
    # end __init__

    # Public methods

    def start(self):
        self._update_apiserver_connection_status('', ConnectionStatus.INIT)
        if self._api_servers:
            self._api_client_connection_task = gevent.spawn(
                self._connect_to_api_server)
        # Connect to rabbitmq for config update notifications
        rabbitmq_qname = self._service_id
        self._rabbitmq_client = VncKombuClient(self._rabbitmq_info['servers'],
            self._rabbitmq_info['port'], self._rabbitmq_info['user'],
            self._rabbitmq_info['password'], self._rabbitmq_info['vhost'],
            self._rabbitmq_info['ha_mode'], rabbitmq_qname,
            self._rabbitmq_subscribe_callback, self._logger,
            rabbit_use_ssl=self._rabbitmq_info['use_ssl'],
            kombu_ssl_version=self._rabbitmq_info['ssl_version'],
            kombu_ssl_keyfile=self._rabbitmq_info['ssl_keyfile'],
            kombu_ssl_certfile=self._rabbitmq_info['ssl_certfile'],
            kombu_ssl_ca_certs=self._rabbitmq_info['ssl_ca_certs'])
    # end start

    def stop(self):
        if self._api_client_connection_task:
            self._api_client_connection_task.kill()
    # end stop

    def obj_to_dict(self, obj):
        def to_json(obj):
            if hasattr(obj, 'serialize_to_json'):
                return obj.serialize_to_json()
            else:
                return dict((k, v) for k, v in obj.__dict__.iteritems())

        return json.loads(json.dumps(obj, default=to_json))
    # end obj_to_dict

    def update_api_server_list(self, api_server_list):
        self._logger('Received update for api_server_list: %s'
            % (str(api_server_list)), SandeshLevel.SYS_INFO)
        if isinstance(api_server_list, list):
            self._api_servers = [tuple(s.split(':')) for s in api_server_list]
            if not len(api_server_list):
                self._active_api_server = None
                self._api_client = None
                if self._api_client_connection_task:
                    self._api_client_connection_task.kill()
                    self._api_client_connection_task = None
                self._update_apiserver_connection_status('',
                    ConnectionStatus.INIT)
            elif self._active_api_server is None or \
               self._active_api_server not in self._api_servers:
                if not self._api_client_connection_task:
                    self._api_client_connection_task = \
                        gevent.spawn(self._connect_to_api_server)
        else:
            self._logger('Invalid api_server_list received %s'
                % (str(api_server_list)), SandeshLevel.SYS_ERR)
    # end update_api_server_list

    # Private methods

    def _fqname_to_str(self, fq_name):
        return ':'.join(fq_name)
    # end _fqname_to_str

    def _update_apiserver_connection_status(self, api_server, status, msg=''):
        ConnectionState.update(conn_type=ConnectionType.APISERVER,
            name='Config', status=status, message=msg,
            server_addrs=[api_server])
    # end _update_apiserver_connection_status

    def _rabbitmq_subscribe_callback(self, notify_msg):
        self._config_sync_done.wait()
        if notify_msg['type'] not in self._config_types:
            return
        self._logger('Received config update: %s' % (pformat(notify_msg)),
            SandeshLevel.SYS_INFO)
        cfg_type = notify_msg['type'].replace('-', '_')
        uuid = notify_msg['uuid']
        if notify_msg['oper'] == 'CREATE' or notify_msg['oper'] == 'UPDATE':
            cfg_obj = self._read_config(cfg_type, uuid)
            if cfg_obj:
                self._handle_config_update(cfg_type, cfg_obj.get_fq_name_str(),
                    cfg_obj, notify_msg['oper'])
            else:
                self._logger('config object %s:%s not found in api-server' %
                    (cfg_type, uuid), SandeshLevel.SYS_ERR)
        elif notify_msg['oper'] == 'DELETE':
            fq_name_str = self._fqname_to_str(
                notify_msg['obj_dict']['fq_name'])
            self._handle_config_update(cfg_type, fq_name_str, None,
                notify_msg['oper'])
    # end _rabbitmq_subscribe_callback

    def _connect_to_api_server(self):
        self._active_api_server = None
        self._api_client = None
        while not self._active_api_server:
            for api_server in self._api_servers:
                try:
                    self._api_client = VncApi(
                        self._keystone_info['admin_user'],
                        self._keystone_info['admin_password'],
                        self._keystone_info['admin_tenant_name'],
                        api_server[0], api_server[1],
                        api_server_use_ssl=self._api_server_use_ssl,
                        auth_host=self._keystone_info['auth_host'],
                        auth_protocol=self._keystone_info['auth_protocol'],
                        auth_port=self._keystone_info['auth_port'])
                except Exception as e:
                    self._logger('Failed to connect to contrail-api '
                    'server: %s' % (str(e)), SandeshLevel.SYS_ERR)
                    self._update_apiserver_connection_status('%s:%s' %
                        (api_server[0], api_server[1]),
                        ConnectionStatus.DOWN, str(e))
                else:
                    if self._sync_config():
                        self._active_api_server = api_server
                        self._update_apiserver_connection_status('%s:%s' %
                            (api_server[0], api_server[1]),
                            ConnectionStatus.UP)
                        break
                    else:
                        self._update_apiserver_connection_status('%s:%s' %
                            (api_server[0], api_server[1]),
                            ConnectionStatus.DOWN, 'Config sync failed')
            if not self._active_api_server:
                gevent.sleep(2)
        self._api_client_connection_task = None
    # end _connect_to_api_server

    def _create_config(self, config_type, config_obj):
        try:
            config_create_method = getattr(self._api_client,
                config_type.replace('-', '_')+'_create')
            config_create_method(config_obj)
        except AttributeError:
            self._logger('Invalid config type "%s"' % (config_type),
                SandeshLevel.SYS_ERR)
        except RefsExistError:
            self._logger('Config object "%s:%s" already created' %
                (config_type, config_obj.name), SandeshLevel.SYS_ERR)
            return True
        except Exception as e:
            self._logger('Failed to create config object "%s:%s - %s"' %
                (config_type, config_obj.name, str(e)), SandeshLevel.SYS_ERR)
        else:
            return True
        return False
    # end _create_config

    def _read_config(self, config_type, uuid):
        try:
            config_read_method = getattr(self._api_client,
                config_type.replace('-', '_')+'_read')
            config_obj = config_read_method(id=uuid)
        except AttributeError:
            self._logger('Invalid config type "%s"' % (config_type),
                SandeshLevel.SYS_ERR)
        except NoIdError:
            self._logger('config object %s:%s not found' %
                (config_type, uuid), SandeshLevel.SYS_ERR)
        except Exception as e:
            self._logger('Failed to get config object %s:%s - %s' %
                (config_type, uuid, str(e)), SandeshLevel.SYS_ERR)
        else:
            return config_obj
        return None
    # end _read_config

    def _sync_config(self):
        config = {}
        for cfg_type in self._config_types:
            try:
                config_list_method = getattr(self._api_client,
                    cfg_type.replace('-', '_')+'s_list')
                config[cfg_type] = config_list_method(detail=True)
            except AttributeError:
                self._logger('Invalid config type "%s"' % (cfg_type),
                    SandeshLevel.SYS_ERR)
                return False
            except Exception as e:
                self._logger('Failed to sync config type "%s" - %s' %
                    (cfg_type, str(e)), SandeshLevel.SYS_ERR)
                return False
        self._handle_config_sync(config)
        self._config_sync_done.set()
        return True
    # end _sync_config

    # Should be overridden by the derived class
    def _handle_config_update(self, config_type, fq_name,
                              config_obj, operation):
        pass
    # end _handle_config_update

    # Should be overridden by the derived class
    def _handle_config_sync(self, config):
        pass
    # end _handle_config_sync


# end class ConfigHandler
