#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

define nh_entry_format
    set $__nh = (NextHop *) ((size_t)($Xnode) - (size_t)&(((NextHop*)0)->node_))
    printf "%p    nh_id=%-4d   type=%-4d    flags=%-4d    ref=%-4d    valid=%-4d    policy=%-4d\n", $__nh, $__nh->id_, $__nh->type_,\
           $__nh->flags, $__nh->refcount_->my_storage->my_value, $__nh->valid_, $__nh->policy_
end

define dump_nh_entries
   pdb_table_entries Agent::singleton_.nh_table_ nh_entry_format
end

define intf_entry_format
   set $__intf = (Interface *)((size_t)($Xnode) - (size_t)&(((Interface*)0)->node_))
   printf "%p    %-4d    flags=%-4d   ref=%-4d\n", $__intf, $__intf->id_,\
           $__intf->flags, $__intf->refcount_->my_storage->my_value
end

define dump_intf_entries
   pdb_table_entries Agent::singleton_.intf_table_ intf_entry_format
end

define li_debug_format
   set $__intf = (VmInterface *)((size_t)($Xnode) - (size_t)&(((VmInterface*)0)->node_))
   if $__intf.type_ == 3
       set $__li = $__intf
       printf "LI %p %s %p\n", $__li, $__li->name_._M_dataplus._M_p, $__li->vm_interface_.px
   end
   if $__intf.type_ == 4
       printf "Got VMI %p\n", $__intf
       print $__intf->logical_interface_
   end
end

define li_debug
   set $__li = 0
   set $__vmi = 0
   pdb_table_entries Agent::singleton_.intf_table_ li_debug_format
end

define mpls_entry_format
    set $__mpls = (MplsLabel *)((size_t)($Xnode) - (size_t)&(((MplsLabel*)0)->node_))
    printf "%p    label=%-4x   nh=%p\n", $__mpls, $__mpls->label_, $__mpls->nh_
end

define dump_mpls_entries
   pdb_table_entries Agent::singleton_.mpls_table_ mpls_entry_format
end

define uc_route_entry_format
    set $__rt = (InetUnicastRouteEntry*)((size_t)($Xnode) - (size_t)&(Route::node_))
    set $__ip = $__rt->addr_.ipv4_address_.addr_.s_addr
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
    set $__rt = (BridgeRouteEntry*)((size_t)($Xnode) - (size_t)&(Route::node_))
    set $__mac = $__rt->mac_
    printf "%p  %02x:%02x:%02x:%02x:%02x:%02x\t\t flags=%d\n", $__rt,\
                 ($__mac.addr_.ether_addr_octet[0]), ($__mac.addr_.ether_addr_octet[1]),\
                 ($__mac.addr_.ether_addr_octet[2]), ($__mac.addr_.ether_addr_octet[3]),\
                 ($__mac.addr_.ether_addr_octet[4]), ($__mac.addr_.ether_addr_octet[5]),\
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
           printf "Path : %p  Peer : %p  NH : %p Label : %d\n", $__path, $__path->peer_.px, $__path->nh_.px, $__path->label_
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
    printf "%p    %-20s    idx=%-4d    ref_count=%-4d   flags=%-4d rt_db=%p mcrt_db=%p evpn_db=%p bridge_db=%p v6_rt_db=%p mpls_rt_db=%p\n", $__vrf,\
           $__vrf->name_._M_dataplus._M_p, $__vrf->id_, $__vrf->refcount_->my_storage->my_value,\
           $__vrf->flags, $__vrf->rt_table_db_[Agent::INET4_UNICAST], $__vrf->rt_table_db_[Agent::INET4_MULTICAST], \
           $__vrf->rt_table_db_[Agent::EVPN], $__vrf->rt_table_db_[Agent::BRIDGE], $__vrf->rt_table_db_[Agent::INET6_UNICAST],\
           $__vrf->rt_table_db_[Agent::INET4_MPLS]
end

