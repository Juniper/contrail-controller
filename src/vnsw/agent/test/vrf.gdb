#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

define print_intf_references
   set $__intf = (Interface *)((size_t)($Xnode) - (size_t)&(((Interface*)0)->node_))
   if $__intf->type_ == Interface::VM_INTERFACE
       set $__vm_intf = (VmInterface *)($__intf)
       printf "Interface %p\n", $__vm_intf
       p $__vm_intf->floating_ip_list_
       p $__vm_intf->service_vlan_list_
       p $__vm_intf->static_route_list_
   end
end

define print_intf_services
   pdb_table_entries Agent::singleton_.intf_table_ print_intf_references
end

define intf_entry_match
    set $__intf = (Interface *)((size_t)($Xnode) - (size_t)&(((Interface*)0)->node_))
    set $__m_vrf = $__intf->vrf_.px
    if $__m_vrf == $__vrf
        printf "Interface %p matches vrf %s\n", $__intf, $__vrf->name_._M_dataplus._M_p
    end
end

define find_intf_vrf_ref
   pdb_table_entries Agent::singleton_.intf_table_ intf_entry_match
end

define nh_entry_match
   set $__nh = (NextHop *) ((size_t)($Xnode) - (size_t)&(((NextHop *)0)->node_))
   if $__nh->type_ == NextHop::ARP
       set $__arp = (ArpNH *) ((size_t)($Xnode) - (size_t)&(((NextHop *)0)->node_))
       if $__arp->vrf_.px == $__vrf
           printf "Arp-NH %p refers to VRF %p\n", $__arp, $__vrf
       end
   end
   if $__nh->type_ == NextHop::INTERFACE
       set $__intf_nh = (InterfaceNH *) ((size_t)($Xnode) - (size_t)&(((NextHop *)0)->node_))
       if $__intf_nh->vrf_.px == $__vrf
           printf "Interface-NH %p refers to VRF %p\n", $__intf_nh, $__vrf
       end
   end
   if $__nh->type_ == NextHop::VRF
       set $__vrf_nh = (VrfNH *) ((size_t)($Xnode) - (size_t)&(((NextHop *)0)->node_))
       if $__vrf_nh->vrf_.px == $__vrf
           printf "Vrf-NH %p refers to VRF %p\n", $__vrf_nh, $__vrf
       end
   end
   if $__nh->type_ == NextHop::VLAN
       set $__vlan_nh = (VlanNH *) ((size_t)($Xnode) - (size_t)&(((NextHop *)0)->node_))
       if $__vrf_nh->vrf_.px == $__vrf
           printf "Vrf-NH %p refers to VRF %p\n", $__vlan_nh, $__vrf
       end
   end
   if $__nh->type_ == NextHop::TUNNEL
       set $__tunnel = (TunnelNH *) ((size_t)($Xnode) - (size_t)&(((NextHop *)0)->node_))
       if $__tunnel->vrf_.px == $__vrf
           printf "Tunnel-NH %p refers to VRF %p\n", $__tunnel, $__vrf
       end
   end
   if $__nh->type_ == NextHop::MIRROR
       set $__mirror = (MirrorNH *) ((size_t)($Xnode) - (size_t)&(((NextHop *)0)->node_))
       if $__mirror->vrf_.px == $__vrf
           printf "Mirror-NH %p refers to VRF %p\n", $__mirror, $__vrf
       end
   end
   if $__nh->type_ == NextHop::COMPOSITE
       set $__comp = (CompositeNH *) ((size_t)($Xnode) - (size_t)&(((NextHop *)0)->node_))
       if $__comp->vrf_.px == $__vrf
           printf "Composite-NH %p refers to VRF %p\n", $__comp, $__vrf
       end
   end
end

define find_nh_vrf_ref
   pdb_table_entries Agent::singleton_.nh_table_ nh_entry_match
end

define vrf_assign_entry_match
    set $__va = (VrfAssign *)((size_t)($Xnode) - (size_t)&(((VrfAssign*)0)->node_))
    set $__m_vrf = $__va->vrf_.px
    if $__m_vrf == $__vrf
        printf "VRF Assign entry %p matches vrf %s\n", $_va, $__vrf->name_._M_dataplus._M_p
    end
end

define find_vrf_assign_vrf_ref
   pdb_table_entries Agent::singleton_.mirror_table_ vrf_assign_entry_match
end

define mirror_entry_match
    set $__mirror = (MirrorEntry *)((size_t)($Xnode) - (size_t)&(((MirrorEntry*)0)->node_))
    set $__m_vrf = $__mirror->vrf_.px
    if $__m_vrf == $__vrf
        printf "Mirror entry %p matches vrf %s\n", $__mirror, $__vrf->name_._M_dataplus._M_p
    end
end

define find_mirror_vrf_ref
   pdb_table_entries Agent::singleton_.mirror_table_ mirror_entry_match
end

define find_vrf_ref
   if $argc != 1
       help find_vrf_ref <vrf>
   else 
       set $__vrf = (VrfEntry *) $arg0
       printf "Unicast DB Count : "
       get_table_count $__vrf->rt_table_db_[0]
       printf "\n"
       printf "Multicast DB Count : ", 
       get_table_count $__vrf->rt_table_db_[1]
       printf "\n"
       printf "Layer2 DB Count : ", 
       get_table_count $__vrf->rt_table_db_[2]
       printf "\n"
       find_intf_vrf_ref $__vrf
       find_nh_vrf_ref $__vrf
       find_vrf_assign_vrf_ref $__vrf
       find_mirror_vrf_ref $__vrf
       print_intf_services $__vrf
   end
end
