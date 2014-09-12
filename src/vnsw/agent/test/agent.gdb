#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

define nh_entry_format
    set $__nh = (NextHop *) ((size_t)($Xnode) - (size_t)&(((NextHop*)0)->node_))
    printf "%p    type=%-4d    flags=%-4d    ref=%-4d    valid=%-4d    policy=%-4d\n", $__nh, $__nh->type_,\
           $__nh->flags, $__nh->refcount_->rep->value, $__nh->valid_, $__nh->policy_
end

define dump_nh_entries
   pdb_table_entries Agent::singleton_.nh_table_ nh_entry_format
end

define intf_entry_format
   set $__intf = (Interface *)((size_t)($Xnode) - (size_t)&(((Interface*)0)->node_))
   printf "%p    %-20s    flags=%-4d   ref=%-4d\n", $__intf, $__intf->name_._M_dataplus._M_p,\
           $__intf->flags, $__intf->refcount_->rep->value
end

define dump_intf_entries
   pdb_table_entries Agent::singleton_.intf_table_ intf_entry_format
end

define mpls_entry_format
    set $__mpls = (MplsLabel *)((size_t)($Xnode) - (size_t)&(((MplsLabel*)0)->node_))
    printf "%p    label=%-4x   nh=%p\n", $__mpls, $__mpls->label_, $__mpls->nh_.px
end
  
define dump_mpls_entries
   pdb_table_entries Agent::singleton_.mpls_table_ mpls_entry_format
end

define uc_route_entry_format
    set $__rt = (Inet4UnicastRouteEntry*)((size_t)($Xnode) - (size_t)&(Route::node_))
    set $__ip = $__rt->addr_.addr_.s_addr
    printf "%p  %d.%d.%d.%d/%d\t\t flags=%d\n", $__rt, ($__ip & 0xff),\
                                   ($__ip >> 8 & 0xff), ($__ip >> 16 & 0xff),\
                                   ($__ip >> 24 & 0xff), $__rt->plen_, $__rt->flags
end

define mc_route_entry_format
    set $__rt = (Inet4MulticastRouteEntry*)((size_t)($Xnode) - (size_t)&(Route::node_))
    set $__ip = $__rt->dst_addr_.addr_.s_addr
    set $__sip = $__rt->src_addr_.addr_.s_addr
    printf "%p  %d.%d.%d.%d/%d.%d.%d.%d\t\t flags=%d\n", $__rt, ($__ip & 0xff),\
                                   ($__ip >> 8 & 0xff), ($__ip >> 16 & 0xff),\
                                   ($__ip >> 24 & 0xff), ($__sip & 0xff),\
                                   ($__sip >> 8 & 0xff), ($__sip >> 16 & 0xff),\
                                   ($__sip >> 24 & 0xff), $__rt->flags
end

define l2_route_entry_format
    set $__rt = (Layer2RouteEntry*)((size_t)($Xnode) - (size_t)&(Route::node_))
    set $__mac = $__rt->mac_
    printf "%p  %02x:%02x:%02x:%02x:%02x:%02x\t\t flags=%d\n", $__rt,\
                 ($__mac.ether_addr_octet[0]), ($__mac.ether_addr_octet[1]),\
                 ($__mac.ether_addr_octet[2]), ($__mac.ether_addr_octet[3]),\
                 ($__mac.ether_addr_octet[4]), ($__mac.ether_addr_octet[5]),\
                  $__rt->flags
end

define dump_uc_v4_route_entries
   if $argc != 1
       help dump_uc_v4_route_entries
   else 
       pdb_table_entries $arg0 uc_route_entry_format
   end 
end

define dump_mc_v4_route_entries
   if $argc != 1
       help dump_mc_v4_route_entries
   else 
       pdb_table_entries $arg0 mc_route_entry_format
   end 
end

define dump_l2_route_entries
   if $argc != 1
       help dump_l2_route_entries
   else 
       pdb_table_entries $arg0 l2_route_entry_format
   end 
end

