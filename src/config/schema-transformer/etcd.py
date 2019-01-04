# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
Schema transformer Etcd to store ids allocated by it
"""

import datetime
import uuid
from functools import wraps

import etcd3
import utils
from etcd3.exceptions import ConnectionFailedError

from cfgm_common.exceptions import (DatabaseUnavailableError, NoIdError,
                                    VncError)
from cfgm_common.vnc_etcd import VncEtcd
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel


class SchemaTransformerEtcd(object):   # FIXME VncObjectEtcdClient

    _ETCD_DEFAULT_PREFIX = "/contrail/schema_transformer"
    _ETCD_SERVICE_CHAIN_OBJ_TYPE = "service_chain"

    def __init__(self, host, port, prefix=None, logger=None,
                 log_response_time=None, timeout=5, credentials=None):
        if prefix:
            self._prefix = prefix
        else:
            self._prefix = self._ETCD_DEFAULT_PREFIX
        self._client = VncEtcd(host, port, self._prefix, logger,
                               log_response_time=log_response_time,
                               timeout=timeout, credentials=credentials)
        self._logger = logger
        self.log_response_time = log_response_time
        super(SchemaTransformerEtcd, self).__init__()   # FIXME

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

    def list_service_chain_uuid(self):
        """List all objects with 'service_chain' object type.

        :return: (gen) Generator of fetched objects
        """
        response = self._client.get_prefix_by_obj_type(
            self._ETCD_SERVICE_CHAIN_OBJ_TYPE)
        return (self._patch_encapsulate_value(value) for key, value in response)

    def add_service_chain_uuid(self, name, value):
        """Add an object with key created from combining prefix,
        'service_chain' and name.

        :param name (str): UUID of object
        :param value (Any): data to store, typically a string (object
                            serialized with jsonpickle.encode)
        """
        self._client.put_with_obj_type(
            self._ETCD_SERVICE_CHAIN_OBJ_TYPE, name, value)

    def remove_service_chain_uuid(self, name):
        """Remove an object with key created from combining prefix,
        'service_chain' and name.

        :param name (str): UUID of object
        """
        self._client.delete_with_obj_type(
            self._ETCD_SERVICE_CHAIN_OBJ_TYPE, name)

    def get_bgpaas_port(self, port):
        return 0

    def alloc_bgpaas_port(self, name):
        return 0

    def free_bgpaas_port(self, port):
        pass