define dump_vrf_entries
    pdb_table_entries Agent::singleton_.vrf_table_ vrf_entry_format
end

define vn_entry_format
    set $__vn = (VnEntry *)((size_t)($Xnode) - (size_t)&(((VnEntry*)0)->node_))
    set $__data = $__vn->uuid_->data
    printf "%p    %-20s %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n", $__vn, $__vn->name_._M_dataplus._M_p, $__data[0], $__data[1], $__data[2], $__data[3], $__data[4], $__data[5], $__data[6], $__data[7], $__data[8], $__data[9], $__data[10], $__data[11], $__data[12], $__data[13], $__data[14], $__data[15]
end

define dump_vn_entries
   pdb_table_entries Agent::singleton_.vn_table_ vn_entry_format
end

define vm_entry_format
    set $__vm = (VmEntry *)((size_t)($Xnode) - (size_t)&((VmEntry*)0)->node_)
    set $__data = $__vm->uuid_->data
    printf "%p    %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n", $__vm, $__data[0], $__data[1], $__data[2], $__data[3], $__data[4], $__data[5], $__data[6], $__data[7], $__data[8], $__data[9], $__data[10], $__data[11], $__data[12], $__data[13], $__data[14], $__data[15]
end

define dump_vm_entries
   pdb_table_entries Agent::singleton_.vm_table_ vm_entry_format
end

define vxlan_entry_format
    set $__vxlan = (VxLanId *)((size_t)($Xnode) - (size_t)&((VxLanId*)0)->node_)
    printf "%p    label=%-4x   nh=%p\n", $__vxlan, $__vxlan->vxlan_id_, $__vxlan->nh_.px
end

define dump_vxlan_entries
   pdb_table_entries Agent::singleton_.vxlan_table_ vxlan_entry_format
end

define mirror_entry_format
    set $__mirror = (MirrorEntry *)((size_t)($Xnode) - (size_t)&(((MirrorEntry*)0)->node_))
    set $__sip = $__mirror->sip_.ipv4_address_.addr_.s_addr
    set $__dip = $__mirror->dip_.ipv4_address_.addr_.s_addr
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
        $__va, $__va->flags, $__va->refcount_->my_storage->my_value, $__va->type, $__va->vlan_tag_
end

define dump_vrf_assign_entries
   pdb_table_entries Agent::singleton_.vrf_assign_table_ vrf_assign_entry_format
end

define acl_entry_format
    set $__acl = (AclDBEntry *)((size_t)($Xnode) - (size_t)&(((AclDBEntry*)0)->node_))
    printf "%p     %s     ref=%d\n", $__acl, $__acl->name_._M_dataplus._M_p, \
                                     $__acl->refcount_->my_storage->my_value
end

define dump_acl_entries
   pdb_table_entries Agent::singleton_.acl_table_ acl_entry_format
end

define sg_entry_format
    set $__sg = (SgEntry *)((size_t)($Xnode) - (size_t)&(((SgEntry*)0)->node_))
    printf "%p     %d     ingress_acl=%p   egress_acl=%p\n", $__sg, $__sg->sg_id_, $__sg->ingress_acl_.px, $__sg->egress_acl_.px
end

define dump_sg_entries
   printf "Entry             Id      Acl\n"
   pdb_table_entries Agent::singleton_.sg_table_ sg_entry_format
end

define ifnode_entry_format
     set $__ifnode = (IFMapNode *) ((size_t)$Xnode - (size_t)&(((IFMapNode*)0)->node_))
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
    set $__iflink = (IFMapLink *) ((size_t)$Xnode - (size_t)&(((IFMapLink*)0)->node_))

    set $left = $__iflink->left_node_
    if $left
    	printf "Left %p  name=%-40s - ", $left, $left->name_._M_dataplus._M_p
    end

    set $right = $__iflink->right_node_
    if $right
    	printf "Right %p  name=%-40s\n", $right, $right->name_._M_dataplus._M_p
    end
