define pat_getnext_node
    set $Xloop = 1
    set $__Xint = $Xnode
    while $Xnode && $Xloop
        if $__Xint->bitpos_ > $Xnode->bitpos_
            set $__Xint = $Xnode
            set $Xnode = $__Xint->right_
        else
            set $__Xint = $Xnode
            if $__Xint->left_
                set $Xnode = $__Xint->left_
            else
                set $Xnode = $__Xint->right_
            end
        end
        if $Xnode && $Xnode->bitpos_ > $__Xint->bitpos_ && $Xnode->intnode_ != 1
            set $Xloop = 0
        end
    end
end

define pat_getfirst_node
    set $Xnode = $Xtree->root_
    if $Xnode && $Xnode->intnode_ == 1
        pat_getnext_node
    end
end

define print_pat_tree
    set $Xtree = ('Patricia::TreeBase' *)$arg0
    pat_getfirst_node

    printf "Patricia Tree %p contains %d entries and %d internal nodes\n", $Xtree, $Xtree ? $Xtree->nodes_ : 0, $Xtree ? $Xtree->int_nodes_ : 0
    if $argc == 2
        printf "-----------------------------------\n"
        printf " length     value                  \n"
        printf "-----------------------------------\n"
    end

    while $Xnode
        if $argc == 2
            $arg1 $Xnode
        else
            printf "%p\n" , $Xnode
        end
        pat_getnext_node
    end
end

define print_ipv4_format
    set $__XRoute = (Route *) ((size_t)$Xnode - (size_t)&(Route::node_))
    set $__Xip = (char *) &($__XRoute->ip_)
    printf " %d.%d.%d.%d/%d \n", $__Xip[3], $__Xip[2], $__Xip[1], $__Xip[0], $__XRoute->len_
end

define print_pat_tree_ipv4_format
    print_pat_tree $arg0 print_ipv4_format
end

define print_flow_mgmt_ipv4_format
    set $__XRoute = (InetRouteFlowMgmtKey *) ((size_t)$Xnode - (size_t)&(InetRouteFlowMgmtKey::node_))
    printf "%p %d %d\n", $__XRoute, $__XRoute->vrf_id_, $__XRoute->plen_
end

define print_pat_flow_mgmt_tree_ipv4_format
    print_pat_tree $arg0 print_flow_mgmt_ipv4_format
end
