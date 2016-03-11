#! /usr/bin/env /usr/bin/python
# -*- coding: utf-8 -*-
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# moveTables.py
# to move cassandra tables created by analytics form ContrailAnalytics
# keyspace with Thrift to ContrailAnalyticsCql keyspace with Cql
#

import uuid
import socket
import json
import numpy
import sys
import re
import copy
import random
from sandesh.viz.gendb.ttypes import DbDataType

import pycassa
from pycassa.columnfamily import ColumnFamily
from pycassa.types import *
from pycassa import *
import argparse
import datetime

from datetime import datetime
from cassandra.cluster import Cluster
from cassandra.query import BatchStatement
from sandesh.viz.constants import *
from sandesh.viz.constants import _VIZD_TABLE_SCHEMA, _VIZD_FLOW_TABLE_SCHEMA, _VIZD_STAT_TABLE_SCHEMA

'''
Dealing with no UUID serialization support in json
'''
from json import JSONEncoder
from uuid import UUID
JSONEncoder_olddefault = JSONEncoder.default
def JSONEncoder_newdefault(self, o):
    if isinstance(o, UUID): return str(o)
    return JSONEncoder_olddefault(self, o)
JSONEncoder.default = JSONEncoder_newdefault

parser = argparse.ArgumentParser()
group = parser.add_mutually_exclusive_group()
parser.add_argument("--cassandra_server_ip", "-s",
                    default = '127.0.0.1',
                    help = "cassandra server ip")
parser.add_argument("--cassandra_server_port", "-p",
                    type = int,
                    default = 9160,
                    help = "cassandra server port")
parser.add_argument("--max_batch_entry_count", "-w",
                    type = int,
                    default = 50,
                    help = "number of rows to be fetched")
group.add_argument("--copy_cf", help = "Copy tables from ContrailAnalytics to ContrailAnalyticsCql",
                    metavar='COLUMNFAMILY', default = None)
group.add_argument("--list_cf", help = "List all the column families",
                    action="store_true")
args = parser.parse_args()

import logging
logging.basicConfig()

log = logging.getLogger()
log.setLevel('INFO')

class SimpleClient:
    session = None

    def connect(self, nodes):
        cluster = Cluster(contact_points=nodes, protocol_version=3)
        metadata = cluster.metadata
        self.session = cluster.connect()
        log.info('Connected to cluster: ' + metadata.cluster_name)
        for host in metadata.all_hosts():
            log.info('Datacenter: %s; Host: %s; Rack: %s',
                host.datacenter, host.address, host.rack)

    def close(self):
        self.session.cluster.shutdown()
        self.session.shutdown()
        log.info('Connection closed.')


    def create_schema(self):
        self.session.execute("USE \"{0}\";".format(COLLECTOR_KEYSPACE_CQL))

    def load_data(self, batch):
        self.session.execute(batch)

    def query_schema(self, table):
        query = "SELECT * FROM {0}".format(table)
        results = self.session.execute(query)
        for row in results:
            print row
        log.info('Schema queried.')

server_and_port = args.cassandra_server_ip+':'+str(args.cassandra_server_port)
pool = pycassa.ConnectionPool(COLLECTOR_KEYSPACE, server_list=[server_and_port])
sysm = pycassa.system_manager.SystemManager(server_and_port)

def list_cf(keyspace = COLLECTOR_KEYSPACE):
    dict = sysm.get_keyspace_column_families(keyspace)
    print dict.keys()

def copy_table(name):
    found_table = False
    for t in table_list:
	print t.table_name, name
	if(t.table_name == name):
	    found_table = True
	    if(t.is_static == True):
		copy_static_table(name)
	    else:
		copy_dynamic_table(name)
	    break
    if found_table == False:
    	print 'Not sure, how to copy this table: ', name
    return

def _convert_to_cql_data(columns, col_num, thrift_data, data_index):
    if(DbDataType.Unsigned32Type == columns[col_num].datatype):
        if isinstance(thrift_data, tuple):
            return numpy.int32(thrift_data[data_index])
        else:
            return numpy.int32(thrift_data)
    elif(DbDataType.InetType == columns[col_num].datatype):
        if isinstance(thrift_data, tuple):
            ip = socket.inet_ntoa(hex(thrift_data[data_index] & 0xffffffff)[2:].zfill(8).decode('hex'))
        else:
            ip = socket.inet_ntoa(hex(thrift_data & 0xffffffff)[2:].zfill(8).decode('hex'))
        return ip
    else:
        if isinstance(thrift_data, tuple):
            return thrift_data[data_index]
        else:
            return thrift_data

