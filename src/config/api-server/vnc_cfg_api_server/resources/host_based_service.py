#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common import _obj_serializer_all
from cfgm_common import jsonutils as json
from cfgm_common.exceptions import HttpError
from vnc_api.gen.resource_common import HostBasedService
from vnc_api.gen.resource_xsd import IpamSubnetType
from vnc_api.gen.resource_xsd import ServiceVirtualNetworkType
from vnc_api.gen.resource_xsd import SubnetType
from vnc_api.gen.resource_xsd import VnSubnetsType

from vnc_cfg_api_server.context import is_internal_request
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class HostBasedServiceServer(ResourceMixin, HostBasedService):
    LEFT = 'left'
    RIGHT = 'right'
    hbf = {
        LEFT: {
            'IP': '0.1.1.0',
            'PREFIX_LEN': '24',
            'NAME': 'hbf-left'
        },
        RIGHT: {
            'IP': '0.2.2.0',
            'PREFIX_LEN': '24',
            'NAME': 'hbf-right'
        }
    }

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
    def _check_for_allowed_configure_vn_types(fq_name, uuid, req_dict):
        if is_internal_request():
            return True, ''
        vn_types = ['management']
        for ref in req_dict.get('virtual_network_refs', []):
            if ref['attr'].get('virtual_network_type') not in vn_types:
                msg = ("Host based service (%s, %s) allows only (%s) "
                       "network types" % (':'.join(fq_name), uuid,
                                          ','.join(vn_types)))
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
        ok, result = cls.server.get_resource_class(
            'project').locate(
            uuid=project_uuid, create_it=False,
            fields=['host_based_services'])
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
    def _create_vn(cls, vn_fq_name, svn_type, hbs_uuid, parent_uuid):
        # Create a VN
        attrs = {
            'parent_type': 'project',
            'parent_uuid': parent_uuid,
        }
        ok, result = cls.server.get_resource_class(
            'virtual_network').locate(fq_name=vn_fq_name, **attrs)
        if not ok:
            return False, result
        vn_dict = result
        vn_uuid = vn_dict['uuid']

        attr = ServiceVirtualNetworkType(svn_type)
        attr_as_dict = attr.__dict__
        try:
            cls.server.internal_request_ref_update(
                'host-based-service', hbs_uuid, 'ADD',
                'virtual-network', vn_uuid,
                attr=attr_as_dict)
        except HttpError as e:
            return False, (e.status_code, e.content)

        # Create a subnet and link it to default ipam
        # ref update to VN
        ipam_obj_type = 'network_ipam'
        ipam_fq_name = ['default-domain',
                        'default-project', 'default-network-ipam']
        ipam_uuid = cls.db_conn.fq_name_to_uuid(ipam_obj_type, ipam_fq_name)
        subnet = SubnetType(cls.hbf[svn_type]['IP'],
                            cls.hbf[svn_type]['PREFIX_LEN'])
        attr = VnSubnetsType([IpamSubnetType(subnet=subnet)])
        attr_as_dict = json.loads(json.dumps(
            attr, default=_obj_serializer_all))
        try:
            cls.server.internal_request_ref_update(
                'virtual-network', vn_uuid, 'ADD',
                'network-ipam', ipam_uuid,
                ref_fq_name=ipam_fq_name,
                attr=attr_as_dict)
        except HttpError as e:
            return False, (e.status_code, e.content)
        return ok, vn_dict

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        proj_name = obj_dict['fq_name'][:-1]
        # Create Left VN
        left = '__%s-%s__' % (obj_dict['fq_name'][-1:][0],
                              cls.hbf[cls.LEFT]['NAME'])
        vn_left_fq_name = proj_name + [left]
        ok, resp = cls._create_vn(vn_left_fq_name, cls.LEFT,
                                  obj_dict['uuid'], obj_dict['parent_uuid'])
        if not ok:
            return ok, resp

        # Create Right VN
        right = '__%s-%s__' % (obj_dict['fq_name'][-1:][0],
                               cls.hbf[cls.RIGHT]['NAME'])
        vn_right_fq_name = proj_name + [right]
        ok, resp = cls._create_vn(vn_right_fq_name, cls.RIGHT,
                                  obj_dict['uuid'], obj_dict['parent_uuid'])
        if not ok:
            return ok, resp

        return True, ''

    @classmethod
    def pre_dbe_create(cls, fq_name, obj_dict, db_conn):
        obj_dict.setdefault('host_based_service_type', 'firewall')
        ok, result = cls._check_for_allowed_configure_vn_types(
            fq_name, obj_dict.get('uuid', ''), obj_dict)
        if not ok:
            return False, result
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

        ok, result = cls._check_for_allowed_configure_vn_types(
            fq_name, id, obj_dict)
        if not ok:
            return False, result

        ok, result = cls._check_only_one_vn_per_type(obj_dict, db_obj_dict)
        if not ok:
            return ok, result

        if not is_internal_request() and \
           'virtual_network_refs' in obj_dict and \
           'virtual_network_refs' in db_obj_dict:
            obj_dict['virtual_network_refs'] += \
                db_obj_dict['virtual_network_refs']
        return True, ''

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        for ref in obj_dict.get('virtual_network_refs', []):
            ok, result = cls.server.get_resource_class(
                'virtual_network').locate(
                uuid=ref['uuid'],
                fields=['virtual_machine_interface_back_refs', 'fq_name'],
                create_it=False)
            if not ok:
                return False, result, None
            if ok:
                if result.get('virtual_machine_interface_back_refs', []):
                    msg = ("HBS object (%s, %s), virtual network (%s) "
                           "got interfaces, cannot be removed"
                           % (obj_dict['fq_name'],
                              id, ':'.join(result['fq_name'])))
                    return False, (400, msg), None
        return True, "", None

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn):
        vn_types = ['left', 'right']
        for ref in obj_dict.get('virtual_network_refs', []):
            if ref['attr'].get('virtual_network_type') in vn_types:
                try:
                    cls.server.internal_request_delete('virtual-network',
                                                       ref['uuid'])
                except HttpError as e:
                    msg = ("Virtual network ref (%s, %s)"
                           "delete failed, status code %s, content (%s) "
                           % (ref['uuid'], ':'.join(ref['id']),
                              e.status_code, e.content))
                    return True, msg
        return True, ''

    # get namespace template
    @staticmethod
    def _get_hbs_namespace(namespace):
        hbs_ns = {
            'apiVersion': 'v1',
            'kind': 'Namespace',
            'metadata': {'annotations': {'opencontrail.org/isolation': 'true'},
                         'name': '<namespace>'}
        }
        hbs_ns['metadata']['name'] = namespace
        return hbs_ns

    # end _get_hbs_namespace

    @staticmethod
    def _get_network(fqn):
        if isinstance(fqn, list) and len(fqn) == 3:
            return '{"domain":"%s", "project":"%s", "name":"%s"}' % (
                   fqn[0], fqn[1], fqn[2])
        return ''