end

define dump_ifmap_link_entries
    pdb_table_entries $arg0 iflink_entry_format
end

document dump_ifmap_entries
     Prints name of all IFMAP nodes in given table
     Syntax: pdb_ifmap_entries <table>: Prints all entries in IFMAP table
end

define dump_flow_tree
    set $__flow_table = Agent::singleton_->pkt_->flow_proto_.px->flow_table_list_[0]
    print $__flow_table->flow_entry_map_
end

document dump_flow_tree
     Prints flow tree
     Syntax: dump_flow_tree
end

define dump_proto_list
    print /x Agent::singleton_->pkt_->pkt_handler_.px->proto_list_
end

document dump_proto_list
     Prints list of Proto entries
     Syntax: dump_proto_list
end

define dump_ksync_sock
    print KSyncSock::sock_
end

document dump_ksync_sock
     Prints list of ksync socket structures
     Syntax: dump_ksync_sock
end

define pwait_tree
    if $argc != 1
        help pwait_tree
    else
        # set the node equal to first node and end marker
        set $Xtree = &((KSyncSock *)$arg0)->wait_tree_
        set $Xnode = $Xtree._M_t._M_impl._M_header._M_left
        set $Xend = &($Xtree._M_t._M_impl._M_header)

        set $Xtotal_len = (int)0

        while $Xnode != $Xend
            set $Kentry = (IoContext *) ((size_t)$Xnode - (size_t)&(((IoContext*)0)->node_))
            printf "IoContext = (KSyncIoContext *) %p      msg_len = %d\n", $Kentry, $Kentry->msg_len_
	    set $Xtotal_len = $Xtotal_len + $Kentry->msg_len_
	    gmap_next_member
        end

        printf "Total Length of pending messages %d bytes \n", $Xtotal_len
    end
end

document pwait_tree
     Prints entries in Ksync object set
     Syntax: pwait_tree <KSyncSock *>
end

define knh_entry_format
    set $__knh = (NHKSyncEntry *)((size_t)$Xnode - (size_t)&(KSyncEntry::node_))
    printf"%p   idx=%-5d    type=%-4d    state=", $__knh, $__knh->index_, $__knh->type_
    print $__knh->state_
end

define pksync_entries
    if $argc != 1 && $argc != 2
        help pksync_entries
    else
        # set the node equal to first node and end marker
        set $Xtree = &((KSyncDBObject *)$arg0)->tree_.tree_
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

define dump_ksync_nh_entries
    pksync_entries Agent::singleton_->ksync_->nh_ksync_obj_.px knh_entry_format
end

define kmpls_entry_format
    set $__kmpls = (MplsKSyncEntry *)((size_t)$Xnode - (size_t)&(KSyncEntry::node_))
    printf"%p  idx=%-5d  label=%-5d  nh=%-5d   ", $__kmpls, $__kmpls->index_,\
                                                      $__kmpls->label_,$__kmpls->nh_->index_
    print $__knh->state_
end

define dump_ksync_mpls_entries
    pksync_entries Agent::singleton_->ksync_->mpls_ksync_obj_.px kmpls_entry_format
end

define kintf_entry_format
    set $__kintf = (InterfaceKSyncEntry *)((size_t)$Xnode - (size_t)&(KSyncEntry::node_))
    printf"%p    idx=%-5d   name=%-20s   ", $__kintf, $__kintf->index_,\
                                                  $__kintf->interface_name_._M_dataplus._M_p
    print $__kintf->state_
end

define dump_ksync_intf_entries
   pksync_entries Agent::singleton_->ksync_->interface_ksync_obj_.px kintf_entry_format
end

