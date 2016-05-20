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

def ipv4_to_string (val):
    addr = ""
    x = int(val)
    x1 = x & 0xFF
    x2 = (x & 0xFF00) >> 8
    x3 = (x & 0xFF0000) >> 16
    x4 = (x & 0xFF000000) >> 24
    ip = '%d.%d.%d.%d' %(x1, x2, x3, x4)
    return '<%-15s>' %(ip)

def default_filter(entry):
    return True

def print_db_entry(entry):
    print(str(entry.address) + "      0x%08X" % (entry['flags']))

def print_db_entries_from_partition(partition, print_fn, filter_fn):
    tree_ref = my_value(gdb.parse_and_eval('(((DBTablePartition *)' + str(partition) + ')->tree_)'))
    tree = BoostIntrusiveSet(tree_ref)
    it = tree.children()
    try:
        while (it.node):
            entry = next(it)[1]
            if filter_fn(entry):
                print_fn(entry)
    except StopIteration:
        pass

print_db_header = True
def print_db_entries(db_table, print_fn = print_db_entry, filter_fn = default_filter):
    """ Dumps DB table entries """
    table_ptr = gdb.parse_and_eval('(DBTable *)' + str(db_table))
    partitions = StdVectorPrinter('partitions_', table_ptr['partitions_'])
    it = partitions.children()
    if (print_db_header):
        print("  Entries in DB Table " + str(table_ptr) + "      ")
        print("--------------------------------------------")
        if (print_fn == print_db_entry):
            print("   Entry ptr      Entry flags       ")
            print("--------------------------------------------")
    try:
        while 1:
            partition = next(it)[1]
            print_db_entries_from_partition(partition, print_fn, filter_fn)
    except StopIteration:
        pass

def print_vrf_entry(entry):
    vrf = entry.cast(gdb.lookup_type('VrfEntry'))
    print(str(vrf.address) + "    %-20s    idx=%-4d    ref_count=%-4d   flags=%-4d rt_db=" % ((vrf['name_']), vrf['id_'], vrf['refcount_']['my_storage']['my_value'], vrf['flags']) + str(vrf['rt_table_db_'][int(gdb.parse_and_eval('Agent::INET4_UNICAST'))]) + " mcrt_db=" + str(vrf['rt_table_db_'][int(gdb.parse_and_eval('Agent::INET4_MULTICAST'))]) + " evpn_db=" + str(vrf['rt_table_db_'][int(gdb.parse_and_eval('Agent::EVPN'))]) + " bridge_db=" + str(vrf['rt_table_db_'][int(gdb.parse_and_eval('Agent::BRIDGE'))]) + " v6_rt_db=" + str(vrf['rt_table_db_'][int(gdb.parse_and_eval('Agent::INET6_UNICAST'))]))

def dump_vrf_entries(filter_fn = default_filter):
    vrf_table = gdb.parse_and_eval('Agent::singleton_.vrf_table_')
    print_db_entries(vrf_table, print_vrf_entry, filter_fn)

def print_vn_entry(entry):
    vn = entry.cast(gdb.lookup_type('VnEntry'))
    print(str(vn.address) + "    %-20s" % (vn['name_']))

def dump_vn_entries(filter_fn = default_filter):
    vn_table = gdb.parse_and_eval('Agent::singleton_.vn_table_')
    print_db_entries(vn_table, print_vn_entry, filter_fn)

def print_vm_entry(entry):
    vm = entry.cast(gdb.lookup_type('VmEntry'))
    print(str(vm.address) + "    %-20s" % (vm['name_']))

def dump_vm_entries(filter_fn = default_filter):
    vm_table = gdb.parse_and_eval('Agent::singleton_.vm_table_')
    print_db_entries(vm_table, print_vm_entry, filter_fn)

def print_intf_entry(entry):
    Intf = entry.cast(gdb.lookup_type(str(entry.dynamic_type)))
    print(str(Intf.address) + "    type = " + str(entry.dynamic_type)  + "    %-20s    ref_count=%-4d   flags=%-4d" % ((Intf['name_']), Intf['refcount_']['my_storage']['my_value'], Intf['flags']))

def dump_intf_entries(filter_fn = default_filter):
    intf_table = gdb.parse_and_eval('Agent::singleton_.intf_table_')
    print_db_entries(intf_table, print_intf_entry, filter_fn)

def print_nh_entry(entry):
    nh = entry.cast(gdb.lookup_type(str(entry.dynamic_type)))
    print (str(nh.address) + "    type=" + str(entry.dynamic_type) + "( %d )    flags=%-4d    ref=%-4d    valid=%-4d    policy=%-4d" % (nh['type_'], nh['flags'], nh['refcount_']['my_storage']['my_value'], nh['valid_'], nh['policy_']))

def dump_nh_entries(filter_fn = default_filter):
    nh_table = gdb.parse_and_eval('Agent::singleton_.nh_table_')
    print_db_entries(nh_table, print_nh_entry, filter_fn)

def print_mpls_entry(entry):
    mpls = entry.cast(gdb.lookup_type('MplsLabel'))
    print(str(mpls.address) +  "    label=%-4x   nh=%d" % (mpls['label_'], mpls['nh_']['px']))

