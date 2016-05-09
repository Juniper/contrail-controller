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

def print_vn_entry(entry_ptr, entry):
    vn = entry.cast(gdb.lookup_type('VnEntry'))
    print(str(entry_ptr) + "    %-20s" % (vn['name_']['_M_dataplus']['_M_p']))

def dump_vn_entries():
    vn_table = gdb.parse_and_eval('Agent::singleton_.vn_table_')
    print_db_entries(vn_table, print_vn_entry)

def print_vm_entry(entry_ptr, entry):
    vm = entry.cast(gdb.lookup_type('VmEntry'))
    print(str(entry_ptr) + "    %-20s" % (vm['name_']['_M_dataplus']['_M_p']))

def dump_vm_entries():
    vm_table = gdb.parse_and_eval('Agent::singleton_.vm_table_')
    print_db_entries(vm_table, print_vm_entry)

def print_intf_entry(entry_ptr, entry):
    Intf = entry.cast(gdb.lookup_type(str(entry.dynamic_type)))
    print(str(entry_ptr) + "    %-20s    ref_count=%-4d   flags=%-4d" % (Intf['name_']['_M_dataplus']['_M_p'], Intf['refcount_']['my_storage']['my_value'], Intf['flags']))

def dump_intf_entries():
    intf_table = gdb.parse_and_eval('Agent::singleton_.intf_table_')
    print_db_entries(intf_table, print_vmi_entry)

def print_nh_entry(entry_ptr, entry):
    nh = entry.cast(gdb.lookup_type(str(entry.dynamic_type)))
    print (str(entry_ptr) + "    type=%-4d    flags=%-4d    ref=%-4d    valid=%-4d    policy=%-4d\n" % (nh['type_'], nh['flags'], nh['refcount_']['my_storage']['my_value'], nh['valid_'], nh['policy_']))

def dump_nh_entries():
    nh_table = gdb.parse_and_eval('Agent::singleton_.nh_table_')
    print_db_entries(nh_table, print_nh_entry)

def print_mpls_entry(entry_ptr, entry):
    mpls = entry.cast(gdb.lookup_type('MplsLabel'))
    print(str(entry_ptr) +  "    label=%-4x   nh=%d\n" % (mpls['label_'], mpls['nh_']['px']))

def dump_mpls_entries():
    mpls_table = gdb.parse_and_eval('Agent::singleton_.mpls_table_')
    print_db_entries(mpls_table, print_mpls_entry)

def print_vxlan_entry(entry_ptr, entry):
    vxlan = entry.cast(gdb.lookup_type('VxLanId'))
    print(str(entry_ptr) + "    label=%-4x   nh=%d\n" % (vxlan['vxlan_id_'], vxlan['nh_']['px']))

def dump_vxlan_entries():
    vxlan_table = gdb.parse_and_eval('Agent::singleton_.vxlan_table_')
    print_db_entries(vxlan_table, print_vxlan_entry)

def print_vrf_assign_entry(entry_ptr, entry):
    vrf_assign = entry.cast(gdb.lookup_type('VrfAssign'))
    print (str(entry_ptr) +  "    flags=%-4d    ref=%-4d    type=%-4d    tag=%-4d\n" % (va['flags'], va['refcount_']['my_storage']['my_value'], va['type'], va['vlan_tag_']))


def dump_vrf_assign_entries():
    vrf_assign_table = gdb.parse_and_eval('Agent::singleton_.vrf_assign_table_')
    print_db_entries(vrf_assign_table, print_vrf_assign_entry)

def print_acl_entry(entry_ptr, entry):
    acl = entry.cast(gdb.lookup_type('AclDBEntry'))
    print (str(entry_ptr) +  "     %s     ref=%d\n" % (acl['name_']['_M_dataplus']['_M_p'], acl['refcount_']['my_storage']['my_value']))

def dump_acl_entries():
    acl_table = gdb.parse_and_eval('Agent::singleton_.acl_table_')
    print_db_entries(acl_table, print_acl_entry)

def print_sg_entry(entry_ptr, entry):
    sg = entry.cast(gdb.lookup_type('SgEntry'))
    print ( str(entry_ptr) + "     %d    engress_acl=%s     ingress_acl=%s\n" % (sg['sg_id_'], sg['egress_acl_']['px'], sg['ingress_acl_']['px']))

def dump_sg_entries():
    sg_table = gdb.parse_and_eval('Agent::singleton_.sg_table_')
    print_db_entries(sg_table, print_sg_entry)

def print_uc_route_entry(entry_ptr, entry):
    rt = entry.cast(gdb.lookup_type('InetUnicastRouteEntry'))
    ip = rt['addr_']['ipv4_address_']['addr_']['s_addr']
    print ( str(entry_ptr) + "  %d.%d.%d.%d/%d\t\t flags=%d\n" % ((ip & 0xff),\
                                   (ip >> 8 & 0xff), (ip >> 16 & 0xff),\
                                   (ip >> 24 & 0xff), rt['plen_'], rt['flags']))

def print_mc_route_entry(entry_ptr, entry):
    rt = entry.cast(gdb.lookup_type(str(entry.dynamic_type)))
    ip = rt['dst_addr_']['addr_']['s_addr']
    sip = rt['src_addr_']['addr_']['s_addr']
    print (str(entry_ptr) +  "  %d.%d.%d.%d/%d.%d.%d.%d\t\t flags=%d\n" % ((ip & 0xff),\
                                   (ip >> 8 & 0xff), (ip >> 16 & 0xff),\
                                   (ip >> 24 & 0xff), (sip & 0xff),\
                                   (sip >> 8 & 0xff), (sip >> 16 & 0xff),\
                                   (sip >> 24 & 0xff), rt['flags']))

def print_l2_route_entry(entry_ptr, entry):
    rt = entry.cast(gdb.lookup_type('BridgeRouteEntry'))
    mac = rt['mac_']['addr_']
    print( str(entry_ptr) +  "  %02x:%02x:%02x:%02x:%02x:%02x\t\t flags=%d\n" % \
                ( (mac['ether_addr_octet'][0]), (mac['ether_addr_octet'][1]),\
                 (mac['ether_addr_octet'][2]), (mac['ether_addr_octet'][3]),\
                 (mac['ether_addr_octet'][4]), (mac['ether_addr_octet'][5]),\
                 rt['flags']))

def print_route_entry(entry_ptr, entry):
    path = entry.cast(gdb.lookup_type('AgentPath'))
    print ( "Path : %s  Peer : %s  NH : %s Label : %s\n" % (str(entry_ptr) , path['peer'], path['nh_']['px'], path['label_']))

def dump_uc_v4_route_entries(table):
   if table:
      uc_v4_route_table = gdb.parse_and_eval(str(table))
      print_db_entries(uc_v4_route_table, print_uc_route_entry)

def dump_mc_v4_route_entries(table):
   if table:
      mc_v4_route_table = gdb.parse_and_eval(str(table))
      print_db_entries(mc_v4_route_table, print_mc_route_entry)

def dump_l2_route_entries(table):
    if table:
      l2_route_table = gdb.parse_and_eval(str(table))
      print_db_entries(l2_route_table, print_l2_route_entry)

