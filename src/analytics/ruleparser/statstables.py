#! /usr/bin/env /usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#


# statstables.py
# utility to create and simulate entry additions into stats tables
#
# UUID - Timestamp
# The timestamp is a 60-bit value.  For UUID version 1, this is
# represented by Coordinated Universal Time (UTC) as a count of 100-
# nanosecond intervals since 00:00:00.00, 15 October 1582 (the date of
# Gregorian reform to the Christian calendar).
#
# datetime/time - Timestamp
# These 2 modules use UNIX Epoch which is 1 January 1970
# The difference in 100ns between the 2 Epochs is 0x01b21dd213814000
#
# datetime object from timestamp
# datetime.datetime.fromtimestamp(timestamp)
#
# define timezone to derive from tzinfo and use to get datetime object
# corresponding to that timezone
#
# ZERO = timedelta(0)
# HOUR = timedelta(hours=1)
# 
#  A UTC class.
# 
# class UTC(tzinfo):
#    """UTC"""
# 
#    def utcoffset(self, dt):
#        return ZERO
# 
#    def tzname(self, dt):
#        return "UTC"
# 
#    def dst(self, dt):
#        return ZERO
#
# utc = UTC()
#
# datetime.datetime(year=2012, month=10, day=1, tzinfo=utc)
#
# datetime.datetime.utcnow() -- give time at UTC
# datetime.datetime.utcfromtimestamp(ts) -- give utc time given timestamp
#
# def UTCTimestampUsec()
#   epoch = datetime.datetime.utcfromtimestamp(0)
#   now = datetime.datetime.utcnow()
#   delta = now-epoch
#   return (int(delta.total_seconds()*1000000))
#

import readline # optional, will allow Up/Down/History in the console
import code
import random
import uuid
import time
import datetime

import pycassa
from pycassa.columnfamily import ColumnFamily
from pycassa.types import *
from pycassa import *

rowtimemin = 10
row100ns = rowtimemin*60*10000000

global cfflows, cfsipindex, cfdipindex, cfspindex, cfdpindex, cftsindex
global VNSwlist, VNlist, VMlist
global VNSwnum, VNnum, VMnum
VNSwnum = 5
VNnum = 5
VMnum = 5

def add_flow_setup(Luuid, VNSwID, SIP, DIP, SP, DP, SetupTS):
    cfflows.insert(Luuid, {'VNSwID':VNSwID, 'SIP':SIP, 'DIP':DIP, 'SP':SP, 'DP':DP, 'SetupTS':SetupTS})

def add_flow_teardown(Luuid, TeardownTS):
    cfflows.insert(Luuid, {'TeardownTS':TeardownTS})

def add_flow_stats(Luuid, VNSwID, SIP, DIP, SP, DP, Tuuid, Pkts, Bytes):
    time100ns = Tuuid.time
    rowtime = time100ns/row100ns
    cffulltupleindex.insert(rowtime, {(SIP[0], SIP[1], DIP[0], DIP[1], SP[0], SP[1], DP[0], DP[1], Tuuid):(Luuid, Pkts, Bytes)})
    cfsipindex.insert(rowtime, {(SIP[0], SIP[1], Tuuid):(Luuid, Pkts, Bytes)})
    cfdipindex.insert(rowtime, {(DIP[0], DIP[1], Tuuid):(Luuid, Pkts, Bytes)})
    cfspindex.insert(rowtime, {(SP[0], SP[1], Tuuid):(Luuid, Pkts, Bytes)})
    cfdpindex.insert(rowtime, {(DP[0], DP[1], Tuuid):(Luuid, Pkts, Bytes)})
    cftsindex.insert(rowtime, {(Tuuid):(Luuid, Pkts, Bytes)})

def initialize():
    """this function will initialize global variables that will be used
       when adding to the tables in the db
    """
    global VNSwlist, VNlist, VMlist
    global VNSwnum, VNnum, VMnum

    VNSwlist = list(uuid.uuid4() for i in range(VNSwnum))
    VNlist = list(uuid.uuid4() for i in range(VNnum))
    VMlist=list(list(0x10100000+0x0100*i+0x01*j for i in range(1,VMnum+1)) for j in range(1,VMnum+1))
    """
       for printing the vm list in hex use..
       print map(lambda x: map(hex,x), VMlist)
    """

class Flow:
    def __init__(self, Flowuuid, VNSw, VNs, IPs, VNd, IPd, prot, SP, DP):
        self.Flowuuid = Flowuuid
        self.VNSw = VNSw
        self.VNs = VNs
        self.IPs = IPs
        self.VNd = VNd
        self.IPd = IPd
        self.prot = prot
        self.SP = SP
        self.DP = DP
        self.Pkts = 0
        self.Bytes = 0

    def __eq__(self, other):
        return ((self.VNs == other.VNs) and
                (self.IPs == other.IPs) and
                (self.VNd == other.VNd) and
                (self.IPd == other.IPd))

