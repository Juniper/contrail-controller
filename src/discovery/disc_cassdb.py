#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import re
from cfgm_common import jsonutils as json
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
from cfgm_common.vnc_cassandra import VncCassandraClient
from sandesh_common.vns.constants import DISCOVERY_SERVER_KEYSPACE_NAME, \
    CASSANDRA_DEFAULT_GC_GRACE_SECONDS

class DiscoveryCassandraClient(VncCassandraClient):
    _DISCOVERY_KEYSPACE_NAME = DISCOVERY_SERVER_KEYSPACE_NAME
    _DISCOVERY_CF_NAME = 'discovery'

    @classmethod
    def get_db_info(cls):
        db_info = [(cls._DISCOVERY_KEYSPACE_NAME, [cls._DISCOVERY_CF_NAME])]
        return db_info
    # end get_db_info

    def __init__(self, module, cass_srv_list, config_log, reset_config=False, db_prefix=None):
        self._debug = {
            'db_upd_oper_state': 0,
        }

        keyspaces = {
            self._DISCOVERY_KEYSPACE_NAME: [
                (self._DISCOVERY_CF_NAME, CompositeType(AsciiType(), UTF8Type(), UTF8Type()))
            ]
        }

        super(DiscoveryCassandraClient, self).__init__(
            cass_srv_list, db_prefix, keyspaces, None,
            config_log, reset_config=reset_config)

        DiscoveryCassandraClient._disco_cf = self._cf_dict[self._DISCOVERY_CF_NAME]
    #end __init__

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
            if service_type:
                data = self._disco_cf.get_range(start = service_type, finish = service_type,
                    column_start = col_name, column_finish = col_name)
            else:
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
                    column_finish = col_name, column_count = disc_consts.MAX_COL)
            except pycassa.NotFoundException:
                return None
            data = [(service_type, dict(clients))]
            entry_format_subscriber = True
        elif service_type:
            col_name = ('client', )
            try:
                clients = self._disco_cf.get(service_type, column_start = col_name,
                    column_finish = col_name, column_count = disc_consts.MAX_COL)
            except pycassa.NotFoundException:
                return None
            data = [(service_type, dict(clients))]
        else:
            col_name = ('client', )
            try:
                data = self._disco_cf.get_range(column_start=col_name,
                   column_finish = col_name, column_count = disc_consts.MAX_COL)
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
        self._disco_cf.remove(entry['service_type'],
            columns = [('service', entry['service_id'], 'service-entry')])
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
                column_finish = col_name, include_timestamp = True)
            # sort columns by timestamp (subs is array of (col_name, (value, timestamp)))
            subs = sorted(subs.items(), key=lambda entry: entry[1][1])
            # col_name = (client, cliend_id, service_id)
            # col_val  = (real-value, timestamp)
            data = None
            for col_name, col_val in subs:
                foo, client_id, service_id = col_name
                if service_id == disc_consts.CLIENT_TAG:
                    data = json.loads(col_val[0])
                    continue
                entry = json.loads(col_val[0])
                r.append((col_name[2], entry.get('expired', False)))
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
        self._disco_cf.remove(service_type,
            columns = [('subscriber', service_id, client_id)])
    # end

    # mark client subscription for deletion in the future. If client never came
    # back, entry would still get deleted due to TTL
    @cass_error_handler
    def mark_delete_subscription(self, service_type, client_id, service_id):
        col_name = ('client', client_id, service_id)
        x = self._disco_cf.get(service_type, columns = [col_name])
        data = [json.loads(val) for col,val in x.items()]
        entry = data[0]
        entry['expired'] = True
        self._disco_cf.insert(service_type, {col_name : json.dumps(entry)})

        col_name = ('subscriber', service_id, client_id)
        x = self._disco_cf.get(service_type, columns = [col_name])
        data = [json.loads(val) for col,val in x.items()]
        entry = data[0]
        entry['expired'] = True
        self._disco_cf.insert(service_type, {col_name : json.dumps(entry)})
    # end

    @cass_error_handler
    def db_update_service_entry_oper_state(self):
        col_name = ('service',)
        data = self._disco_cf.get_range(column_start = col_name, column_finish = col_name)
        for service_type, services in data:
            for col_name in services:
                (_, _, tag) = col_name
                if tag != 'service-entry':
                    continue
                col_value = services[col_name]
                entry = json.loads(col_value)
                if 'oper_state' not in entry:
                    entry['oper_state'] = 'up'
                    entry['oper_state_msg'] = ''
                    self._disco_cf.insert(service_type, {col_name : json.dumps(entry)})
                    self._debug['db_upd_oper_state'] += 1

    """
    read DSA rules
    'dsa_rule_entry': {
        u'subscriber': [{
            u'service_type': u'',
            u'service_id': u'',
            u'service_prefix': {u'ip_prefix': u'2.2.2.2', u'ip_prefix_len': 32},
            u'service_version': u''}],
        u'publisher': {
            u'service_type': u'',
            u'service_id': u'',
            u'service_prefix': {u'ip_prefix': u'1.1.1.1', u'ip_prefix_len': 32},
            u'service_version': u''}
    }
    """
    def read_dsa_config(self):
        try:
            obj_type = 'discovery-service-assignment'
            dsa_uuid = self.fq_name_to_uuid(obj_type,
                           ['default-discovery-service-assignment'])
            (ok, dsa_obj) = self.object_read(obj_type, [dsa_uuid])
        except Exception as e:
            return None

        dsa_rules = dsa_obj[0].get('dsa_rules', None)
        if dsa_rules is None:
            return None

        result = []
        for rule in dsa_rules:
            (ok, rule_obj) = self.object_read('dsa-rule', [rule['uuid']])
            entry = rule_obj[0]['dsa_rule_entry']
            if entry:
                result.append(entry)

        return result
