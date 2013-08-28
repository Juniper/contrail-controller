#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

define pvlan_entry_format
    set $__XVlan = (Vlan *) ((size_t)$Xnode - (size_t)&(Vlan::node_))
    printf "  %p      0x%08X    vlan_tag = %4d\n", $__XVlan, $__XVlan->flags, $__XVlan->vlan_tag
end

define vpn_entry_format
    set $Xdbentry = (DBEntry *) ((size_t)$Xnode - (size_t)&(DBEntry::node_))
    set $__Vpn = (InetVpnRoute *) $Xdbentry
    printf "%p : %08x/%d\n", $__Vpn, $__Vpn.prefix_.addr_.addr_.s_addr, $__Vpn.prefix_.prefixlen_
end


define inet_entry_format
    set $Xdbentry = (DBEntry *) ((size_t)$Xnode - (size_t)&(DBEntry::node_))
    set $__Inet = (InetRoute *) $Xdbentry
    printf "%p : %08x/%d\n", $__Inet, $__Inet.prefix_.ip4_addr_.addr_.s_addr, $__Inet.prefix_.prefixlen_
end


define gmap_next_member
    if $Xnode._M_right != 0
        set $Xnode = $Xnode._M_right
        while $Xnode._M_left != 0
            set $Xnode = $Xnode._M_left
        end
    else
        set $Xtmp_node = $Xnode._M_parent
        while $Xnode == $Xtmp_node._M_right
            set $Xnode = $Xtmp_node
            set $Xtmp_node = $Xtmp_node._M_parent
        end
        if $Xnode._M_right != $Xtmp_node
            set $Xnode = $Xtmp_node
        end
    end
end

define get_table_count
    if $argc != 1
        help get_table_count
    else
        set $Xtable = (DBTable *)$arg0
        set $Xstart = $Xtable->partitions_._M_impl._M_start
        set $Xsize = $Xtable->partitions_._M_impl._M_finish - $Xtable->partitions_._M_impl._M_start
        set $Xcount = 0
        set $Xi = 0
        while $Xi  < $Xsize
            set $Xpart = *(DBTablePartition **)($Xstart + $Xi)
            set $Xcount = $Xcount + $Xpart->tree_.tree_.data_.node_plus_pred_.header_plus_size_.size_
            set $Xi++
        end
        printf "    %d    ",  $Xcount
    end
end

define pdb_tables
    if $argc != 1
        help pdb_tables
    else
        set $Xdb = (DB *)$arg0
        set $Xtree = &($Xdb->tables_)

        # set the node equal to first node and end marker
        set $Xnode = $Xtree._M_t._M_impl._M_header._M_left
        set $Xend = &($Xtree._M_t._M_impl._M_header)

        printf "DB %p contains following tables \n", $arg0
        printf "-----------------------------------------------------\n"
        printf "DB Table ptr     Count      DB Table Name\n"
        printf "-----------------------------------------------------\n"
        while $Xnode != $Xend
            set $Xvalue1 = (void *) ($Xnode + 1)
            set $Xvalue2 = $Xvalue1 + sizeof(char *)
            set $Xtable = (DBTable *) (*((void **)$Xvalue2))
            printf "%p    ", $Xtable
            get_table_count $Xtable
            printf "   %s", *((char **)$Xvalue1)
            printf "\n"
            gmap_next_member
        end
    end
end

document pdb_tables
        Prints information for tables present in a given db.
        Syntax: pdb_tables <db>: Prints all the tables present in given db
end

define pdb_table_partitions
    if $argc != 1
        help pdb_table_partitions
    else
        set $Xtable = (DBTable *)$arg0
        set $Xstart = $Xtable->partitions_._M_impl._M_start
        set $Xsize = $Xtable->partitions_._M_impl._M_finish - $Xtable->partitions_._M_impl._M_start
        set $Xi = 0
        printf "DB Table %p(%s) have following partitions \n", $Xtable, $Xtable->name_._M_dataplus._M_p
        printf "-------------------------------------------------\n"
        printf "  index       Partition ptr        no. of entries\n"
        printf "-------------------------------------------------\n"
        while $Xi  < $Xsize
            set $Xpart = *(DBTablePartition **)($Xstart + $Xi)
            printf "   %3d        %p          %8d\n", $Xi, $Xpart, $Xpart->tree_.tree_.data_.node_plus_pred_.header_plus_size_.size_
            #$Xpart->index_, $Xpart
            set $Xi++
        end
    end
end

document pdb_table_partitions
        Prints information of partitions of a DB table.
        Syntax: pdb_table_partitions <db_table_ptr>: Prints partitions of a DB table.
end


define grbtree_next_node
    if $Xnode.right_ != 0
        set $Xnode = $Xnode.right_
        while $Xnode.left_ != 0
            set $Xnode = $Xnode.left_
        end
    else
        set $Xtmp_node = $Xnode.parent_
        while $Xnode == $Xtmp_node.right_
            set $Xnode = $Xtmp_node
            set $Xtmp_node = $Xtmp_node.parent_
        end
        if $Xnode.right_ != $Xtmp_node
            set $Xnode = $Xtmp_node
        end
    end
end

