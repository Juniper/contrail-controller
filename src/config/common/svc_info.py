#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

_SVC_VN_MGMT = "svc-vn-mgmt"
_SVC_VN_LEFT = "svc-vn-left"
_SVC_VN_RIGHT = "svc-vn-right"

def get_left_vn(parent_str, vn):
    if vn is None:
        return None
    if vn == "":
        return(parent_str + ':' + _SVC_VN_LEFT)
    else:
        return vn
# end get_left_vn

def get_right_vn(parent_str, vn):
    if vn is None:
        return None
    if vn == "":
        return(parent_str + ':' + _SVC_VN_RIGHT)
    else:
        return vn
# end get_right_vn

def get_management_vn(parent_str, vn):
    if vn is None:
        return None
    if vn == "":
        return(parent_str + ':' + _SVC_VN_MGMT)
    else:
        return vn
# end get_management_vn
