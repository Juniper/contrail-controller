
import socket
from cfgm_common import jsonutils as json
import jsonpickle

import cfgm_common as common
from cfgm_common.exceptions import NoIdError, RefsExistError, BadRequest
from cfgm_common.exceptions import HttpError, RequestSizeError
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

def _access_control_list_update(acl_obj, name, obj, entries):
    if acl_obj is None:
        if entries is None:
            return None
        acl_obj = AccessControlList(name, obj, entries)
        try:
            DBBaseST._vnc_lib.access_control_list_create(acl_obj)
            return acl_obj
        except (NoIdError, BadRequest) as e:
            DBBaseST._logger.error(
                "Error while creating acl %s for %s: %s" %
                (name, obj.get_fq_name_str(), str(e)))
        except RequestSizeError as e:
            # log the error and raise an alarm
            DBBaseST._logger.error(
                "Bottle request size error while creating acl %s for %s" %
                (name, obj.get_fq_name_str()))
            err_info = {'acl rule limit exceeded': True}
            _raise_and_send_uve_to_sandesh('ACL', err_info,
                                           DBBaseST._sandesh)
        return None
    else:
        if entries is None:
            try:
                DBBaseST._vnc_lib.access_control_list_delete(id=acl_obj.uuid)
            except NoIdError:
                pass
            return None

        entries_hash = hash(entries)
        # if entries did not change, just return the object
        if acl_obj.get_access_control_list_hash() == entries_hash:
            return acl_obj

        # Set new value of entries on the ACL
        acl_obj.set_access_control_list_entries(entries)
        acl_obj.set_access_control_list_hash(entries_hash)
        try:
            DBBaseST._vnc_lib.access_control_list_update(acl_obj)
        except HttpError as he:
            DBBaseST._logger.error(
                "HTTP error while updating acl %s for %s: %d, %s" %
                (name, obj.get_fq_name_str(), he.status_code, he.content))
        except NoIdError:
            DBBaseST._logger.error("NoIdError while updating acl %s for %s" %
                                   (name, obj.get_fq_name_str()))
        except RequestSizeError as e:
            # log the error and raise an alarm
            DBBaseST._logger.error(
                "Bottle request size error while creating acl %s for %s" %
                (name, obj.get_fq_name_str()))
            err_info = {'acl rule limit exceeded': True}
            _raise_and_send_uve_to_sandesh('ACL', err_info,
                                           DBBaseST._sandesh)
    return acl_obj
# end _access_control_list_update

def _pp_json_object(obj):
    parsed = json.loads(jsonpickle.encode(obj, unpicklable=False))
    return  json.dumps(parsed, indent=4)

def _create_pprinted_prop_list(name, value):
    return sandesh.PropList(name, _pp_json_object(value))