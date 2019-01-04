# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
Schema transformer Etcd to store ids allocated by it
"""

# from pycassa import NotFoundException
# import cfgm_common as common
# from cfgm_common.exceptions import VncError, NoIdError
# from cfgm_common.zkclient import IndexAllocator
# from cfgm_common.vnc_object_db import VncObjectDBClient
# from sandesh_common.vns.constants import SCHEMA_KEYSPACE_NAME

import datetime
import uuid
from functools import wraps
import utils

import etcd3
from etcd3.exceptions import ConnectionFailedError

from cfgm_common.exceptions import (DatabaseUnavailableError, NoIdError,
                                    VncError)
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel


def _handle_conn_error(func):
    @wraps(func)
    def wrapper(self, *args, **kwargs):
        start_time = datetime.datetime.now()
        try:
            return func(self, *args, **kwargs)
        except ConnectionFailedError as exc:
            if self._logger:
                msg = 'etcd connection down. Exception in {}'.format(str(func))
                self._logger(msg, level=SandeshLevel.SYS_ERR)
            raise DatabaseUnavailableError(
                'Error, %s: %s' % (str(exc), utils.detailed_traceback()))
        finally:
            if self.log_response_time:
                end_time = datetime.datetime.now()
                self.log_response_time(end_time - start_time)
    return wrapper


class SchemaTransformerEtcd(object):   # FIXME VncObjectEtcdClient

    _ETCD_DEFAULT_PREFIX = "/contrail/schema_transformer"
    _ETCD_SERVICE_CHAIN_OBJ_TYPE = "service_chain"

    def __init__(self, host, port, prefix=None, logger=None,
                 log_response_time=None, timeout=5, credentials=None):
        self._client = EtcdClient(host, port, timeout, credentials)
        if prefix:
            self._prefix = prefix
        else:
            self._prefix = self._ETCD_DEFAULT_PREFIX
        self._logger = logger
        self.log_response_time = log_response_time
        super(SchemaTransformerEtcd, self).__init__()   # FIXME

    def _key_prefix(self, obj_type):
        """Resources prefix for etcd.

        :param (str) obj_type: Type of resource
        :return: (str) full prefix ie: "/contrail/schema_transformer/service_chain/"
        """
        return "{prefix}/{type}/".format(prefix=self._prefix, type=obj_type)

    def _key_obj(self, obj_type, obj_id):
        """Resource key with resource prefix.

        :param (str) obj_type: Type of resource
        :param (str) obj_id: uuid of object
        :return: (str) full key ie: "/contrail/schema_transformer/service_chain/aaa-bbb-ccc"
        """
        key_prefix = self._key_prefix(obj_type)
        return "{prefix}{id}".format(prefix=key_prefix, id=obj_id)

    def _patch_encapsulate_value(self, obj):
        """Encaplsulate each obj in dictionary with single element having
        'value' key and `obj` value.
        This is required for backward compatibility.

        :param (obj) obj: Vanilla object from etcd
        :return: (dict) Obj with patched 'value' key
        """
        return {'value': obj}

    def allocate_service_chain_vlan(self, service_vm, service_chain):
        # Since vlan tag 0 is not valid, return 1
        return 1

    def free_service_chain_vlan(self, service_vm, service_chain):
        pass

    def get_route_target(self, ri_fq_name):
        return 0

    def alloc_route_target(self, ri_fq_name, zk_only=False):
        return 0

    def free_route_target(self, ri_fq_name):
        pass

    def get_service_chain_ip(self, sc_name):
        return 0

    def add_service_chain_ip(self, sc_name, ip_dict):
        pass

    def remove_service_chain_ip(self, sc_name):
        pass

    @_handle_conn_error
    def list_service_chain_uuid(self):
        """List all objects with 'service_chain' object type.

        :return: (gen) Generator of fetched objects
        """
        key = self._key_prefix(self._ETCD_SERVICE_CHAIN_OBJ_TYPE)
        response = self._client.get_prefix(key)
        return (self._patch_encapsulate_value(value) for key, value in response)

    @_handle_conn_error
    def add_service_chain_uuid(self, name, value):
        """Add an object with key created from combining prefix,
        'service_chain' and name.

        :param name (str): UUID of object
        :param value (Any): data to store, typically a string (object
                            serialized with jsonpickle.encode)
        """
        key = self._key_obj(self._ETCD_SERVICE_CHAIN_OBJ_TYPE, name)
        self._client.put(key, value)

    @_handle_conn_error
    def remove_service_chain_uuid(self, name):
        """Remove an object with key created from combining prefix,
        'service_chain' and name.

        :param name (str): UUID of object
        """
        key = self._key_obj(self._ETCD_SERVICE_CHAIN_OBJ_TYPE, name)
        self._client.delete(key)

    def get_bgpaas_port(self, port):
        return 0

    def alloc_bgpaas_port(self, name):
        return 0

    def free_bgpaas_port(self, port):
        pass


class EtcdClient(etcd3.Etcd3Client):
    def __init__(self, host, port=2379, timeout=5, credentials=None):
        self._host = host
        self._port = port
        self._timeout = timeout
        self._credentials = credentials
        self._conn_state = ConnectionStatus.INIT
        kwargs = {'host': self._host,
                  'port': self._port, 'timeout': self._timeout}
        if self._credentials:
            kwargs.update(self._credentials)
        super(EtcdClient, self).__init__(**kwargs)
        self._update_sandesh_status(ConnectionStatus.UP)

    def _update_sandesh_status(self, status, msg=''):
        self._conn_state = status
        ConnectionState.update(conn_type=ConnType.DATABASE,
                               name='etcd', status=status, message=msg,
                               server_addrs=["{}:{}".format(self._host, self._port)])

    # def get_prefix(self, key_prefix, sort_order=None, sort_target='key'):
    #     return self._client.get_prefix(key_prefix, sort_order=sort_order, sort_target=sort_target)
