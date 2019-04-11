#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common import CANNOT_MODIFY_MSG
from vnc_api.gen.resource_common import RoutingInstance

from vnc_cfg_api_server.context import is_internal_request
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class RoutingInstanceServer(ResourceMixin, RoutingInstance):
    @staticmethod
    def _check_default_routing_instance(obj_dict, db_obj_dict=None):
        """Prevent to update/delete default routing instance.

        Forbidden an external call to delete a default routing instance and
        also prevent an external call to change the default flag.
        """
        if is_internal_request():
            return True, ''
        if 'routing_instance_is_default' not in obj_dict:
            return True, ''
        if not db_obj_dict and not obj_dict['routing_instance_is_default']:
            # create and delete allowed if not default RI
            return True, ''
        elif (db_obj_dict and obj_dict['routing_instance_is_default'] ==
                db_obj_dict.get('routing_instance_is_default', False)):
            # update allowed if same as before
            return True, ''

        if not db_obj_dict:
            db_obj_dict = obj_dict
        msg = CANNOT_MODIFY_MSG % {
            'resource_type':
                RoutingInstance.object_type.replace('_', ' ').title(),
            'fq_name': ':'.join(db_obj_dict['fq_name']),
            'uuid': db_obj_dict['uuid'],
        }
        return False, (409, msg)

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls._check_default_routing_instance(obj_dict)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn,
                       prop_collection_updates=None, ref_update=None):
        ok, result = cls.locate(uuid=id, create_it=False, fields=[
            'parent_type', 'parent_uuid', 'routing_instance_is_default'])
        if not ok:
            return False, result
        db_obj_dict = result
        return cls._check_default_routing_instance(obj_dict, db_obj_dict)

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        return cls._check_default_routing_instance(obj_dict) + (None,)
