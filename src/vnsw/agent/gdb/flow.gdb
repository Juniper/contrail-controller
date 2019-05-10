define pflow_table
    print Agent::singleton_->pkt_->flow_proto_.px->flow_table_list_
end

define pflow_entry
    set $XFlow = *((FlowEntry **)($XValue))
#    print *(FlowKey *)$XKey
    set $index = $XFlow->ksync_index_entry_._M_ptr

    printf "\t%6d   %p  %p  %p %p\n", $XFlow->flow_handle_, $XFlow, $index, $index->ksync_entry_, $XFlow->reverse_flow_entry_.px
end

#based on pmap macro from stl.gdb
define pflow_tree
    set $table = (FlowTable *)$arg0
    set $tree = $table->flow_entry_map_
    set $i = 0
    set $node = $tree._M_t._M_impl._M_header._M_left
    set $end = $tree._M_t._M_impl._M_header
    set $tree_size = $tree._M_t._M_impl._M_node_count
    while $i < $tree_size
        set $XIndex = $i
        set $XKey = (void *)($node + 1)
        set $XValue = $XKey + sizeof(FlowKey)
        pflow_entry
        if $node._M_right != 0
            set $node = $node._M_right
            while $node._M_left != 0
                set $node = $node._M_left
            end
        else
            set $tmp_node = $node._M_parent
            while $node == $tmp_node._M_right
                set $node = $tmp_node
                set $tmp_node = $tmp_node._M_parent
            end
            if $node._M_right != $tmp_node
                set $node = $tmp_node
            end
        end
        set $i++
    end
end