define kroute_entry_format
    set $__krt = (RouteKSyncEntry *)((size_t)$Xnode - (size_t)&(KSyncEntry::node_))
    set $__ip = $__krt->addr_.ipv4_address_.addr_.s_addr
    printf"%p  %d.%d.%d.%d/%d  vrf=%d  label=%d nh=%d state ", $__krt,\
        ($__ip & 0xff), ($__ip >> 8 & 0xff), ($__ip >> 16 & 0xff), ($__ip >> 24 & 0xff),\
        $__krt->prefix_len_, $__krt->vrf_id_, $__krt->label_, $__krt->nh_.px->index_
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
        $__kflow, $__kflow->hash_id_, $__kflow->flow_entry_.px
end

define dump_ksync_flow_entries
   pksync_entries $arg0 kflow_entry_format
end

define kmirror_entry_format
    set $__mirror = (MirrorKSyncEntry *)((size_t)($Xnode) - (size_t)&(KSyncEntry::node_))
    set $__sip = $__mirror->sip_.ipv4_address_.addr_.s_addr
    set $__dip = $__mirror->dip_.ipv4_address_.addr_.s_addr
    printf "%p   %d.%d.%d.%d:%d   %d.%d.%d.%d:%d nh=%p\n", $__mirror,\
            ($__sip & 0xff), ($__sip >> 8 & 0xff), ($__sip >> 16 & 0xff),\
            ($__sip >> 24 & 0xff), $__mirror->sport_,\
            ($__dip & 0xff), ($__dip >> 8 & 0xff), ($__dip >> 16 & 0xff),\
            ($__dip >> 24 & 0xff), $__mirror->dport_, $__mirror->nh_.px
end

define dump_ksync_mirror_entries
   pksync_entries Agent::singleton_->ksync_->mirror_ksync_obj_.px kmirror_entry_format
end

define dump_fip_list
    if $argc == 0
        help dump_fip_list
    else
    	set $Xlist = (VmInterface *)$arg0
    	set $Xtree = &($Xlist->floating_ip_list_)

    	# set the node equal to first node and end marker
    	set $Xnode = $Xtree.list_._M_t._M_impl._M_header._M_left
    	set $Xend = &($Xtree.list_._M_t._M_impl._M_header)

    	printf "FloatingIp       Addr             Vrf                  Vn\n"
    	while $Xnode != $Xend
        	set $__fip = (VmInterface::FloatingIp *)($Xnode + 1)
        	set $__ip = $__fip->floating_ip_.ipv4_address_.addr_.s_addr
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
    pksync_entries Agent::singleton_->ksync_->vrf_assign_ksync_obj_.px kvassign_entry_format
end

define kvxlan_entry_format
    set $__kvxlan = (VxLanIdKSyncEntry *)((size_t)$Xnode - (size_t)&(KSyncEntry::node_))
    printf"%p   nh=%p  state=", $__kvxlan, $__kvxlan.nh_.px
    print $__kvxlan.state_
end

define dump_ksync_vxlan_entries
    pksync_entries Agent::singleton_->ksync_->vxlan_ksync_obj_.px kvxlan_entry_format
end


define service_instance_entry_format
    set $__svi = (ServiceInstance *)((size_t)($Xnode) - (size_t)&(((ServiceInstance*)0)->node_))
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

define dump_component_nh_label
     set $__nh = (NextHop *) ((size_t)($Xnode) - (size_t)&(((NextHop*)0)->node_))
     if $__nh->type_ == NextHop::COMPOSITE
         set $__cnh = (CompositeNH *)$__nh
         set $size = $__cnh->component_nh_key_list_._M_impl._M_finish - $__cnh->component_nh_key_list_._M_impl._M_start
         set $i = 0
         while $i < $size
            if (($__cnh->component_nh_key_list_._M_impl._M_start + $i).px == 0)
               printf "Label %d NULL, ", $i
            else
               printf "Label %d %u, ", $i, ($__cnh->component_nh_key_list_._M_impl._M_start + $i).px->label_
            end
            set $i++
         end
         printf "\n"
     end
end

define dump_nh_component_entries
   pdb_table_entries Agent::singleton_.nh_table_ dump_component_nh_label
end