Flowlist = []
def flow_setup():
    while 1:
        numVNSw = random.randint(0,VNSwnum-1)
        numVNs = random.randint(0,VNSwnum-1)
        VNSw = VNSwlist[numVNSw]
        VNs = VNlist[numVNs]
        IPs = VMlist[numVNs][numVNSw]
        VNd = VNlist[numVNs]
        numVNSwd = random.randint(0,VNSwnum-1)
        IPd = VMlist[numVNs][numVNSwd]
        if random.randint(0,1) == 0:
            prot = 6
        else:
            prot = 11
        SP = random.randint(1,50000)
        DP = random.randint(1,50000)
     
        Flowuuid = uuid.uuid4()
        aflow = Flow(Flowuuid, VNSw, VNs, IPs, VNd, IPd, prot, SP, DP)
        if (Flowlist.__contains__(aflow)):
            continue

        Flowlist.append(aflow)
        add_flow_setup(Flowuuid, VNSw, (VNs, IPs), (VNd, IPd), (prot, SP), (prot, DP), uuid.uuid1())
        return

def flow_teardown():
    if len(Flowlist) == 0:
        return;

    aflow = Flowlist[random.randint(0,len(Flowlist)-1)]
    add_flow_teardown(aflow.Flowuuid, uuid.uuid1())
    Flowlist.remove(aflow)
        
def flow_stats():
    if len(Flowlist) == 0:
        return;

    aflow = Flowlist[random.randint(0,len(Flowlist)-1)]
    pkts = random.randint(100, 1000)
    aflow.Pkts += pkts
    aflow.Bytes += pkts * 714
    Tuuid = uuid.uuid1()
    add_flow_stats(aflow.Flowuuid, aflow.VNSw, (aflow.VNs, aflow.IPs),
            (aflow.VNd, aflow.IPd), (aflow.prot, aflow.SP), (aflow.prot, aflow.DP),
            Tuuid, aflow.Pkts, aflow.Bytes)

class simulate():
    def __init__(self):
        self._cache = {'nsetups':0, 'nteardowns':0, 'nstats':0}
        self.SETUP, self.TEARDOWN, self.STATS = range(1,4)

    def __call__(self):
       while 1:
           a = random.randint(1,100)
           if a < 10:
               if (self._cache['nsetups'] % 10) == 0:
                   print "setup ", self._cache['nsetups']
               self._cache['nsetups'] += 1
               act = self.SETUP
           elif a < 20:
               if (self._cache['nteardowns'] % 10) == 0:
                   print "teardown ", self._cache['nteardowns']
               self._cache['nteardowns'] += 1
               act = self.TEARDOWN
           else:
               if (self._cache['nstats'] % 10) == 0:
                   print "stats ", self._cache['nstats']
               self._cache['nstats'] += 1
               act = self.STATS
    
           if act == self.SETUP:
               flow_setup()
           elif act == self.TEARDOWN:
               flow_teardown()
           else:
               flow_stats()
    
           time.sleep(1)

def flow_query(time1=None, time2=None, VNs=None, SIP=None, VNd=None, DIP=None):
    """time1 and time2 are in datetime format and their timestamp is unix epoch
        based, which is 0x01b21dd213814000 100ns ahead of uuid epoch"""
    if (time1 == None):
        time1 = datetime.datetime.now() - datetime.timedelta(minutes=10)
    unixtsin100ns = time.mktime(time1.timetuple())*10000000 + time1.microsecond*10
    rowtime1 = (int)((unixtsin100ns+0x01b21dd213814000)/row100ns)
    unixtsinsecs = float(unixtsin100ns)/10000000
    uuid1 = util.convert_time_to_uuid(unixtsinsecs)

    if time2 == None:
        time2 = time1 + datetime.timedelta(minutes=10)
    unixtsin100ns = time.mktime(time2.timetuple())*10000000 + time2.microsecond*10
    rowtime2 = (int)((unixtsin100ns+0x01b21dd213814000)/row100ns)
    unixtsinsecs = float(unixtsin100ns)/10000000
    uuid2 = util.convert_time_to_uuid(unixtsinsecs)

    rowtimel = [rowtime1]
    while (rowtime1 < rowtime2): 
        rowtime1 += 1
        rowtimel.append(rowtime1)

    result_d = {}
    for rowtime in rowtimel:
            column_start = uuid1
            column_finish = uuid2
            try:
                int_d = cftsindex.get(rowtime, column_start=column_start, column_finish=column_finish)
                result_d.update(int_d)
            except NotFoundException:
                continue
    return result_d