define dump_route_paths
   if $argc != 1
       help dump_route_paths
   else
       set $__rt = (Route *) $arg0
       set $__path_list = &$__rt->path_
       set $__count = $__path_list->data_.root_plus_size_.size_
       printf "Number of paths : %d\n", $__count
       set $__path = (AgentPath *)((size_t)$__path_list->data_.root_plus_size_.root_ - 8)
       while $__count >= 1
           printf "Path : %p  Peer : %p  NH : %p Label : %d\n", $__path, $__path->peer_, $__path->nh_.px, $__path->label_
           set $__path = (AgentPath *)((size_t)$__path->node_->next_ - 8)
           set $__count--
       end
   end
end

document dump_uc_v4_route_entries
     Prints all route entries in given table 
     Syntax: dump_uc_v4_route_entries <table>: Prints all route entries in UC v4 route table
end

document dump_mc_v4_route_entries
     Prints all route entries in given table 
     Syntax: dump_mc_v4_route_entries <table>: Prints all route entries in MC v4 route table
end

document dump_l2_route_entries
     Prints all L2 route entries in given table 
     Syntax: dump_l2_route_entries <table>: Prints all route entries in L2 route table
end

document dump_route_paths
     Prints all paths in a route entry
     Syntax: dump_route_paths <route>
end

define vrf_entry_format
    set $__vrf = (VrfEntry *)((size_t)($Xnode) - (size_t)&(((VrfEntry *)0)->node_))
    printf "%p    %-20s    idx=%-4d    ref_count=%-4d   flags=%-4d rt_db=%p mcrt_db=%p layer2_db=%p\n", $__vrf,\
           $__vrf->name_._M_dataplus._M_p, $__vrf->id_, $__vrf->refcount_->rep->value,\
           $__vrf->flags, $__vrf->rt_table_db_[0], $__vrf->rt_table_db_[1], \
           $__vrf->rt_table_db_[2]
end

define dump_vrf_entries
    pdb_table_entries Agent::singleton_.vrf_table_ vrf_entry_format
end

define vn_entry_format
    set $__vn = (VnEntry *)((size_t)($Xnode) - (size_t)&(((VnEntry*)0)->node_))
    printf "%p    %-20s  %-20s\n", $__vn, $__vn->name_._M_dataplus._M_p,\
                                   $__vn->uuid_->data
end

define dump_vn_entries
   pdb_table_entries Agent::singleton_.vn_table_ vn_entry_format
end

define vm_entry_format
    set $__vm = (VmEntry *)((size_t)($Xnode) - (size_t)&((VmEntry*)0)->node_)
    printf "%p    %-20s\n", $__vm, $__vm->uuid_->data
end

define dump_vm_entries
   pdb_table_entries Agent::singleton_.vm_table_ vm_entry_format
end

define vxlan_entry_format
    set $__vxlan = (VxLanId *)((size_t)($Xnode) - (size_t)&(VxLanId::node_))
    printf "%p    label=%-4x   nh=%p\n", $__vxlan, $__vxlan->vxlan_id_, $__vxlan->nh_.px
end
  
define dump_vxlan_entries
   pdb_table_entries Agent::singleton_.vxlan_table_ vxlan_entry_format
end
 
define mirror_entry_format
    set $__mirror = (MirrorEntry *)((size_t)($Xnode) - (size_t)&(((MirrorEntry*)0)->node_))
    set $__sip = $__mirror->sip_.addr_.s_addr
    set $__dip = $__mirror->dip_.addr_.s_addr
    printf "%p   %d.%d.%d.%d:%d   %d.%d.%d.%d:%d nh=%p\n", $__mirror,\
            ($__sip & 0xff), ($__sip >> 8 & 0xff), ($__sip >> 16 & 0xff),\
            ($__sip >> 24 & 0xff), $__mirror->sport_,\
            ($__dip & 0xff), ($__dip >> 8 & 0xff), ($__dip >> 16 & 0xff),\
            ($__dip >> 24 & 0xff), $__mirror->dport_, $__mirror->nh_.px
