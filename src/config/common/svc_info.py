#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

_MGMT_STR = "management"
_LEFT_STR = "left"
_RIGHT_STR = "right"

_SVC_VN_MGMT = "svc-vn-mgmt"
_SVC_VN_LEFT = "svc-vn-left"
_SVC_VN_RIGHT = "svc-vn-right"
_VN_MGMT_SUBNET_CIDR = '10.250.1.0/24'
_VN_LEFT_SUBNET_CIDR = '10.250.2.0/24'
_VN_RIGHT_SUBNET_CIDR = '10.250.3.0/24'

_VN_SNAT_PREFIX_NAME = 'snat-si-left'
_VN_SNAT_SUBNET_CIDR = '100.64.0.0/29'

_CHECK_SVC_VM_HEALTH_INTERVAL = 30

_VM_INSTANCE_TYPE = 'virtual-machine'
_NETNS_INSTANCE_TYPE = 'network-namespace'

_SNAT_SVC_TYPE = 'source-nat'
_LB_SVC_TYPE = 'loadbalancer'

_ACTIVE_LOCAL_PREFERENCE = 200
_STANDBY_LOCAL_PREFERENCE = 100

def get_management_if_str():
    return _MGMT_STR

def get_left_if_str():
    return _LEFT_STR

def get_right_if_str():
    return _RIGHT_STR

def get_if_str_list():
    if_str_list = []
    if_str_list.append(get_management_if_str())
    if_str_list.append(get_left_if_str())
    if_str_list.append(get_right_if_str())
    return if_str_list

def get_management_vn_name():
    return _SVC_VN_MGMT

def get_left_vn_name():
    return _SVC_VN_LEFT

def get_right_vn_name():
    return _SVC_VN_RIGHT

def get_shared_vn_list():
    shared_vn_list = []
    shared_vn_list.append(get_management_vn_name())
    shared_vn_list.append(get_left_vn_name())
    shared_vn_list.append(get_right_vn_name())
    return shared_vn_list

def get_management_vn_subnet():
    return _VN_MGMT_SUBNET_CIDR

def get_left_vn_subnet():
    return _VN_LEFT_SUBNET_CIDR

def get_right_vn_subnet():
    return _VN_RIGHT_SUBNET_CIDR

def get_snat_left_network_prefix_name():
    return _VN_SNAT_PREFIX_NAME

def get_snat_left_subnet():
    return _VN_SNAT_SUBNET_CIDR

def get_vm_instance_type():
    return _VM_INSTANCE_TYPE

def get_netns_instance_type():
    return _NETNS_INSTANCE_TYPE

def get_snat_service_type():
    return _SNAT_SVC_TYPE

def get_lb_service_type():
    return _LB_SVC_TYPE

def get_vm_health_interval():
    return _CHECK_SVC_VM_HEALTH_INTERVAL

def get_active_preference():
    return _ACTIVE_LOCAL_PREFERENCE

def get_standby_preference():
    return _STANDBY_LOCAL_PREFERENCE

def get_service_instance_interface(vnc_lib, si_obj, type):
    st_refs = si_obj.get_service_template_refs()
    if not st_refs:  
        return None

    st_obj = vnc_lib.service_template_read(id=st_refs[0]['uuid'])
    if not st_obj:  
        return None

    st_props = st_obj.get_service_template_properties()
    if not st_props:
        return None

    st_if_list = st_props.get_interface_type()
    for idx in range(0, len(st_if_list)):
        st_if = st_if_list[idx]
        itf_type = st_if.service_interface_type
        if itf_type == type:
            break
    else:
        return None

    si_props = si_obj.get_service_instance_properties()
    if not si_props:
        return None

    si_if_list = si_props.get_interface_list()
    try:
        si_if = si_if_list[idx]
    except IndexError:
        return None 

    return si_if
