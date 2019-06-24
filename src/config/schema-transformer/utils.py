
import socket
from cfgm_common import jsonutils as json
import jsonpickle

import cfgm_common as common
from vnc_api.vnc_api import *
from schema_transformer.sandesh.st_introspect import ttypes as sandesh
from cfgm_common.uve.config_req.ttypes import *

_PROTO_STR_TO_NUM_IPV4 = {
    'icmp6': '58',
    'icmp': '1',
    'tcp': '6',
    'udp': '17',
    'any': 'any',
}
_PROTO_STR_TO_NUM_IPV6 = {
    'icmp6': '58',
    'icmp': '58',
    'tcp': '6',
    'udp': '17',
    'any': 'any',
}

RULE_IMPLICIT_ALLOW_UUID = common.RULE_IMPLICIT_ALLOW_UUID
RULE_IMPLICIT_DENY_UUID = common.RULE_IMPLICIT_DENY_UUID

def _raise_and_send_uve_to_sandesh(obj_type, err_info, sandesh):
    config_req_err = SystemConfigReq(obj_type=obj_type,
                                  err_info=err_info)
    config_req_err.name = socket.getfqdn()
    config_req_trace = SystemConfigReqTrace(data=config_req_err,
                                         sandesh=sandesh)
    config_req_trace.send(sandesh=sandesh)

def _pp_json_object(obj):
    parsed = json.loads(jsonpickle.encode(obj, unpicklable=False))
    return  json.dumps(parsed, indent=4)

def _create_pprinted_prop_list(name, value):
    return sandesh.PropList(name, _pp_json_object(value))
