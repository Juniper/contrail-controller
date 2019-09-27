#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common import _obj_serializer_all
from cfgm_common import jsonutils as json
from cfgm_common.exceptions import HttpError
from vnc_api.gen.resource_common import HostBasedService
from vnc_api.gen.resource_common import VirtualNetwork
from vnc_api.gen.resource_common import NetworkIpam
from vnc_api.gen.resource_xsd import VnSubnetsType
from vnc_api.gen.resource_xsd import IpamSubnetType
from vnc_api.gen.resource_xsd import SubnetType
from vnc_api.gen.resource_xsd import ServiceVirtualNetworkType
from vnc_cfg_api_server.resources._resource_base import ResourceMixin
from vnc_cfg_api_server.context import is_internal_request


class HostBasedServiceServer(ResourceMixin, HostBasedService):
    LEFT = 'left'
    RIGHT = 'right'
    hbf = {
        LEFT: {
            'IP': '1.1.1.0',
            'PREFIX_LEN': '24',
            'NAME': 'hbf-left'
        },
        RIGHT: {
            'IP': '2.2.2.0',
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
    def _check_for_allowed_configure_vn_types(fq_name, req_dict):
        # interal request, return True
        if is_internal_request():
            return True, ''
        vn_types = ['management']
        for ref in req_dict.get('virtual_network_refs', []):
            if ref['attr'].get('virtual_network_type') not in vn_types:
                msg = ("Host based service (%s) allows only (%s) "
                       "network types" % (':'.join(fq_name), ','.join(map(str, vn_types))))
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
    def _create_vn(cls, vn_fq_name, svn_type, parent_uuid, db_conn):
        api_server = db_conn.get_api_server()
        # Create a VN
        ok, result = cls.server.get_resource_class(
            'virtual_network').locate(fq_name=vn_fq_name)
        if not ok:
            return False, result
        vn_obj = result
        vn_uuid = vn_obj['uuid']

        attr = ServiceVirtualNetworkType(svn_type)
        attr_as_dict = attr.__dict__
        try:
            api_server.internal_request_ref_update(
                'host-based-service', parent_uuid, 'ADD',
                'virtual-network', vn_uuid,
                ref_fq_name=vn_fq_name,
                attr=attr_as_dict)
        except HttpError as e:
            return False, (e.status_code, e.content)

        # Create a subnet and link it to default ipam
        # ref update to VN 
        ipam_obj_type = 'network_ipam'
        ipam_fq_name = ['default-domain', 'default-project', 'default-network-ipam']
        ipam_uuid = db_conn.fq_name_to_uuid(ipam_obj_type, ipam_fq_name)
        subnet = SubnetType(cls.hbf[svn_type]['IP'],
                            cls.hbf[svn_type]['PREFIX_LEN'])
        attr = VnSubnetsType([IpamSubnetType(subnet=subnet)])
        attr_as_dict = json.loads(json.dumps(attr, default=_obj_serializer_all))
        try:
            api_server.internal_request_ref_update(
                'virtual-network', vn_uuid, 'ADD',
                'network-ipam', ipam_uuid,
                attr=attr_as_dict)
        except HttpError as e:
            return False, (e.status_code, e.content)
        return ok, vn_obj

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        api_server = db_conn.get_api_server()
        ok, result = cls.server.get_resource_class('project').locate(
            uuid=obj_dict['parent_uuid'], create_it=False, fields=['annotations'])
        if not ok:
            return False, result
        project = result
        cluster = ''
        if project.get('annotations'):
            kvs = project.get('annotations', {}).get('key_value_pair', [])
            for kv in kvs:
                if kv.get('key') == 'cluster': cluster = kv['value']; break
        # left = cluster + cls.hbf[cls.LEFT]['NAME']
        left = cls.hbf[cls.LEFT]['NAME']

        # Create Left VN
        proj_name = obj_dict['fq_name'][:-1]
        vn_left_fq_name = proj_name + [left]
        ok, resp = cls._create_vn(vn_left_fq_name, cls.LEFT,
                                  obj_dict['uuid'], db_conn)
        if not ok:
            return ok, resp

        # Create Right VN
        # right = cluster + cls.hbf[cls.RIGHT]['NAME']
        right = cls.hbf[cls.RIGHT]['NAME']
        vn_right_fq_name = proj_name + [right]
        ok, resp = cls._create_vn(vn_right_fq_name, cls.RIGHT,
                                  obj_dict['uuid'], db_conn)
        if not ok:
            return ok, resp

        return True, ''

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        obj_dict.setdefault('host_based_service_type', 'firewall')
        ok, result = cls._check_for_allowed_configure_vn_types(tenant_name, obj_dict)
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

        ok, result = cls._check_for_allowed_configure_vn_types(fq_name, obj_dict)
        if not ok:
            return False, result

        return cls._check_only_one_vn_per_type(obj_dict, db_obj_dict)

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        ok, result = cls.locate(uuid=id, create_it=False,
                                fields=['virtual_network_refs',
                                        'host_based_service_type'])
        if not ok:
            return False, result
        db_obj_dict = result
        for ref in db_obj_dict.get('virtual_network_refs', []):
            ok, result = cls.server.get_resource_class('virtual_network').locate(
                uuid=ref['uuid'],
                fields=['virtual_machine_interface_back_refs', 'fq_name'],
                create_it=False)
            if ok:
                if result.get('virtual_machine_interface_back_refs', []):
                    msg = ("HBS object virtual network (%s) got interfaces"
                           " cannot be removed" % ':'.join(result['fq_name']))
                    return False, (400, msg)
        return True, "", None

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn):
        api_server = db_conn.get_api_server()
        vn_types = ['left', 'right']
        for ref in obj_dict.get('virtual_network_refs', []):
            if ref['attr'].get('virtual_network_type') in vn_types:
                api_server.internal_request_delete('virtual-network',
                                                   ref['uuid'])

        return True, ''