end

define dump_mirror_entries
   pdb_table_entries Agent::singleton_.mirror_table_ mirror_entry_format
end

define vrf_assign_entry_format
    set $__va = (VrfAssign *) ((size_t)($Xnode) - (size_t)&(VrfAssign::node_))
    printf "%p    flags=%-4d    ref=%-4d    type=%-4d    tag=%-4d\n", \
        $__va, $__va->flags, $__va->refcount_->rep->value, $__va->type, $__va->vlan_tag_
end

define dump_vrf_assign_entries
   pdb_table_entries Agent::singleton_.vrf_assign_table_ vrf_assign_entry_format
end

define acl_entry_format
    set $__acl = (AclDBEntry *)((size_t)($Xnode) - (size_t)&(AclDBEntry::node_))
    printf "%p     %s     ref=%d\n", $__acl, $__acl->name_._M_dataplus._M_p, \
                                     $__acl->refcount_->rep->value
end

define dump_acl_entries
   pdb_table_entries Agent::singleton_.acl_table_ acl_entry_format
end

define sg_entry_format
    set $__sg = (SgEntry *)((size_t)($Xnode) - (size_t)&(AclDBEntry::node_))
    printf "%p     %d     acl=%p\n", $__sg, $__sg->sg_id_, $__sg->acl_.px
end

define dump_sg_entries
   printf "Entry             Id      Acl\n"
   pdb_table_entries Agent::singleton_.sg_table_ sg_entry_format
end

define ifnode_entry_format
     set $__ifnode = (IFMapNode *) ((size_t)$Xnode - (size_t)&(IFMapNode::node_))
     printf"%p  name=%-40s\n", $__ifnode, $__ifnode->name_._M_dataplus._M_p
end

define dump_ifmap_entries
    if $argc != 1
        help dump_ifmap_entries
    else    
        pdb_table_entries $arg0 ifnode_entry_format
    end
end

define iflink_entry_format
    set $__iflink = (IFMapLink *) ((size_t)$Xnode - (size_t)&(IFMapLink::node_))

    set $left = $__iflink->left_node_
    printf"Left %p  name=%-40s - ", $left, $left->name_._M_dataplus._M_p
    set $right = $__iflink->right_node_
    printf"Right %p  name=%-40s\n", $right, $right->name_._M_dataplus._M_p
    
end

define dump_ifmap_link_entries
    pdb_table_entries $arg0 iflink_entry_format

document dump_ifmap_entries
     Prints name of all IFMAP nodes in given table
     Syntax: pdb_ifmap_entries <table>: Prints all entries in IFMAP table
end

define pwait_tree
    if $argc != 1
        help pwait_tree
    else
        # set the node equal to first node and end marker
        set $Xtree = &((KSyncSock *)$arg0)->wait_tree_.tree_
        set $Xnode = $Xtree->data_.node_plus_pred_.header_plus_size_.header_.left_
        set $Xend = &($Xtree->data_.node_plus_pred_.header_plus_size_.header_)

        set $Xtotal_len = (int)0

        while $Xnode != $Xend
            set $Kentry = (IoContext *) ((size_t)$Xnode - (size_t)&(IoContext::node_))
            printf " IoContext = (KSyncIoContext *) %p      msg_len = %4d\n", $Kentry, $Kentry->msg_len_
            set $Xtotal_len = $Xtotal_len + $Kentry->msg_len_
            grbtree_next_node
        end

        printf "Total Length of pending messages %d bytes \n", $Xtotal_len
    end
end

document pwait_tree
     Prints entries in Ksync object set
     Syntax: pwait_tree <KSyncSock *>
end

