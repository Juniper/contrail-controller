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

def print_mpls_ksync_entry(entry_ptr, entry):
    kmpls = entry.cast(gdb.lookup_type(str(entry.dynamic_type)))
    print (str(entry_ptr) + "  label=%-5s  nh=%-5s   " % (kmpls['label_'], kmpls['nh_']))

def dump_ksync_mpls_entries():
    kmpls_table = gdb.parse_and_eval('Agent::singleton_->ksync_->mpls_ksync_obj_.px')
    print_ksync_entries(kmpls_table, print_mpls_ksync_entry)

def print_kintf_entry(entry_ptr, entry):
    kintf = entry.cast(gdb.lookup_type('InterfaceKSyncEntry'))
    print(str(entry_ptr) + "    idx=%-5d   name=%-20s   " % (kintf['index_'],\
                                                  kintf['interface_name_']['_M_dataplus']['_M_p']))
def dump_ksync_intf_entries():
    kintf_table = gdb.parse_and_eval('Agent::singleton_->ksync_->interface_ksync_obj_.px')
    print_ksync_entries(kintf_table, print_kintf_entry)

def print_kvrf_assign_entries(entry_ptr, entry):
    kvrf_assign = entry.cast(gdb.lookup_type('VrfAssignKSyncEntry'))
    print (str(entry_ptr) + "    id=%-5s     vlan_tag=%-5s    nh=%-5s   " % (kvrf_assign['vrf_id_'], kvrf_assign['vlan_tag_'], kvrf_assign['nh_']))

def dump_kvrf_assign_entries():
    kvrf_assign_table = gdb.parse_and_eval('Agent::singleton_->ksync_->vrf_assign_ksync_obj_.px')
    print_ksync_entries(kvrf_assign_table, print_kvrf_assign_entries)

def print_ksync_route_entry(entry_ptr, ptr):
    krt = entry.cast(gdb.lookup_type('RouteKSyncEntry'))
    ip = krt['addr_']['ipv4_address_']['addr_']['s_addr']
    print (str(entry_ptr) + "  %d.%d.%d.%d/%d  vrf=%d  label=%d nh=%d " % ((ip & 0xff),\
          (ip >> 8 & 0xff), (ip >> 16 & 0xff), (ip >> 24 & 0xff),\
          krt['plen_'], krt['vrf_id_'], krt['label_'], krt['nh_']['px']))

def dump_ksync_route_entries(table):
    ksync_uc_route_table = gdb.parse_and_eval(str(table))
    print_ksync_entries(ksync_uc_route_table, print_ksync_route_entry)

def dump_ksync_mc_route_entries(table):
    ksync_mc_route_table = gdb.parse_and_eval(str(table))
    print_ksync_entries(ksync_mc_route_table, print_ksync_route_entry)

def print_ksync_flow_entry(entry_ptr, entry):
    flow = entry.cast(gdb.lookup_type('FlowTableKSyncEntry'))
    print ( str(entry_ptr) + "  hash=0x%-8x  fp=%s \n" % (kflow['hash_id_'], kflow['flow_entry_']['px']))

def dump_ksync_flow_entries(table):
    pksync_entries = gdb.parse_and_eval(str(table))
    print_ksync_entries(pksync_entries, print_ksync_flow_entry)

def print_ksync_mirror_entry(entry, entry_ptr):
    kmirror_entry = entry.cast(gdb.lookup_type('MirrorKSyncEntry'))
    sip = mirror['sip_']['addr_']['s_addr']
    dip = mirror['dip_']['addr_']['s_addr']
    print (str(entry_ptr) + "   %d.%d.%d.%d:%d   %d.%d.%d.%d:%d nh=%s\n" % ((sip >> 8 & 0xff), (sip >> 16 & 0xff),\
           (sip >> 24 & 0xff), mirror['sport_'],\
           (dip & 0xff), (dip >> 8 & 0xff), (dip >> 16 & 0xff),\
           (dip >> 24 & 0xff), mirror['dport_'], mirror['nh_']['px'] ))

def dump_ksync_mirror_entries():
    mirror_entries = gdb.parse_and_eval('Agent::singleton_->ksync_->mirror_ksync_obj_.px')
    print_ksync_entries(mirror_entries, print_ksync_mirror_entry)

def dump_ksync_vxlan_entries():
    vxlan_entries = gdb.parse_and_eval('Agent::singleton_->ksync_->vxlan_ksync_obj_.px')
    print_ksync_entries(vxlan_entries, print_ksync_vxlan_entry)

def print_ksync_vxlan_entry(entry, entry_ptr):
    kvxlan = entry.cast(gdb.lookup_type('VxLanIdKSyncEntry'))
    print (str(entry_ptr) + "   nh=%s  label=%s\n" % (kvxlan['nh_']['px'], kvxlan['label_']))
