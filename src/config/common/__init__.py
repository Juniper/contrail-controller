#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

IP_FABRIC_VN_FQ_NAME = ['default-domain', 'default-project', 'ip-fabric']
IP_FABRIC_RI_FQ_NAME = IP_FABRIC_VN_FQ_NAME + ['__default__']
LINK_LOCAL_VN_FQ_NAME = ['default-domain', 'default-project', '__link_local__']
LINK_LOCAL_RI_FQ_NAME = LINK_LOCAL_VN_FQ_NAME + ['__link_local__']

BGP_RTGT_MIN_ID = 8000000
def obj_to_json(obj):
    return dict((k, v) for k, v in obj.__dict__.iteritems())
#end obj_to_json

def json_to_obj(obj):
    pass
#end json_to_obj

def ignore_exceptions(func):
    def wrapper(*args, **kwargs):
        try:
            return func(*args, **kwargs)
        except Exception as e:
            return None
    return wrapper
# end ignore_exceptions