def dump_mpls_entries(filter_fn = default_filter):
    mpls_table = gdb.parse_and_eval('Agent::singleton_.mpls_table_')
    print_db_entries(mpls_table, print_mpls_entry, filter_fn)

def print_vxlan_entry(entry):
    vxlan = entry.cast(gdb.lookup_type('VxLanId'))
    print(str(vxlan.address) + "    label=%-4x   nh=%d" % (vxlan['vxlan_id_'], vxlan['nh_']['px']))

def dump_vxlan_entries(filter_fn = default_filter):
    vxlan_table = gdb.parse_and_eval('Agent::singleton_.vxlan_table_')
    print_db_entries(vxlan_table, print_vxlan_entry, filter_fn)

def print_vrf_assign_entry(entry):
    vrf_assign = entry.cast(gdb.lookup_type('VrfAssign'))
    print (str(vrf_assign.address) +  "    flags=%-4d    ref=%-4d    type=%-4d    tag=%-4d" % (va['flags'], va['refcount_']['my_storage']['my_value'], va['type'], va['vlan_tag_']))

def dump_vrf_assign_entries(filter_fn = default_filter):
    vrf_assign_table = gdb.parse_and_eval('Agent::singleton_.vrf_assign_table_')
    print_db_entries(vrf_assign_table, print_vrf_assign_entry, filter_fn)

def print_acl_entry(entry):
    acl = entry.cast(gdb.lookup_type('AclDBEntry'))
    print (str(acl.address) +  "     %s     ref=%d" % (acl['name_'], acl['refcount_']['my_storage']['my_value']))

def dump_acl_entries(filter_fn = default_filter):
    acl_table = gdb.parse_and_eval('Agent::singleton_.acl_table_')
    print_db_entries(acl_table, print_acl_entry, filter_fn)

def print_sg_entry(entry):
    sg = entry.cast(gdb.lookup_type('SgEntry'))
    print (str(sg.address) + "     %d    engress_acl=%s     ingress_acl=%s" % (sg['sg_id_'], sg['egress_acl_']['px'], sg['ingress_acl_']['px']))

def dump_sg_entries(filter_fn = default_filter):
    sg_table = gdb.parse_and_eval('Agent::singleton_.sg_table_')
    print_db_entries(sg_table, print_sg_entry, filter_fn)

def print_uc_route_entry(entry):
    rt = entry.cast(gdb.lookup_type('InetUnicastRouteEntry'))
    ip = rt['addr_']['ipv4_address_']['addr_']['s_addr']
    print (str(rt.address) + "  %d.%d.%d.%d/%d\t\t flags=%d" % ((ip & 0xff),\
                                   (ip >> 8 & 0xff), (ip >> 16 & 0xff),\
                                   (ip >> 24 & 0xff), rt['plen_'], rt['flags']))

def print_mc_route_entry(entry):
    rt = entry.cast(gdb.lookup_type(str(entry.dynamic_type)))
    ip = rt['dst_addr_']['addr_']['s_addr']
    sip = rt['src_addr_']['addr_']['s_addr']
    print (str(rt.address) +  "  %d.%d.%d.%d/%d.%d.%d.%d\t\t flags=%d" % ((ip & 0xff),\
                                   (ip >> 8 & 0xff), (ip >> 16 & 0xff),\
                                   (ip >> 24 & 0xff), (sip & 0xff),\
                                   (sip >> 8 & 0xff), (sip >> 16 & 0xff),\
                                   (sip >> 24 & 0xff), rt['flags']))

def print_l2_route_entry(entry):
    rt = entry.cast(gdb.lookup_type('BridgeRouteEntry'))
    mac = rt['mac_']['addr_']
    print(str(rt.address) +  "  %02x:%02x:%02x:%02x:%02x:%02x\t\t flags=%d" % \
                ( (mac['ether_addr_octet'][0]), (mac['ether_addr_octet'][1]),\
                 (mac['ether_addr_octet'][2]), (mac['ether_addr_octet'][3]),\
                 (mac['ether_addr_octet'][4]), (mac['ether_addr_octet'][5]),\
                 rt['flags']))

def dump_uc_v4_route_entries(table, filter_fn = default_filter):
      uc_v4_route_table = gdb.parse_and_eval(str(table))
      print_db_entries(uc_v4_route_table, print_uc_route_entry, filter_fn)

def dump_mc_v4_route_entries(table, filter_fn = default_filter):
      mc_v4_route_table = gdb.parse_and_eval(str(table))
      print_db_entries(mc_v4_route_table, print_mc_route_entry, filter_fn)

def dump_l2_route_entries(table, filter_fn = default_filter):
      l2_route_table = gdb.parse_and_eval(str(table))
      print_db_entries(l2_route_table, print_l2_route_entry, filter_fn)

def print_ifmap_node_entry(entry):
    ifnode = entry.cast(gdb.lookup_type('IFMapNode'))
    print (str(ifnode.address) + "   name=%-40s" % str(ifnode['name_']))

