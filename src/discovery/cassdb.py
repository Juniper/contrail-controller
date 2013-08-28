#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Layer that transforms VNC config objects to ifmap representation
"""
import gevent
import gevent.monkey
gevent.monkey.patch_all()

import sys
import time
from pprint import pformat

from lxml import etree, objectify
import StringIO
import re

import socket
import errno
import subprocess

import copy
import json
import uuid
import datetime
import pycassa
import pycassa.util
from pycassa.system_manager import *
from pycassa.util import *
from pycassa.types import *

class DiscoveryCassendraClient():

    def __init__(self, cass_srv_list, reset_config = False):

        self._services_cf_name = 'services'
        self._clients_cf_name = 'clients'
        self._keyspace_name = 'DISCOVERY_SERVICE'

        self.service_id_to_type = {}
        self._reset_config = reset_config
        self._cassandra_init(cass_srv_list)
        gevent.Greenlet.spawn(self.inuse_loop)
    #end __init__

    # Helper routines for cassandra
    def _cassandra_init(self, server_list):
        # 1. Ensure keyspace and schema/CFs exist

        # ('service', <service-id>)
        services_cf_info = (self._services_cf_name, 
            CompositeType(AsciiType(), UTF8Type()), AsciiType())
        # ('client', <service-id>, <client-id>)
        clients_cf_info = (self._clients_cf_name, 
            CompositeType(AsciiType(), UTF8Type(), UTF8Type()), AsciiType())
        self._cassandra_ensure_keyspace(server_list, self._keyspace_name,
                [services_cf_info, clients_cf_info])
        pool = pycassa.ConnectionPool(self._keyspace_name,
                                           server_list=server_list, max_overflow = -1)

        self._services_cf = pycassa.ColumnFamily(pool, self._services_cf_name)
        self._clients_cf  = pycassa.ColumnFamily(pool, self._clients_cf_name)
    #end _cassandra_init

    def _cassandra_ensure_keyspace(self, server_list,
                                   keyspace_name, cf_info_list):
        # Retry till cassandra is up
        connected = False
        while not connected:
            try:
                sys_mgr = SystemManager(server_list[0])
                connected = True
            except Exception as e:
                # TODO do only for thrift.transport.TTransport.TTransportException
                time.sleep(3)

        if self._reset_config:
            try:
                sys_mgr.drop_keyspace(keyspace_name)
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                # TODO verify only EEXISTS
                print "Warning! " + str(e)

        try:
            # TODO replication_factor adjust?
            sys_mgr.create_keyspace(keyspace_name, SIMPLE_STRATEGY,
                                    {'replication_factor': '1'})
        except pycassa.cassandra.ttypes.InvalidRequestException as e:
            # TODO verify only EEXISTS
            print "Warning! " + str(e)

        for cf_info in cf_info_list:
            try:
                (cf_name, comparator_type, validator_type) = cf_info
                sys_mgr.create_column_family(keyspace_name, cf_name, 
                        comparator_type = comparator_type, default_validation_class = validator_type)
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                # TODO verify only EEXISTS
                print "Warning! " + str(e)
    #end _cassandra_ensure_keyspace

    def build_service_id_to_type_map(self):
        for service_type, services in self._services_cf.get_range():
            for col, svc_info_json in services.items():
                (service, service_id) = col
                self.service_id_to_type[service_id] = service_type
    #end

    def insert_service(self, service_type, service_id, data):
        col_name = ('service', service_id)
        self._services_cf.insert(service_type, {col_name : json.dumps(data)})
        if service_id not in self.service_id_to_type:
            self.service_id_to_type[service_id] = service_type
    #end insert_service

    def lookup_service(self, service_type, service_id = None):
        try:
            if service_id:
                services = self._services_cf.get(service_type, columns = [('service', service_id)])
                data = [json.loads(val) for col,val in services.items()]
                return data[0]
            else:
                services = self._services_cf.get(service_type, column_start = ('service',))
                data = [json.loads(val) for col,val in services.items()]
                return data
        except pycassa.NotFoundException:
            return None
    #end lookup_service

    def get_all_services(self):
        r = []
        for service_type, services in self._services_cf.get_range():
            for col, svc_info_json in services.items():
                svc_info = json.loads(svc_info_json)
                r.append(svc_info)
        return r
    #end

    def pub_id_to_service(self, id):
        if id in self.service_id_to_type:
            return self.lookup_service(self.service_id_to_type[id], service_id = id)
        return None
    #end 

    def insert_client(self, service_type, service_id, client_id, data, ttl):
        col_name = ('subscriber', service_id, client_id)
        self._clients_cf.insert(service_type, {col_name : json.dumps(data)}, ttl = ttl)

        col_name = ('subscription', client_id, service_id)
        self._clients_cf.insert(service_type, {col_name : json.dumps(data)}, ttl = ttl)

    def lookup_subscribers(self, service_type, service_id):
        col_name = ('subscriber', service_id, )
        try:
            clients = self._clients_cf.get(service_type, column_start = col_name, 
                column_finish = col_name)
            data = [json.loads(val) for col,val in clients.items()]
            return data
        except pycassa.NotFoundException:
            return None
    #end lookup_subscribers

    def lookup_subscription(self, service_type, client_id):
        col_name = ('subscription', client_id, )
        try:
            clients = self._clients_cf.get(service_type, column_start = col_name, 
                column_finish = col_name)
            # [(('subscription', client_id, service_id), '{"ip_addr": "127.0.0.1", "port": "8443"}')]
            # return [(service_id, '{"ip_addr": "127.0.0.1", "port": "8443"}')]
            data = [(col[2], json.loads(val)) for col,val in clients.items()]
            return data
        except pycassa.NotFoundException:
            return None
    #end lookup_subscription

    def get_all_clients(self):
        col_name = ('subscription', )
        r = []
        for service_type, clients in self._clients_cf.get_range(column_start = col_name,
                column_finish = col_name):
            for col, info_json in clients.items():
                #info = json.loads(info_json)
                result = (service_type, col[1], col[2])
                r.append(result)
        return r

    # reset in-use count of clients for each service
    def inuse_loop(self):
        while True:
            for service_type, services in self._services_cf.get_range():
                for col, svc_info_json in services.items():
                    (service, service_instance) = col
                    # refetch info otherwise possible to use stale info such as heartbeat TS
                    # svc_info = json.loads(svc_info_json)
                    svc_info = self.lookup_service(service_type, service_id = service_instance)
                    # TODO touch service entry only if in_use count is non zero
                    subscribers = self.lookup_subscribers(service_type, service_instance)
               	    svc_info['in_use'] = len(subscribers) if subscribers else 0
                    self.insert_service(service_type, service_instance, svc_info)
                    gevent.sleep(10)


#end class DiscoveryCassendraClient