define pksync_entries
    if $argc != 1 && $argc != 2
        help pksync_entries
    else
        # set the node equal to first node and end marker
        set $Xtree = &((RouteKSyncObject *)$arg0)->tree_.tree_
        set $Xnode = $Xtree->data_.node_plus_pred_.header_plus_size_.header_.left_
        set $Xend = &($Xtree->data_.node_plus_pred_.header_plus_size_.header_)

        while $Xnode != $Xend
            if $argc == 2
                $arg1
            else
                set $Kentry = (KSyncEntry *) ((size_t)$Xnode - (size_t)&(KSyncEntry::node_))
                printf "  %p      state=0x%08X\n", $Kentry, $Kentry->state_
            end
            grbtree_next_node
        end
    end
end

document pksync_entries
     Prints entries in Ksync object set
     Syntax: pksync_entries <table> <format_fn>
end

define knh_entry_format
    set $__knh = (NHKSyncEntry *)((size_t)$Xnode - (size_t)&(KSyncEntry::node_))
    printf"%p   idx=%-5d    type=%-4d    state=", $__knh, $__knh->index_, $__knh->type_
    print $__knh->state_
end

define dump_ksync_nh_entries
    pksync_entries NHKSyncObject::singleton_ knh_entry_format
end

define kmpls_entry_format
    set $__kmpls = (MplsKSyncEntry *)((size_t)$Xnode - (size_t)&(KSyncEntry::node_))
    printf"%p  idx=%-5d  label=%-5d  nh=%-5d   ", $__kmpls, $__kmpls->index_,\
                                                      $__kmpls->label_,$__kmpls->nh_->index_
    print $__knh->state_
end

define dump_ksync_mpls_entries
    pksync_entries MplsKSyncObject::singleton_ kmpls_entry_format
end

define kintf_entry_format
    set $__kintf = (InterfaceKSyncEntry *)((size_t)$Xnode - (size_t)&(KSyncEntry::node_))
    printf"%p    idx=%-5d   name=%-20s   ", $__kintf, $__kintf->index_,\
                                                  $__kintf->ifname_._M_dataplus._M_p
    print $__kintf->state_
end

define dump_ksync_intf_entries
   pksync_entries InterfaceKSyncObject::singleton_ kintf_entry_format
end

define dump_ksync_route_objects
    pmap RouteKSyncObject::vrf_ucrt_object_map_ uint32_t RouteKSyncObject*
end

define dump_ksync_mcast_route_objects
    pmap RouteKSyncObject::vrf_mcrt_object_map_ uint32_t RouteKSyncObject*
end

define kroute_entry_format
    set $__krt = (RouteKSyncEntry *)((size_t)$Xnode - (size_t)&(KSyncEntry::node_))
    set $__ip = $__krt->addr_.ipv4_address_.addr_.s_addr
    printf"%p  %d.%d.%d.%d/%d  vrf=%d  label=%d nh=%d state ", $__krt,\
        ($__ip & 0xff), ($__ip >> 8 & 0xff), ($__ip >> 16 & 0xff), ($__ip >> 24 & 0xff),\
        $__krt->plen_, $__krt->vrf_id_, $__krt->label_, $__krt->nh_.px->index_
    print $__krt->state_
end

define dump_ksync_route_entries
    if $argc != 1
        help dump_ksync_route_entries
    else 
        pksync_entries $arg0 kroute_entry_format
    end
end

document dump_ksync_route_entries
    Print route entries in Ksync
    Use "dump_route_ksync_objects" to get object pointer
    Syntax: dump_ksync_route_entries <route_object>
end

define kflow_entry_format
    set $__kflow = (FlowTableKSyncEntry *)((size_t)$Xnode - (size_t)&(KSyncEntry::node_))
    printf"%p  hash=0x%-8x  fp=%p \n",\
        $__kflow, $__kflow->hash_id_, $__kflow->fe_.px
end

define dump_ksync_flow_entries
   pksync_entries FlowTableKSyncObject::singleton_ kflow_entry_format
end