def copy_static_table(table):
    c_ft = pycassa.ColumnFamily(pool, table)
    table_rows = (c_ft.get_range(include_ttl=True))

    command = "BEGIN BATCH INSERT INTO {0} (key".format(table)
    columns = []
    for t in table_list:
	if(t.table_name == table):
	    columns = t.columns
	    break
    field_dict = {}
    for column in columns:
	field_dict[column.name] = column.datatype

    num_rows_in_batch = 0
    rowKey = ''
    for rowKey,columns in table_rows:
	num_rows_in_batch += 1
	if(num_rows_in_batch % args.max_batch_entry_count == 0):
	    num_rows_in_batch = 0
	    command += 'APPLY BATCH;'
    	    client.session.execute(command)
    	    command = "BEGIN BATCH INSERT INTO {0} (key".format(table)
	if isinstance(rowKey, unicode):
	    rowKey = rowKey.encode('utf-8')
	values = [rowKey]
	ttl = 0
	for key in columns.keys():
	    value, ttl = columns.get(key)
	    if(DbDataType.Unsigned32Type == field_dict['key']):
		value = numpy.int32(value)
	    elif(DbDataType.InetType == field_dict['key']):
	    	ip = socket.inet_ntoa(hex(value & 0xffffffff)[2:].zfill(8).decode('hex'))
		value = ip
	    command = '{0}, "{1}"'.format(command, key)
	    values.append(value)
	command += ') VALUES '
	if(ttl):
	    command = "{0}{1} USING TTL {2};".format(command, tuple(values), ttl)
	else:
	    command = "{0}{1};".format(command, tuple(values))
    if(rowKey != ''):
    	command += 'APPLY BATCH;'
    	client.session.execute(command)
	    
def is_json(myjson):
  try:
    json_object = json.loads(myjson)
  except ValueError, e:
    return False
  return True

def copy_dynamic_table(table):
    c_ft = pycassa.ColumnFamily(pool, table)
    table_rows = (c_ft.get_range(include_ttl=True))

    command = "INSERT INTO {0} ("
    table_columns = []
    values = ''
    keyCount = 0
    for t in table_list:
	if(t.table_name == table):
	    table_columns = t.columns
	    for column in t.columns:
                if keyCount == 0:
		    command += column.name
		    values = ') VALUES (?'
                else:
		    command += ', ' + column.name
		    values += ', ?'
                if (column.key == True):    
		    keyCount += 1
	    break
    command += values + ') USING TTL ?'
    insert_flow = client.session.prepare(command.format(table))

    batch = BatchStatement()
    num_rows_in_batch = 0
    rowKey = ''
    for rowKey,columns in table_rows:
	key_value = []
	rowKey_size = 0
	if isinstance(rowKey, tuple):
	    for key in range(0, len(list(rowKey))):
		key_value.append(_convert_to_cql_data(table_columns, rowKey_size, rowKey, rowKey_size))
	    	rowKey_size += 1
            if(t.is_index_table == True):
                key_value.append(random.randrange(0, 16)) 
	else:
	    key_value.append(_convert_to_cql_data(table_columns, rowKey_size, rowKey, rowKey_size))
	    rowKey_size += 1
	for key in columns.keys():
	    value, ttl = columns.get(key)
	    jsonValue = value
	    if is_json(value) != True:
	    	jsonValue = json.dumps(value, ensure_ascii=False)
	    column_value = copy.deepcopy(key_value)
	    num_rows_in_batch += 1
	    if(num_rows_in_batch % args.max_batch_entry_count == 0):
	    	num_rows_in_batch = 0
    	    	client.load_data(batch)
	    	batch = BatchStatement()
	    for col_num in range(rowKey_size, len(table_columns)-1):
		column_value.append(_convert_to_cql_data(table_columns, col_num, key, col_num-rowKey_size))
	    column_value.append(jsonValue)
	    column_value.append(ttl if ttl else 7200)
	    
	    batch.add(insert_flow, tuple(column_value))
    if(rowKey != ''):
    	client.load_data(batch)

client = SimpleClient()
client.connect([args.cassandra_server_ip])
client.create_schema()
table_list = _VIZD_TABLE_SCHEMA + _VIZD_FLOW_TABLE_SCHEMA + _VIZD_STAT_TABLE_SCHEMA

dict = sysm.get_keyspace_column_families(COLLECTOR_KEYSPACE)
tables = dict.keys()

if args.copy_cf == None:
    startTime = datetime.utcnow()
    print 'Start time for copying: ', startTime
    for table in tables:
    	print 'Start time for table ', table, ': ', datetime.utcnow()
	copy_table(table)
    	print 'Finish time for table ', table, ': ', datetime.utcnow()
    endTime = datetime.utcnow()
    print 'Total time taken: ', endTime - startTime
else:
    copy_table(args.copy_cf)

if args.list_cf != False:
    list_cf()
