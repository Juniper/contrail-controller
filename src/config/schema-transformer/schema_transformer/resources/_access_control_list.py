#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from builtins import str

from cfgm_common.exceptions import BadRequest, NoIdError
from cfgm_common.exceptions import HttpError, RequestSizeError
from vnc_api.gen.resource_client import AccessControlList

from schema_transformer.resources._resource_base import ResourceBaseST
from schema_transformer.utils import _raise_and_send_uve_to_sandesh


def _access_control_list_update(acl_obj, name, obj, entries):
    if acl_obj is None:
        if entries is None:
            return None
        acl_obj = AccessControlList(name, obj, entries)
        try:
            # Remove stale acl
            acl_uuid = ResourceBaseST._vnc_lib.fq_name_to_id(
                obj_type='access-control-list',
                fq_name=obj.fq_name + [name])
            ResourceBaseST._vnc_lib.access_control_list_delete(id=acl_uuid)
        except Exception:
            pass
        try:
            ResourceBaseST._vnc_lib.access_control_list_create(acl_obj)
            return acl_obj
        except (NoIdError, BadRequest) as e:
            ResourceBaseST._logger.error(
                "Error while creating acl %s for %s: %s" %
                (name, obj.get_fq_name_str(), str(e)))
        except RequestSizeError:
            # log the error and raise an alarm
            ResourceBaseST._logger.error(
                "Bottle request size error while creating acl %s for %s" %
                (name, obj.get_fq_name_str()))
            err_info = {'acl rule limit exceeded': True}
            _raise_and_send_uve_to_sandesh('ACL', err_info,
                                           ResourceBaseST._sandesh)
        return None
    else:
        if entries is None:
            try:
                ResourceBaseST._vnc_lib.access_control_list_delete(
                    id=acl_obj.uuid)
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
            ResourceBaseST._vnc_lib.access_control_list_update(acl_obj)
        except HttpError as he:
            ResourceBaseST._logger.error(
                "HTTP error while updating acl %s for %s: %d, %s" %
                (name, obj.get_fq_name_str(), he.status_code, he.content))
        except NoIdError:
            ResourceBaseST._logger.error(
                "NoIdError while updating acl %s for %s" %
                (name, obj.get_fq_name_str()))
        except RequestSizeError:
            # log the error and raise an alarm
            ResourceBaseST._logger.error(
                "Bottle request size error while creating acl %s for %s" %
                (name, obj.get_fq_name_str()))
            err_info = {'acl rule limit exceeded': True}
            _raise_and_send_uve_to_sandesh('ACL', err_info,
                                           ResourceBaseST._sandesh)
    return acl_obj
# end _access_control_list_update
