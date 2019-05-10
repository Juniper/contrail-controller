define dump_component_nh
    set $__composite_nh = ((CompositeNH *) $arg0)
    set $size = $__composite_nh->component_nh_list_._M_impl._M_finish - $__composite_nh->component_nh_list_._M_impl._M_start
    set $i = 0
    while $i < $size
        if (($__composite_nh->component_nh_list_._M_impl._M_start + $i).px == 0)
              printf "%p Label = %d", $__composite_nh->component_nh_list_._M_impl._M_start + $i).px, $i
        else
            set $__nh = ((NextHop*)($__composite_nh->component_nh_list_._M_impl._M_start + $i).px->nh_.px)
            set $__cnh = ((ComponentNH*)($__composite_nh->component_nh_list_._M_impl._M_start + $i).px)
            printf "%p  Label = %d   nh_ = %p nh_type = %d  nh_id = %d", $__cnh, $__cnh->label_, $__nh, $__nh->type_, $__nh->id_
        end
        set $i++
        printf "\n"
    end
end
