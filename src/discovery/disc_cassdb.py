#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import re
import json
import time
import gevent
import disc_consts
import disc_exceptions
from datetime import datetime
from gevent.coros import BoundedSemaphore

import pycassa
import pycassa.util
from pycassa.system_manager import *
from pycassa.util import *
from pycassa.types import *

class DiscoveryCassendraClient():

    def __init__(self, module, cass_srv_list, reset_config=False):
        self._disco_cf_name = 'discovery'
        self._keyspace_name = 'DISCOVERY_SERVER'
        self._reset_config = reset_config
        self._cassandra_init(cass_srv_list)

        self._debug = {
        }
    #end __init__

    # Helper routines for cassandra
    def _cassandra_init(self, server_list):

        # column name <table-name>, <id1>, <id2>
        disco_cf_info = (self._disco_cf_name, 
            CompositeType(AsciiType(), UTF8Type(), UTF8Type()), AsciiType())

        # 1. Ensure keyspace and schema/CFs exist
        self._cassandra_ensure_keyspace(server_list, self._keyspace_name,
                [disco_cf_info])

        pool = pycassa.ConnectionPool(self._keyspace_name,
                                      server_list, max_overflow=10,
                                      use_threadlocal=True, prefill=True,
                                      pool_size=10, pool_timeout=20,
                                      max_retries=-1, timeout=0.5)
        rd_consistency = pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM
        wr_consistency = pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM
        self._disco_cf = pycassa.ColumnFamily(pool, self._disco_cf_name,
                                              read_consistency_level = rd_consistency,
                                              write_consistency_level = wr_consistency)
    #end _cassandra_init

    def _cassandra_ensure_keyspace(self, server_list,
                                   keyspace_name, cf_info_list):
        # Retry till cassandra is up
        server_idx = 0
        num_dbnodes = len(server_list)
        connected = False
        while not connected:
            try:
                cass_server = server_list[server_idx]
                sys_mgr = SystemManager(cass_server)
                connected = True
            except Exception as e:
                # TODO do only for thrift.transport.TTransport.TTransportException
                server_idx = (server_idx + 1) % num_dbnodes
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
                                    {'replication_factor': str(num_dbnodes)})
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

    def get_debug_stats(self):
        return self._debug
    # end

    """
        various column names
        ('client', client_id, 'client-entry')
        ('subscriber', service_id, client_id)
        ('subscription', client_id, service_id)
        ('service', service_id, 'service-entry')
    """

    # decorator to catch connectivity error
    def cass_error_handler(func):
        def error_handler(*args, **kwargs):
            try:
                return func(*args,**kwargs)
            except (pycassa.pool.AllServersUnavailable,
                    pycassa.pool.MaximumRetryException):
                raise disc_exceptions.ServiceUnavailable()
            except Exception as e:
                raise
        return error_handler

    # return all publisher entries
    @cass_error_handler
    def service_entries(self, service_type = None):
        col_name = ('service',)
        try:
            data = self._disco_cf.get_range(column_start = col_name, column_finish = col_name)
            for service_type, services in data:
                for col_name in services:
                    col_value = services[col_name]
                    entry = json.loads(col_value)
                    col_name = ('subscriber', entry['service_id'],)
                    entry['in_use'] = self._disco_cf.get_count(service_type, 
                        column_start = col_name, column_finish = col_name)
                    yield(entry)
        except pycassa.pool.AllServersUnavailable:
            raise disc_exceptions.ServiceUnavailable()
            #raise StopIteration

    # return all clients
    def subscriber_entries(self):
        col_name = ('client',)
        data = self._disco_cf.get_range(column_start = col_name, column_finish = col_name)
        for service_type, clients in data:
            for col_name in clients:
                (foo, client_id, service_id) = col_name
                # skip pure client entry
                if service_id == disc_consts.CLIENT_TAG:
                    continue
                yield((client_id, service_type))
    # end

    # return all subscriptions
    @cass_error_handler
    def get_all_clients(self, service_type=None, service_id=None):
        r = []
        entry_format_subscriber = False
        if service_type and service_id:
            # ('subscriber', service_id, client_id)
            col_name = ('subscriber', service_id,)
            try:
                clients = self._disco_cf.get(service_type, column_start = col_name,
                    column_finish = col_name)
            except pycassa.NotFoundException:
                return None
            data = [(service_type, dict(clients))]
            entry_format_subscriber = True
        elif service_type:
            col_name = ('client', )
            try:
                clients = self._disco_cf.get(service_type, column_start = col_name,
                    column_finish = col_name)
            except pycassa.NotFoundException:
                return None
            data = [(service_type, dict(clients))]
        else:
            col_name = ('client', )
            try:
                data = self._disco_cf.get_range(column_start=col_name, column_finish=col_name)
            except pycassa.NotFoundException:
                return None

        for service_type, clients in data:
            rr = []
            for col_name in clients:
                if entry_format_subscriber:
                    (foo, service_id, client_id) = col_name
                else:
                    (foo, client_id, service_id) = col_name
                    # skip pure client entry
                    if service_id == disc_consts.CLIENT_TAG:
                        continue
                entry_str = clients[col_name]
                entry = json.loads(entry_str)
                rr.append((service_type, client_id, service_id,
                    entry['mtime'], entry['ttl']))
            # sort by modification time
            # rr = sorted(rr, key=lambda entry: entry[3])
            r.extend(rr)
        return r
    # end get_all_clients

    # update publisher entry
    @cass_error_handler
    def update_service(self, service_type, service_id, entry):
        self.insert_service(service_type, service_id, entry)
    # end

    @cass_error_handler
    def insert_service(self, service_type, service_id, entry):
        col_name = ('service', service_id, 'service-entry')
        self._disco_cf.insert(service_type, {col_name : json.dumps(entry)})
    # end insert_service

    # forget service and subscribers
    @cass_error_handler
    def delete_service(self, entry):
        col_name = ('service', entry['service_id'], 'service-entry')
        self._disco_cf.remove(entry['service_type'])
     #end delete_service

    # return service entry
    @cass_error_handler
    def lookup_service(self, service_type, service_id=None):
        try:
            if service_id:
                services = self._disco_cf.get(service_type, columns = [('service', service_id, 'service-entry')])
                data = [json.loads(val) for col,val in services.items()]
                entry = data[0]
                col_name = ('subscriber', service_id,)
                entry['in_use'] = self._disco_cf.get_count(service_type, 
                    column_start = col_name, column_finish = col_name)
                return entry
            else:
                col_name = ('service',)
                services = self._disco_cf.get(service_type, 
                    column_start = col_name, column_finish = col_name)
                data = [json.loads(val) for col,val in services.items()]
                for entry in data:
                    col_name = ('subscriber', entry['service_id'],)
                    entry['in_use'] = self._disco_cf.get_count(service_type, 
                        column_start = col_name, column_finish = col_name)
                return data
        except pycassa.NotFoundException:
            return None
    # end lookup_service

    @cass_error_handler
    def query_service(self, service_type):
        return self.lookup_service(service_type, service_id = None)
    # end

    # this is actually client create :-(
    @cass_error_handler
    def insert_client_data(self, service_type, client_id, blob):
        col_name = ('client', client_id, disc_consts.CLIENT_TAG)
        self._disco_cf.insert(service_type, {col_name : json.dumps(blob)})
    # end insert_client_data

    # insert a subscription (blob/ttl per service_type)
    @cass_error_handler
    def insert_client(self, service_type, service_id, client_id, blob, ttl):
        col_val = json.dumps({'ttl': ttl, 'blob': blob, 'mtime': int(time.time())})
        col_name = ('subscriber', service_id, client_id)
        self._disco_cf.insert(service_type, {col_name : col_val}, 
            ttl = ttl + disc_consts.TTL_EXPIRY_DELTA)
        col_name = ('client', client_id, service_id)
        self._disco_cf.insert(service_type, {col_name : col_val}, 
            ttl = ttl + disc_consts.TTL_EXPIRY_DELTA)
    # end insert_client

    # return client (subscriber) entry
    @cass_error_handler
    def lookup_client(self, service_type, client_id, subs = False):
        r = []
        col_name = ('client', client_id, )
        try:
            subs = self._disco_cf.get(service_type, column_start = col_name,
                column_finish = col_name)
            # col_name = client, cliend_id, service_id
            for col_name, col_val in subs.items():
                foo, client_id, service_id = col_name
                if service_id == disc_consts.CLIENT_TAG:
                    data = json.loads(col_val)
                    continue
                entry = json.loads(col_val)
                r.append((col_name[2], entry['blob']))
            return (data, r)
        except pycassa.NotFoundException:
            return (None, [])
    # end lookup_client

    # return all subscriptions for a given client
    @cass_error_handler
    def lookup_subscription(self, service_type, client_id):
        r = []
        col_name = ('client', client_id, )
        try:
            subs = self._disco_cf.get(service_type, column_start = col_name,
                column_finish = col_name)
            # col_name = subscription, cliend_id, service_id
            for col_name, col_val in subs.items():
                foo, client_id, bar = col_name
                if bar == disc_consts.CLIENT_TAG:
                    continue
                entry = json.loads(col_val)
                r.append((col_name[2], entry['blob']))
            return r
        except pycassa.NotFoundException:
            return None
    # end lookup_subscription

    # delete client subscription. 
    @cass_error_handler
    def delete_subscription(self, service_type, client_id, service_id):
        self._disco_cf.remove(service_type, 
            columns = [('client', client_id, service_id)])
        self._disco_cfg.remove(service_type,
            columns = [('subscriber', service_id, client_id)])
    # end

    # return tuple (service_type, client_id, service_id)
