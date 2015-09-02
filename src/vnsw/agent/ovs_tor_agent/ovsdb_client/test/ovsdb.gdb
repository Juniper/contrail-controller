#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

define ovsdb_physical_switch_entry_format
    set $__entry = (OVSDB::PhysicalSwitchEntry *) ((size_t)($Xnode) -\
                              (size_t)&(((OVSDB::PhysicalSwitchEntry*)0)->node_))
    printf "%p    state=%-4d    name= %s\n",\
            $__entry, $__entry->state_, $__entry->name_._M_dataplus._M_p
end

define dump_ovsdb_physical_switch_entries
    if $argc != 1
        help dump_ovsdb_physical_switch_entries
    else
        set $__ovs_session = (OVSDB::OvsdbClientSession *) $arg0
        pksync_entries $__ovs_session->client_idl_.px->physical_switch_table_._M_ptr\
                       ovsdb_physical_switch_entry_format
    end
end

document dump_ovsdb_physical_switch_entries
    Prints entries of physical switch table in given ovsdb session
    Syntax: dump_ovsdb_physical_switch_entries <OvsdbClientSession>
end

define ovsdb_physical_port_entry_format
    set $__entry = (OVSDB::PhysicalPortEntry *) ((size_t)($Xnode) -\
                              (size_t)&(((OVSDB::PhysicalPortEntry*)0)->node_))
    printf "%p    state=%-4d    name= %s\n",\
            $__entry, $__entry->state_, $__entry->name_._M_dataplus._M_p
    set $__bindings = $__entry->binding_table_
    set $__b_i = 0
    set $__b_node = $__bindings._M_t._M_impl._M_header._M_left
    set $__b_tree_size = $__bindings._M_t._M_impl._M_node_count
    while $__b_i < $__b_tree_size
        set $value = (void *)($__b_node + 1)
        printf "\t\t%4u:  vlan_tag = %d", $__b_i, *(uint32_t *)$value
        set $value = $value + sizeof(uint32_t)
        set $__ls_entry = *((OVSDB::LogicalSwitchEntry **)$value)
        printf "    logical_switch= %s\n", $__ls_entry->name_._M_dataplus._M_p
        if $__b_node._M_right != 0
            set $__b_node = $__b_node._M_right
            while $__b_node._M_left != 0
                set $__b_node = $__b_node._M_left
            end
        else
            set $tmp_node = $__b_node._M_parent
            while $__b_node == $tmp_node._M_right
                set $__b_node = $tmp_node
                set $tmp_node = $tmp_node._M_parent
            end
            if $__b_node._M_right != $tmp_node
                set $__b_node = $tmp_node
            end
        end
        set $__b_i++
    end
end

define dump_ovsdb_physical_port_entries
    if $argc != 1
        help dump_ovsdb_physical_port_entries
    else
        set $__ovs_session = (OVSDB::OvsdbClientSession *) $arg0
        pksync_entries $__ovs_session->client_idl_.px->physical_port_table_._M_ptr\
                       ovsdb_physical_port_entry_format
    end
end

document dump_ovsdb_physical_port_entries
    Prints entries of physical port table in given ovsdb session
    Syntax: dump_ovsdb_physical_port_entries <OvsdbClientSession>
end

define ovsdb_logical_switch_entry_format
    set $__entry = (OVSDB::LogicalSwitchEntry *) ((size_t)($Xnode) -\
                              (size_t)&(((OVSDB::LogicalSwitchEntry*)0)->node_))
    printf "%p    state=%-4d    vxlan_id=%-4d    name= %s, physical_device= %s\n",\
            $__entry, $__entry->state_, $__entry->vxlan_id_,\
            $__entry->name_._M_dataplus._M_p,\
            $__entry->device_name_._M_dataplus._M_p
end

define dump_ovsdb_logical_switch_entries
    if $argc != 1
        help dump_ovsdb_logical_switch_entries
    else
        set $__ovs_session = (OVSDB::OvsdbClientSession *) $arg0
        pksync_entries $__ovs_session->client_idl_.px->logical_switch_table_._M_ptr\
                       ovsdb_logical_switch_entry_format
    end
end

document dump_ovsdb_logical_switch_entries
    Prints entries of logical switch table in given ovsdb session
    Syntax: dump_ovsdb_logical_switch_entries <OvsdbClientSession>
end

define ovsdb_vlan_port_entry_format
    set $__entry = (OVSDB::VlanPortBindingEntry *) ((size_t)($Xnode) -\
                              (size_t)&(((OVSDB::VlanPortBindingEntry*)0)->node_))
    printf "%p    state=%-4d    vlan_tag=%-4d    physical_intf= %s  physical_device= %s\n",\
            $__entry, $__entry->state_, $__entry->vlan_,\
            $__entry->physical_port_name_._M_dataplus._M_p,\
            $__entry->physical_device_name_._M_dataplus._M_p
end

define dump_ovsdb_vlan_port_entries
    if $argc != 1
        help dump_ovsdb_vlan_port_entries
    else
        set $__ovs_session = (OVSDB::OvsdbClientSession *) $arg0
        pksync_entries $__ovs_session->client_idl_.px->vlan_port_table_._M_ptr\
                       ovsdb_vlan_port_entry_format
    end
end

document dump_ovsdb_vlan_port_entries
    Prints entries of vlan port bindings table in given ovsdb session
    Syntax: dump_ovsdb_vlan_port_entries <OvsdbClientSession>
end

define ovsdb_uc_local_entry_format
    set $__entry = (OVSDB::UnicastMacLocalEntry *) ((size_t)($Xnode) -\
                              (size_t)&(((OVSDB::UnicastMacLocalEntry*)0)->node_))
    printf "%p    state=%-4d    mac=%s  logical_switch= %s  dest_ip= %s\n",\
            $__entry, $__entry->state_, $__entry->mac_._M_dataplus._M_p,\
            $__entry->logical_switch_name_._M_dataplus._M_p,\
            $__entry->dest_ip_._M_dataplus._M_p
end

define dump_ovsdb_uc_local_entries
    if $argc != 1
        help dump_ovsdb_uc_local_entries
    else
        set $__ovs_session = (OVSDB::OvsdbClientSession *) $arg0
        pksync_entries $__ovs_session->client_idl_.px->unicast_mac_local_ovsdb_._M_ptr\
                       ovsdb_uc_local_entry_format
    end
end

document dump_ovsdb_uc_local_entries
    Prints entries of unicast mac local table in given ovsdb session
    Syntax: dump_ovsdb_uc_local_entries <OvsdbClientSession>
end

