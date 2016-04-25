#
#  Copyright (c) 2016 Juniper Networks. All rights reserved.
#
#  agent_ksync.py
#
#  gdb macros to dump the vrouter agent KSync object/entries

import gdb

from libstdcxx.v6.printers import *
from boost.printers import *

class my_value(gdb.Value):
    def __init__(self, b):
        gdb.Value(b)
        self.type_name = str(b.type)

def print_ksync_entry(entry_ptr, entry):
    print(str(entry_ptr) + "      state=0x%08X" % (entry['state_']))

def print_ksync_entries(ksync_obj, print_fn = print_ksync_entry):
    tree_ref = my_value(gdb.parse_and_eval('(((KSyncDBObject *)' + str(ksync_obj) + ')->tree_)'))
    tree = BoostIntrusiveSet(tree_ref)
    it = tree.children()
    try:
        while (it.node):
            entry_ptr = it.get_element_pointer_from_node_pointer()
            entry = next(it)[1]
            print_fn(entry_ptr, entry)
    except StopIteration:
        pass

def print_nh_ksync_entry(entry_ptr, entry):
    knh = entry.cast(gdb.lookup_type('NHKSyncEntry'))
    print(str(entry_ptr) + "   idx=%-5d    type=%-4d    state=0x%08X" % (knh['index_'], knh['type_'], knh['state_']))

def dump_nh_ksync_entries():
    nh_table = gdb.parse_and_eval('Agent::singleton_->ksync_->nh_ksync_obj_.px')
    print_ksync_entries(nh_table, print_nh_ksync_entry)

