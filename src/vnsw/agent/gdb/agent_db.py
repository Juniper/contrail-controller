#
#  Copyright (c) 2016 Juniper Networks. All rights reserved.
#
#  agent_db.py
#  
#  gdb macros to dump the vrouter agent DB object/entries

import gdb

from libstdcxx.v6.printers import *
from boost.printers import *

class my_value(gdb.Value):
    def __init__(self, b):
        gdb.Value(b)
        self.type_name = str(b.type)

def print_db_entry(entry_ptr, entry):
    print(str(entry_ptr) + "      0x%08X" % (entry['flags']))

def print_db_entries_from_partition(partition, print_fn):
    tree_ref = my_value(gdb.parse_and_eval('(((DBTablePartition *)' + str(partition) + ')->tree_)'))
    tree = BoostIntrusiveSet(tree_ref)
    it = tree.children()
    try:
        while (it.node):
            entry_ptr = it.get_element_pointer_from_node_pointer()
            entry = next(it)[1]
            print_fn(entry_ptr, entry)
    except StopIteration:
        pass

def print_db_entries(db_table, print_fn = print_db_entry):
    """ Dumps DB table entries """
    table_ptr = gdb.parse_and_eval('(DBTable *)' + str(db_table))
    partitions = StdVectorPrinter('partitions_', table_ptr['partitions_'])
    it = partitions.children()
    print("  Entries in DB Table " + str(table_ptr) + "      ")
    print("--------------------------------------------")
    if (print_fn == print_db_entry):
        print("   Entry ptr      Entry flags       ")
        print("--------------------------------------------")
    try:
        while 1:
            partition = next(it)[1]
            print_db_entries_from_partition(partition, print_fn)
    except StopIteration:
        pass

def print_vrf_entry(entry_ptr, entry):
    vrf = entry.cast(gdb.lookup_type('VrfEntry'))
    print(str(entry_ptr) + "    %-20s    idx=%-4d    ref_count=%-4d   flags=%-4d rt_db=" % (vrf['name_']['_M_dataplus']['_M_p'], vrf['id_'], vrf['refcount_']['my_storage']['my_value'], vrf['flags']) + str(vrf['rt_table_db_'][int(gdb.parse_and_eval('Agent::INET4_UNICAST'))]) + " mcrt_db=" + str(vrf['rt_table_db_'][int(gdb.parse_and_eval('Agent::INET4_MULTICAST'))]) + " evpn_db=" + str(vrf['rt_table_db_'][int(gdb.parse_and_eval('Agent::EVPN'))]) + " bridge_db=" + str(vrf['rt_table_db_'][int(gdb.parse_and_eval('Agent::BRIDGE'))]) + " v6_rt_db=" + str(vrf['rt_table_db_'][int(gdb.parse_and_eval('Agent::INET6_UNICAST'))]))

def dump_vrf_entries():
    vrf_table = gdb.parse_and_eval('Agent::singleton_.vrf_table_')
    print_db_entries(vrf_table, print_vrf_entry)