def dump_ifmap_entries(table, filter_fn = default_filter):
    ifmap_table = gdb.parse_and_eval(str(table))
    print_db_entries(ifmap_table, print_ifmap_node_entry, filter_fn)

def print_iflink_entry(entry):
    iflink = entry.cast(gdb.lookup_type('IFMapLink'))
    left = iflink['node_']['left_node_']
    print ("Left %s  name=%-40s - " % str(left), left['name_'])
    right = iflink['node_']['right_node_']
    print ("Right %s  name=%-40s" % str(right), right['name_'])

def dump_ifmap_link_entries(table, filter_fn = default_filter):
    ifmap_table = gdb.parse_and_eval(str(table))
    print_db_entries(ifmap_table, print_iflink_entry, filter_fn)

def print_service_instance_entry(entry):
    svi = entry.cast(gdb.lookup_type('ServiceInstance'))
    prop = svi['properties_']
    print (str(svi.address) +  "   Uuid:%-20s ServiceType:%s VirtualisationType:%s VmiInside:%-20s VmiOutside:%-20s MacIn:%s MacOut:%s IpIn:%s IpOut:%s IpLenIn:%s IpLenOut:%s IfCount:%s LbPool:%-20s" % (
       prop['instance_id']['data'], prop['service_type'], prop['virtualization_type'], prop['vmi_inside']['data'], prop['vmi_outside']['data'], prop['mac_addr_inside'], prop['mac_addr_outside'],
       prop['ip_addr_inside'], prop['ip_addr_outside'], prop['ip_prefix_len_inside'], prop['ip_prefix_len_outside'], prop['interface_count'], prop['loadbalancer_id']['data']))

def dump_service_instance_entries(table, filter_fn = default_filter):
    svi_table = gdb.parse_and_eval(str(table))
    print_db_entries(svi_table, print_service_instance_entry, filter_fn)

def print_mirror_entry(entry):
    mir = entry.cast(gdb.lookup_type(str(entry.dynamic_type)))
    sip = ipv4_to_string(mir['sip_']['addr_']['s_addr'])
    dip = ipv4_to_string(mir['dip_']['addr_']['s_addr'])
    print(str(mir.address) + "    " + sip + "    " + dip + "    nh = " + str(mir['nh_']['px']))

def dump_mirror_entries(filter_fn = default_filter):
    mirror_table = gdb.parse_and_eval('Agent::singleton_.mirror_table_')
    print_db_entries(mirror_table, print_mirror_entry, filter_fn)

filter_vrf_pointer = gdb.parse_and_eval('(VrfEntry *)0')
def db_entry_filter_vrf(entry):
    global filter_vrf_pointer
    my_entry = entry.cast(gdb.lookup_type(str(entry.dynamic_type)))
    try:
        vrf = my_entry['vrf_']['px']
        if vrf == filter_vrf_pointer:
            return True
    except gdb.error:
        pass
    return False

def find_vrf_references(vrf_ptr):
    global filter_vrf_pointer
    global print_db_header
    print_db_header = False
    filter_vrf_pointer = gdb.parse_and_eval('(VrfEntry *)' + str(vrf_ptr))
    print("looking for VRF Delete Actor")
    if (int(filter_vrf_pointer['deleter_']['px']) != 0):
        if (filter_vrf_pointer['deleter_']['px'].dereference()['table_']['px'] == filter_vrf_pointer):
            print("Delete Actor in VRF is also holding a reference")
    print("looking for Agent Route Tables")
    table_count = int(gdb.parse_and_eval('Agent::ROUTE_TABLE_MAX'))
    for num in range(1, table_count):
        if (filter_vrf_pointer['rt_table_delete_bmap_'] & ( 1 << num) == 0):
            rt_table = filter_vrf_pointer['rt_table_db_'][num]
            print("Active route table = " + str(rt_table) + " table type = " + str(rt_table.dynamic_type))
    print("looking for Virtual Networks")
    dump_vn_entries(db_entry_filter_vrf)
    print("looking for Interfaces")
    dump_intf_entries(db_entry_filter_vrf)
    print("looking for Nexthops")
    dump_nh_entries(db_entry_filter_vrf)
    print("looking for Mirror Entries")
    dump_mirror_entries(db_entry_filter_vrf)
    print("looking for VRF Assign Entries")
    dump_vrf_assign_entries(db_entry_filter_vrf)
    print_db_header = True

def print_route_entry(entry_ptr, entry):
    path = entry.cast(gdb.lookup_type('AgentPath'))
    print ( "Path : %s  Peer : %s  NH : %s Label : %d\n" % (str(entry_ptr) , path['peer_'], path['nh_']['px'], path['label_']))

def dump_route_paths(entry):
    paths = my_value(gdb.parse_and_eval('(((Route *)' + str(entry) + ')->path_)'))
    path_list = BoostIntrusiveList(paths)
    it = path_list.children()
    try:
        while (it.node):
            element_ptr = it.get_element_pointer_from_node_pointer()
            element = next(it)[1]
            print( "Number of paths : %d\n" % path_list.get_size())
            print_route_entry(element_ptr, element)
    except StopIteration:
        pass

