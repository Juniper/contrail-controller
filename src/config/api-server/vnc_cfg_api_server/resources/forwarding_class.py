#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import ForwardingClass

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class ForwardingClassServer(ResourceMixin, ForwardingClass):
    @staticmethod
    def _check_fc_id(obj_dict, db_conn):
        fc_id = 0
        if obj_dict.get('forwarding_class_id'):
            fc_id = obj_dict.get('forwarding_class_id')

        id_filters = {'forwarding_class_id': [fc_id]}
        ok, forwarding_class_list, _ = db_conn.dbe_list('forwarding_class',
                                                        filters=id_filters)
        if not ok:
            return False, forwarding_class_list

        if len(forwarding_class_list) != 0:
            msg = ("Forwarding class %s is configured with a id %d" %
                   (forwarding_class_list[0][0], fc_id))
            return False, (400, msg)
        return True, ''

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls._check_fc_id(obj_dict, db_conn)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, forwarding_class = cls.dbe_read(db_conn, 'forwarding_class', id)
        if not ok:
            return ok, forwarding_class

        if 'forwarding_class_id' in obj_dict:
            fc_id = obj_dict['forwarding_class_id']
            if 'forwarding_class_id' in forwarding_class:
                if fc_id != forwarding_class.get('forwarding_class_id'):
                    return cls._check_fc_id(obj_dict, db_conn)
        return (True, '')
