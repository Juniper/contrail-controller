#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import HostBasedService

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class HostBasedServiceServer(ResourceMixin, HostBasedService):
    @staticmethod
    def _check_only_one_vn_per_type(req_dict, db_dict=None):
        vn_type_map = {}
        if not db_dict:
            db_dict = {}

        for ref in req_dict.get('virtual_network_refs', []):
            if 'attr' in ref and ref['attr'].get('virtual_network_type'):
                vn_type_map.setdefault(
                    ref['attr'].get('virtual_network_type'),
                    set([]),
                ).add(ref['uuid'])
        for ref in db_dict.get('virtual_network_refs', []):
            if 'attr' in ref and ref['attr'].get('virtual_network_type'):
                vn_type_map.setdefault(
                    ref['attr'].get('virtual_network_type'),
                    set([]),
                ).add(ref['uuid'])

        for vn_type, vn_uuids in vn_type_map.items():
            if len(vn_uuids) > 1:
                msg = ("Virtual network type %s cannot be referenced by more "
                       "than one virtual network at a time" % vn_type)
                return False, (400, msg)

        return True, ''

    @staticmethod
    def _check_type(req_dict, db_dict):
        if ('host_based_service_type' in req_dict and
                req_dict['host_based_service_type'] !=
                db_dict['host_based_service_type']):
            msg = "Cannot change the Host Based Service type"
            return False, (400, msg)

        return True, ''

    @classmethod
    def host_based_service_enabled(cls, project_uuid):
        ok, result = cls.server.get_resource_class('project').locate(
            uuid=project_uuid, create_it=False, fields=['host_based_services'])
        if not ok:
            return False, result
        project = result

        hbss = project.get('host_based_services', [])
        if len(hbss) == 0:
            msg = ("Project %s(%s) have no host based service instantiated" %
                   (':'.join(project['fq_name']), project['uuid']))
            return True, (False, msg)

        if len(hbss) > 1:
            msg = ("Project %s(%s) have more than one host based service" %
                   (':'.join(project['fq_name']), project['uuid']))
            return True, (False, msg)

        ok, result = cls.server.get_resource_class(
            'host_based_service').locate(
                uuid=hbss[0]['uuid'], create_it=False,
                fields=['virtual_network_refs'])
        if not ok:
            return False, result
        hbs = result

        vn_types = set()
        for ref in hbs.get('virtual_network_refs', []):
            vn_types.add(ref['attr'].get('virtual_network_type'))
        if not set(['left', 'right']).issubset(vn_types):
            msg = ("Host based service %s(%s) needs at least one reference to "
                   "left and right virtual network to be considered as enable"
                   % (':'.join(hbs['fq_name']), hbs['uuid']))
            return True, (False, msg)

        return True, (True, '')

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        obj_dict.setdefault('host_based_service_type', 'firewall')
        return cls._check_only_one_vn_per_type(obj_dict)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, result = cls.locate(uuid=id, create_it=False,
                                fields=['virtual_network_refs',
                                        'host_based_service_type'])
        if not ok:
            return False, result
        db_obj_dict = result

        ok, result = cls._check_type(obj_dict, db_obj_dict)
        if not ok:
            return False, result

        return cls._check_only_one_vn_per_type(obj_dict, db_obj_dict)