sysm = pycassa.system_manager.SystemManager()
try:
    pool = pycassa.ConnectionPool(keyspace='StatsSpace')
except pycassa.pool.AllServersUnavailable:
    print "Server unavailable"
    quit()
except:
    try:
        sysm.create_keyspace('StatsSpace', strategy_options={"replication_factor": "1"})
        pool = pycassa.ConnectionPool(keyspace='StatsSpace')
    except:
        print "creation of keyspace StatsSpace failed"
        quit()

IPType = CompositeType(LEXICAL_UUID_TYPE, INT_TYPE)
PortType = CompositeType(INT_TYPE, INT_TYPE)
FullTupleIndexType = CompositeType(LEXICAL_UUID_TYPE, INT_TYPE, LEXICAL_UUID_TYPE,
        INT_TYPE, INT_TYPE, INT_TYPE, INT_TYPE, INT_TYPE, TIME_UUID_TYPE)
SIPIndexType = CompositeType(LEXICAL_UUID_TYPE, INT_TYPE, TIME_UUID_TYPE)
DIPIndexType = CompositeType(LEXICAL_UUID_TYPE, INT_TYPE, TIME_UUID_TYPE)
PortIndexType = CompositeType(INT_TYPE, INT_TYPE, TIME_UUID_TYPE)
IndexValueType = CompositeType(LEXICAL_UUID_TYPE, LONG_TYPE, LONG_TYPE)

try:
    cfflows = pycassa.ColumnFamily(pool, 'StatsFlows')
except:
    int0 = 0
    sysm.create_column_family('StatsSpace', 'StatsFlows',
            key_validation_class=LEXICAL_UUID_TYPE,
            column_validation_classes={'SetupTS':TIME_UUID_TYPE,
            'VNSwID':LEXICAL_UUID_TYPE,
            'SIP':IPType,
            'DIP':IPType,
            'SP':PortType,
            'DP':PortType,
            'AggStats':CompositeType(LONG_TYPE, LONG_TYPE),
            'TeardownTS':TIME_UUID_TYPE})
    cfflows = pycassa.ColumnFamily(pool, 'StatsFlows')
    
try:
    cffulltupleindex = pycassa.ColumnFamily(pool, 'StatsFullTupleIndex')
except:
    sysm.create_column_family('StatsSpace', 'StatsFullTupleIndex',
            key_validation_class=LONG_TYPE,
            comparator_type=FullTupleIndexType,
            default_validation_class=IndexValueType)
    cffulltupleindex = pycassa.ColumnFamily(pool, 'StatsFullTupleIndex')

try:
    cfsipindex = pycassa.ColumnFamily(pool, 'StatsSIPIndex')

except:
    sysm.create_column_family('StatsSpace', 'StatsSIPIndex',
            key_validation_class=LONG_TYPE,
            comparator_type=SIPIndexType,
            default_validation_class=IndexValueType)
    cfsipindex = pycassa.ColumnFamily(pool, 'StatsSIPIndex')

try:
    cfdipindex = pycassa.ColumnFamily(pool, 'StatsDIPIndex')
except:
    sysm.create_column_family('StatsSpace', 'StatsDIPIndex',
            key_validation_class=LONG_TYPE,
            comparator_type=DIPIndexType,
            default_validation_class=IndexValueType)
    cfdipindex = pycassa.ColumnFamily(pool, 'StatsDIPIndex')

try:
    cfspindex = pycassa.ColumnFamily(pool, 'StatsSPIndex')
except:
    sysm.create_column_family('StatsSpace', 'StatsSPIndex',
            key_validation_class=LONG_TYPE,
            comparator_type=PortIndexType,
            default_validation_class=IndexValueType)
    cfspindex = pycassa.ColumnFamily(pool, 'StatsSPIndex')

try:
    cfdpindex = pycassa.ColumnFamily(pool, 'StatsDPIndex')
except:
    sysm.create_column_family('StatsSpace', 'StatsDPIndex',
            key_validation_class=LONG_TYPE,
            comparator_type=PortIndexType,
            default_validation_class=IndexValueType)
    cfdpindex = pycassa.ColumnFamily(pool, 'StatsDPIndex')

try:
    cftsindex = pycassa.ColumnFamily(pool, 'StatsTSIndex')
except:
    sysm.create_column_family('StatsSpace', 'StatsTSIndex',
            key_validation_class=LONG_TYPE,
            comparator_type=TIME_UUID_TYPE,
            default_validation_class=IndexValueType)
    cftsindex = pycassa.ColumnFamily(pool, 'StatsTSIndex')

initialize()

vars = globals().copy()
vars.update(locals())
shell = code.InteractiveConsole(vars)
shell.interact()

