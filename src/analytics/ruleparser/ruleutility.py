#! /usr/bin/env /usr/bin/python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

# ruleutility.py
# utility to allow read/list/write rulefiles into cassandra

import readline # optional, will allow Up/Down/History in the console
import code
import pycassa
import urllib

ruleengpy_found = 0
try:
    import ruleengpy
    ruleengpy_found = 1
except ImportError:
    print ""
    print "*** Import ruleengpy failed, cannot add rulefiles"
    print ""

pool = pycassa.ConnectionPool(keyspace='VizKeyspace')
cf = pycassa.ColumnFamily(pool, 'Rulefiles')

def list_summary():
    g=cf.get_range()
    l=list(g)
    print "Rulefile"
    print "--------"
    for li in l:
        lx,ly = li
        print lx

def list_detail(key=None):
    if key == None:
        list_detail_all()
    else:
        list_detail_one(key)

def list_detail_all():
    g=cf.get_range()
    l=list(g)
    for li in l:
        lx,ly = li
        for lyi in ly:
            print lx
            print "---------"
            print lyi
            print ly[lyi]

def list_detail_one(key):
    g=cf.get(key)
    print "data for row", key, "is as follows"
    for gi in g:
        if gi == 'rule-data':
            print gi
            print g[gi]
        else:
            print "unknown column"

def add_file(url_or_file):
    if (ruleengpy_found == 0):
        print "ruleengpy not available"
        return

    try:
        f = urllib.urlopen(url_or_file)
    except IOError:
        print "open-failed:", IOError.errno, IOError.strerror
        return

    ruledata = f.read()
    print ruledata
    ret = ruleengpy.check_rulebuf(ruledata, len(ruledata))
    if (ret != 0):
        print "check_rulebuf failed"
        return

    col = {}
    col['rule-data'] = ruledata
    cf.insert(url_or_file, col)

def remove_file(key):
    if (ruleengpy_found == 0):
        print "ruleengpy not available"
        return
    print "removing entry ", key
    cf.remove(key)

vars = globals().copy()
vars.update(locals())
shell = code.InteractiveConsole(vars)
shell.interact()

