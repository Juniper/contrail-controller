#! /usr/bin/env /usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# tabledump.py
# to show cassandra tables created by analytics
#

import readline # optional, will allow Up/Down/History in the console
import code
import random
import uuid
import time
import datetime
from prettytable import PrettyTable
import socket

import pycassa
from pycassa.columnfamily import ColumnFamily
from pycassa.types import *
from pycassa import *
import argparse

parser = argparse.ArgumentParser()
parser.add_argument("--cassandra_server_ip",
                    default = '127.0.0.1',
                    help = "cassandra server ip")
parser.add_argument("--cassandra_server_port",
                    type = int,
                    default = 9160,
                    help = "cassandra server port")
parser.add_argument("--utc", "-u",
                    default = None,
                    help = "print current utctimestamp")
args = parser.parse_args()

def UTCTimestampUsec():
    epoch = datetime.datetime.utcfromtimestamp(0)
    now = datetime.datetime.utcnow()
    delta = now-epoch
    return ((delta.days*24*3600*1000000+delta.seconds*1000000+delta.microseconds))

def convert_to_time_delta(time_str):
    num = int(time_str[:-1])
    if time_str.endswith('s'):
        return datetime.timedelta(seconds=num)
    elif time_str.endswith('m'):
        return datetime.timedelta(minutes=num)
    elif time_str.endswith('h'):
        return datetime.timedelta(hours=num)
    elif time_str.endswith('d'):
        return datetime.timedelta(days=num)

if args.utc != None:
    # Try now-+ format       
    time_str = args.utc
    if time_str == 'now':
        print UTCTimestampUsec()
    else:
        # Handle now-/+1h format
        if time_str.startswith('now'):
            td = convert_to_time_delta(time_str[len('now'):])
        else:
            # Handle -/+1h format
            td = convert_to_time_delta(time_str)
        
        utc_tstamp_usec = UTCTimestampUsec()
        print utc_tstamp_usec + ((td.microseconds + (td.seconds + td.days * 24 * 3600) * 10**6))

    quit()

server_and_port = args.cassandra_server_ip+':'+str(args.cassandra_server_port)
pool = pycassa.ConnectionPool('ContrailAnalytics', server_list=[server_and_port])
sysm = pycassa.system_manager.SystemManager(server_and_port)

def list_cf(keyspace = 'ContrailAnalytics'):
    dict = sysm.get_keyspace_column_families(keyspace)
    print dict.keys()



from datetime import datetime
def parse_time(s):
    try:
        ret = datetime.strptime(s, "%Y-%m-%d %H:%M:%S %Z")
    except ValueError:
        try:
            ret = datetime.strptime(s, "%Y-%m-%d %H:%M:%S")
        except ValueError:
            try:
                ret = datetime.strptime(s, "%Y-%m-%d")
            except ValueError:
                ret = datetime.utcfromtimestamp(float(s))
    return ret

# purge all data defore particular datatime given in format 
# %YYYY-%MM-%DD %HH:%MM:%SS %TZ
# or %YYYY-%MM-%DD %HH:%MM:%SS
# or %YYYY-%MM-%DD
def purge_old_data(before_time = "1970-01-01 00:00:00 UTC"):
    cutoff_time = parse_time(before_time)
    total_rows_deleted = 0 # total number of rows deleted
    table_list = sysm.get_keyspace_column_families('ContrailAnalytics')
    for table in table_list:
        # purge from index tables
        if ((table != 'MessageTable') and (table != 'FlowRecordTable') and (table != 'MessageTableTimestamp') and (table != 'SystemObjectTable')):
            print "deleting old records from table:" + table + "\n"
            per_table_deleted = 0 # total number of rows deleted from this row
            cf = pycassa.ColumnFamily(pool, table)
            cf_get = cf.get_range(row_count=1000000)
            l_result = list(cf_get)
            for table_row in l_result:
                t2 = table_row[0][0]
                # each row will have equivalent of 2^23 = 8388608 usecs
                row_time = datetime.utcfromtimestamp((float(t2)*8388608)/1000000)
                if (row_time < cutoff_time):
                    print "deleting row:" + str(table_row) + "\n"
                    cf.remove(table_row[0])
                    per_table_deleted +=1
                    total_rows_deleted +=1
            print "deleted " + str(per_table_deleted) + " rows from table:" + table
    print "total rows deleted:" + str(total_rows_deleted)

def show_cf(name='MessageTable', key=None, row_count=100, detail=0):
    if (name == 'FlowTable'):
        show_ft(key, row_count, detail)
        return

    cf = pycassa.ColumnFamily(pool, name)
    if (key == None):
        cf_get = cf.get_range(row_count=row_count)
        l_result = list(cf_get)

        for l in l_result:
            print l
    else:
        l_result = cf.get(key)
        print l_result

    print 'max row_count - %d, num elements = %d' %(row_count, len(l_result))

def get_substring(str):
    l = len(str)
    if l < 10:
     return str
    return str[0:5]+'..'+str[l-5:l]

def show_ft(key = None, row_count = 100, detail=0):
    c_ft = pycassa.ColumnFamily(pool, 'FlowTable')
    l_ft = list(c_ft.get_range(row_count = row_count))
    sl_ft=sorted(l_ft, key=lambda le: le[1].get('setup_time', 0))
    x = PrettyTable(['setup_time', 'flow_id', 'sourcevn', 'sourceip', 'destvn', 'destip', 'dir', 'prot', 'sprt', 'dprt'])
    for l in sl_ft:
        setuptime = l[1].get('setup_time', None)
        if not setuptime:
            continue
        if (setuptime > 135300693300):
            setuptime = setuptime / 1000000
        try:
            message_dt = datetime.datetime.fromtimestamp(setuptime)
        except:
            import pdb;pdb.set_trace()
        message_ts = message_dt.strftime('%Y-%m-%d %H:%M:%S')
        x.add_row([message_ts,
            str(l[0]) if detail else get_substring(str(l[0])),
            l[1]['sourcevn'] if detail else get_substring(l[1]['sourcevn']),
            socket.inet_ntoa(hex(l[1]['sourceip'] & 0xffffffff)[2:].zfill(8).decode('hex')),
            l[1]['destvn'] if detail else get_substring(l[1]['destvn']),
            socket.inet_ntoa(hex(l[1]['destip'] & 0xffffffff)[2:].zfill(8).decode('hex')),
            'ing' if l[1]['direction_ing']==1 else 'egr',
            l[1]['protocol'],
            l[1]['sport'],
            l[1]['dport']])
    print x
    print 'max row_count - %d, num elements = %d' %(row_count, len(l_ft))

vars = globals().copy()
vars.update(locals())
shell = code.InteractiveConsole(vars)
shell.interact()