define pdb_table_pentries
    if $argc != 1 && $argc != 2
        help pdb_table_pentries
    else
        # set the node equal to first node and end marker
        set $Xtree = &((DBTablePartition *)$arg0)->tree_.tree_
        set $Xnode = $Xtree->data_.node_plus_pred_.header_plus_size_.header_.left_
        set $Xend = &($Xtree->data_.node_plus_pred_.header_plus_size_.header_)

        printf "  Entries in DB Table Partion %p      \n", $arg0
        printf "--------------------------------------------\n"
        printf "   Entry ptr      Entry flags       \n"
        printf "--------------------------------------------\n"
        while $Xnode != $Xend
            if $argc == 2
                # example pvlan_entry_format 
                $arg1
            else
                set $Xdbentry = (DBEntry *) ((size_t)$Xnode - (size_t)&(DBEntry::node_))
                printf "  %p      0x%08X\n", $Xdbentry, $Xdbentry->flags
            end
            grbtree_next_node
        end
    end
end

document pdb_table_pentries
    Prints all entries of given DB table Partition.
    Syntax: pdb_table_pentries <db_table_partition_ptr> <print-fn-additional-info>
    Note: Prints entries of a DB table partition.
    Examples:
    pdb_table_pentries part_ptr - Prints all entries in partion in default format
    pdb_table_pentries part_ptr print_fn - Prints all entries in partion in default format and additional information specified by print function
end

define pdb_table_entries
    if $argc != 1 && $argc != 2
        help pdb_table_entries
    else
        set $Xtable = (DBTable *)$arg0
        set $Xstart = $Xtable->partitions_._M_impl._M_start
        set $Xsize = $Xtable->partitions_._M_impl._M_finish - $Xtable->partitions_._M_impl._M_start
        set $Xi = 0
        if $argc != 2
            printf "  Entries in DB Table %p      \n", $arg0
            printf "--------------------------------------------\n"
            printf "   Entry ptr      Entry flags       \n"
        end
        printf "--------------------------------------------\n"

        while $Xi  < $Xsize
            set $Xpart = *(DBTablePartition **)($Xstart + $Xi)
            # set the node equal to first node and end marker
            set $Xtree = &$Xpart->tree_.tree_
            set $Xnode = $Xtree->data_.node_plus_pred_.header_plus_size_.header_.left_
            set $Xend = &($Xtree->data_.node_plus_pred_.header_plus_size_.header_)
            while $Xnode != $Xend
                if $argc == 2
                    # example pvlan_entry_format 
                    $arg1
                else
                    set $Xdbentry = (DBEntry *) ((size_t)$Xnode - (size_t)&(DBEntry::node_))
                    printf "  %p      0x%08X\n", $Xdbentry, $Xdbentry->flags
                end
                grbtree_next_node
            end
            set $Xi++
        end
    end
end

document pdb_table_entries
    Prints all entries of given DB table
    Syntax: pdb_table_entries <db_table_ptr> <print-fn-additional-info>
    Note: Prints entries of a DB table
    Examples:
    pdb_table_entries tbl_ptr - Prints all entries in table in default format
    pdb_table_entries tbl_ptr print_fn - Prints all entries in table in default format and additional information specified by print function
end

define pdb_entry_states
    if $argc != 1
        help pdb_entry_states
    else
        set $Xentry = (DBEntry *)$arg0
        set $Xtree = &($Xentry->state_)

        # set the node equal to first node and end marker
        set $Xnode = $Xtree._M_t._M_impl._M_header._M_left
        set $Xend = &($Xtree._M_t._M_impl._M_header)

        printf "  DBEntry %p has following states \n", $arg0
        printf "-----------------------------------------------------\n"
        printf "    ListenerId          DBState ptr \n"
        printf "-----------------------------------------------------\n"
        while $Xnode != $Xend
            set $Xvalue1 = (void *) ($Xnode + 1)
            set $Xvalue2 = $Xvalue1 + sizeof(void *)
            printf "      %4d              %p\n", *((int **)$Xvalue1), *((void **)$Xvalue2)
            gmap_next_member
        end
    end
end

document pdb_entry_states
    Prints all the listener states for given entry
    Syntax: pdb_entry_states <db_entry> Prints all the listener states
end

define pdb_table_listeners
    if $argc != 1
        help pdb_table_listeners
    else
        set $Xtable = (DBTable *)$arg0
        set $Xlisteners = $Xtable->info_._M_ptr
        set $Xcallbacks = &($Xlisteners->callbacks_)
        set $Xstart = $Xcallbacks->_M_impl._M_start 
        set $Xsize = $Xcallbacks->_M_impl._M_finish - $Xcallbacks->_M_impl._M_start
        set $Xi = 0
        printf " DBtable %p(%s) has following clients \n", $Xtable, $Xtable->name_._M_dataplus._M_p 
        printf "-----------------------------------------------------\n"
        printf "  ListenerId          Callback         \n"
        printf "-----------------------------------------------------\n"
        while $Xi < $Xsize
            set $Xcentry = ('boost::function<void (DBTablePartBase *, DBEntryBase *)>' *)($Xstart + $Xi)
            if $Xcentry->functor.func_ptr != 0
                printf "  %4d          ", $Xi
                print $Xcentry->functor.func_ptr
            end
            set $Xi++
        end
    end
end

document pdb_table_listeners
    Prints all the listeners for given table
    Syntax: pdb_table_listeners <db_table> Prints all the table listeners
end