define kmirror_entry_format
    set $__mirror = (MirrorKSyncEntry *)((size_t)($Xnode) - (size_t)&(KSyncEntry::node_))
    set $__sip = $__mirror->sip_.addr_.s_addr
    set $__dip = $__mirror->dip_.addr_.s_addr
    printf "%p   %d.%d.%d.%d:%d   %d.%d.%d.%d:%d nh=%p\n", $__mirror,\
            ($__sip & 0xff), ($__sip >> 8 & 0xff), ($__sip >> 16 & 0xff),\
            ($__sip >> 24 & 0xff), $__mirror->sport_,\
            ($__dip & 0xff), ($__dip >> 8 & 0xff), ($__dip >> 16 & 0xff),\
            ($__dip >> 24 & 0xff), $__mirror->dport_, $__mirror->nh_.px
end

define dump_ksync_mirror_entries
   pksync_entries MirrorKSyncObject::singleton_ kmirror_entry_format
end

define dump_fip_list
    if $argc == 0
        help dump_fip_list
    else
    set $Xlist = (VmInterface *)$arg0
    set $Xtree = &($Xlist->floating_iplist_)

    # set the node equal to first node and end marker
    set $Xnode = $Xtree._M_t._M_impl._M_header._M_left
    set $Xend = &($Xtree._M_t._M_impl._M_header)

    printf "FloatingIp       Addr             Vrf                  Vn\n"
    while $Xnode != $Xend
        set $__fip = (VmInterface::FloatingIp *)($Xnode + 1)
        set $__ip = $__fip->floating_ip_.addr_.s_addr
        printf "%p   %d.%d.%d.%d  %p   %p\n", $__fip, \
            (($__ip >> 0) & 0xff), (($__ip >> 8) & 0xff), \
            (($__ip >> 16) & 0xff), (($__ip >> 24) & 0xff), \
            $__fip->vrf_.px, $__fip->vn_.px
        gmap_next_member
    end
    end
end

document dump_fip_list
    Prints List of floating-ip for an interface
    Syntax: dump_fip_list <interface-addr>
end

define kvassign_entry_format
    set $__kva = (VrfAssignKSyncEntry *)((size_t)$Xnode - (size_t)&(KSyncEntry::node_))
    printf"%p   intf=%p    tag=%-4d    vrf=%d     state=", $__kva, $__kva.interface_.px, $__kva.vlan_tag_, $__kva.vrf_id_
    print $__kva.state_
end

define dump_ksync_vassign_entries
    pksync_entries VrfAssignKSyncObject::singleton_ kvassign_entry_format
end

define kvxlan_entry_format
    set $__kvxlan = (VxLanIdKSyncEntry *)((size_t)$Xnode - (size_t)&(KSyncEntry
    printf"%p   nh=%p  state=", $__kvxlan, $__kvxlan.nh_.px
    print $__kvxlan.state_
end

define dump_ksync_vxlan_entries
    pksync_entries VxLanKSyncObject::singleton_ kvxlan_entry_format
end


define service_instance_entry_format
    set $__svi = (ServiceInstance *)((size_t)($Xnode) - (size_t)&(ServiceInstance::node_))
    set $__prop = $__svi->properties_
    printf "%p Uuid:%-20s ServiceType:%d VirtualisationType:%d VmiInside:%-20s VmiOutside:%-20s MacIn:%s MacOut:%s IpIn:%s IpOut:%s IpLenIn:%d IpLenOut:%d IfCount:%d LbPool:%-20s",
       $__svi, $__prop.instance_id->data, $__prop.service_type, $__prop.virtualization_type, $__prop.vmi_inside->data, $__prop.vmi_outside->data, $__prop.mac_addr_inside, $__prop.mac_addr_outside,
       $__prop.ip_addr_inside, $__prop.ip_addr_outside, $__prop.ip_prefix_len_inside, $__prop.ip_prefix_len_outside, $__prop.interface_count, $__prop.pool_id->data
end

define dump_service_instance_entries
   if $argc != 1
       help dump_service_instance_entries
   else
       pdb_table_entries $arg0 service_instance_entry_format
   end
end

document dump_service_instance_entries
     Prints all service instance entries
     Syntax: dump_service_instance_entries <table>: Prints all service instance entries
end

