#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import copy

from cfgm_common import _obj_serializer_all
from cfgm_common import jsonutils as json
from cfgm_common.exceptions import HttpError
from cfgm_common.exceptions import NoIdError
from sandesh_common.vns import constants
from vnc_api.gen.resource_xsd import QuotaType

# from vnc_cfg_api_server.resources.
from vnc_cfg_api_server.resources.alarm import AlarmServer
from vnc_cfg_api_server.resources.alias_ip import AliasIpServer
from vnc_cfg_api_server.resources.application_policy_set import\
    ApplicationPolicySetServer
from vnc_cfg_api_server.resources.bgp_as_a_service import BgpAsAServiceServer
from vnc_cfg_api_server.resources.bgpvpn import BgpvpnServer
from vnc_cfg_api_server.resources.bridge_domain import BridgeDomainServer
from vnc_cfg_api_server.resources.data_center_interconnect import\
    DataCenterInterconnectServer
from vnc_cfg_api_server.resources.domain import DomainServer
from vnc_cfg_api_server.resources.firewall_rule import FirewallPolicyServer
from vnc_cfg_api_server.resources.firewall_rule import FirewallRuleServer
from vnc_cfg_api_server.resources.floating_ip import FloatingIpServer
from vnc_cfg_api_server.resources.floating_ip_pool import FloatingIpPoolServer
from vnc_cfg_api_server.resources.forwarding_class import ForwardingClassServer
from vnc_cfg_api_server.resources.global_system_config import\
    GlobalSystemConfigServer
from vnc_cfg_api_server.resources.instance_ip import InstanceIpServer
from vnc_cfg_api_server.resources.logical_interface import\
    LogicalInterfaceServer
from vnc_cfg_api_server.resources.logical_router import LogicalRouterServer
from vnc_cfg_api_server.resources.network_ipam import NetworkIpamServer
from vnc_cfg_api_server.resources.network_policy import NetworkPolicyServer
from vnc_cfg_api_server.resources.physical_interface import\
    PhysicalInterfaceServer
from vnc_cfg_api_server.resources.physical_router import PhysicalRouterServer
from vnc_cfg_api_server.resources.project import ProjectServer
from vnc_cfg_api_server.resources.qos_config import QosConfigServer
from vnc_cfg_api_server.resources.route_aggregate import RouteAggregateServer
from vnc_cfg_api_server.resources.route_table import RouteTableServer
from vnc_cfg_api_server.resources.security_group import SecurityGroupServer
from vnc_cfg_api_server.resources.service_appliance_set import\
    ServiceApplianceSetServer
from vnc_cfg_api_server.resources.service_group import ServiceGroupServer
from vnc_cfg_api_server.resources.service_template import ServiceTemplateServer
from vnc_cfg_api_server.resources.tag import TagServer
from vnc_cfg_api_server.resources.tag_type import TagTypeServer
from vnc_cfg_api_server.resources.virtual_dns import VirtualDnsServer
from vnc_cfg_api_server.resources.virtual_dns_record import\
    VirtualDnsRecordServer
from vnc_cfg_api_server.resources.virtual_machine_interface import\
    VirtualMachineInterfaceServer
from vnc_cfg_api_server.resources.virtual_network import VirtualNetworkServer
from vnc_cfg_api_server.resources.virtual_router import VirtualRouterServer
from vnc_cfg_api_server.vnc_quota import QuotaHelper


__all__ = [
    AlarmServer,
    AliasIpServer,
    ApplicationPolicySetServer,
    BgpAsAServiceServer,
    BgpvpnServer,
    BridgeDomainServer,
    DataCenterInterconnectServer,
    DomainServer,
    FirewallPolicyServer,
    FirewallRuleServer,
    FloatingIpServer,
    FloatingIpPoolServer,
    ForwardingClassServer,
    GlobalSystemConfigServer,
    InstanceIpServer,
    LogicalInterfaceServer,
    LogicalRouterServer,
    NetworkIpamServer,
    NetworkPolicyServer,
    PhysicalInterfaceServer,
    PhysicalRouterServer,
    ProjectServer,
    QosConfigServer,
    RouteAggregateServer,
    RouteTableServer,
    SecurityGroupServer,
    ServiceApplianceSetServer,
    ServiceGroupServer,
    ServiceTemplateServer,
    TagServer,
    TagTypeServer,
    VirtualDnsServer,
    VirtualDnsRecordServer,
    VirtualMachineInterfaceServer,
    VirtualNetworkServer,
    VirtualRouterServer,
]


class ResourceMixin(object):
    server = None

    class __metaclass__(type):
        @property
        def db_conn(cls):
            return cls.server.get_db_connection()

        @property
        def addr_mgmt(cls):
            return cls.server._addr_mgmt

        @property
        def vnc_zk_client(cls):
            return cls.db_conn._zk_db

    @classmethod
    def get_project_id_for_resource(cls, obj_dict, obj_type, db_conn):
        proj_uuid = None
        if 'project_refs' in obj_dict:
            proj_dict = obj_dict['project_refs'][0]
            proj_uuid = proj_dict.get('uuid')
            if not proj_uuid:
                proj_uuid = db_conn.fq_name_to_uuid('project', proj_dict['to'])
        elif ('parent_type' in obj_dict and
                obj_dict['parent_type'] == 'project'):
            proj_uuid = obj_dict['parent_uuid']
        elif (obj_type == 'loadbalancer_member' or
              obj_type == 'floating_ip_pool' and 'parent_uuid' in obj_dict):
            # loadbalancer_member and floating_ip_pool have only one parent
            # type
            ok, proj_res = cls.dbe_read(db_conn, cls.parent_types[0],
                                        obj_dict['parent_uuid'],
                                        obj_fields=['parent_uuid'])
            if not ok:
                return proj_uuid  # None

            proj_uuid = proj_res['parent_uuid']

        return proj_uuid

    @classmethod
    def get_quota_for_resource(cls, obj_type, obj_dict, db_conn):
        user_visible = obj_dict['id_perms'].get('user_visible', True)
        if not user_visible or obj_type not in QuotaType.attr_fields:
            return True, -1, None

        proj_uuid = cls.get_project_id_for_resource(obj_dict, obj_type,
                                                    db_conn)

        if proj_uuid is None:
            return True, -1, None

        ok, result = QuotaHelper.get_project_dict_for_quota(proj_uuid, db_conn)
        if not ok:
            return False, result
        proj_dict = result

        quota_limit = QuotaHelper.get_quota_limit(proj_dict, obj_type)
        return True, quota_limit, proj_uuid

    @classmethod
    def no_pending_deleted_resource_in_refs(cls, obj_dict):
        # Check if any reference points to a pending deleted resource
        if not obj_dict:
            return True, ''

        refs = [(ref_type, ref.get('to'), ref.get('uuid'))
                for ref_type in constants.SECURITY_OBJECT_TYPES
                for ref in obj_dict.get('%s_refs' % ref_type, [])]
        for ref_type, ref_fq_name, ref_uuid in refs:
            ref_class = cls.server.get_resource_class(ref_type)
            ok, result = ref_class.locate(
                fq_name=ref_fq_name,
                uuid=ref_uuid,
                create_it=False,
                fields=['fq_name', 'parent_type', 'draft_mode_state'],
            )
            if not ok:
                return False, result
            ref = result
            if (ref['fq_name'][-2] !=
                    constants.POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT):
                ok, result = ref_class.get_pending_resource(
                    ref, fields=['draft_mode_state'])
                if ok and result == '':
                    # draft mode not enabled
                    continue
                elif not ok and isinstance(result, tuple) and result[0] == 404:
                    # draft mode enabled, but no draft version of the
                    # referenced resource
                    continue
                elif not ok:
                    return False, result
                draft_ref = result
            else:
                draft_ref = ref
            if draft_ref.get('draft_mode_state') == 'deleted':
                msg = ("Referenced %s resource '%s' (%s) is in pending delete "
                       "state, it cannot be referenced" %
                       (ref_type.replace('_', ' ').title(),
                        ':'.join(ref['fq_name']), ref_uuid))
                return False, (400, msg)
        return True, ''

    @classmethod
    def pending_dbe_create(cls, obj_dict):
        return True, ''

    @classmethod
    def pre_dbe_alloc(cls, obj_dict):
        return True, ''

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return True, ''

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return True, ''

    @classmethod
    def pending_dbe_update(cls, obj_dict, delta_obj_dict=None,
                           prop_collection_updates=None):
        return True, ''

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn,
                       prop_collection_updates=None, ref_update=None):
        return True, ''

    @classmethod
    def post_dbe_update(cls, id, fq_name, obj_dict, db_conn,
                        prop_collection_updates=None, ref_update=None):
        return True, ''

    @classmethod
    def pending_dbe_delete(cls, obj_dict):
        return True, ''

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        return True, '', None

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn, **kwargs):
        return True, ''

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        return True, ''

    @classmethod
    def dbe_update_notification(cls, obj_id, extra_dict=None):
        return True, ''

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        return True, ''

    @classmethod
    def pre_dbe_read(cls, id, fq_name, db_conn):
        return True, ''

    @classmethod
    def post_dbe_read(cls, obj_dict, db_conn):
        return True, ''

    @classmethod
    def pre_dbe_list(cls, obj_uuids, db_conn):
        return True, ''

    @classmethod
    def post_dbe_list(cls, obj_dict_list, db_conn):
        return True, ''

    @classmethod
    def dbe_read(cls, db_conn, res_type, obj_uuid, obj_fields=None):
        try:
            ok, result = db_conn.dbe_read(res_type, obj_uuid, obj_fields)
        except NoIdError as e:
            return False, (404, str(e))
        if not ok:
            return False, result
        return True, result

    @classmethod
    def locate(cls, fq_name=None, uuid=None, create_it=True, **kwargs):
        if fq_name is not None and uuid is None:
            try:
                uuid = cls.db_conn.fq_name_to_uuid(cls.object_type, fq_name)
            except NoIdError as e:
                if create_it:
                    pass
                else:
                    return False, (404, str(e))
        if uuid:
            try:
                ok, result = cls.db_conn.dbe_read(
                    cls.object_type, uuid, obj_fields=kwargs.get('fields'))
            except NoIdError as e:
                if create_it:
                    pass
                else:
                    return False, (404, str(e))
            if not ok:
                return False, result
            else:
                return ok, result

        # Does not exist, create it. Need at least an fq_name
        if fq_name is None or fq_name == []:
            msg = ("Cannot create %s without at least a FQ name" %
                   cls.object_type.replace('_', ' ').title())
            return False, (400, msg)
        parent_obj = None
        if kwargs.get('parent_type') is not None:
            parent_class = cls.server.get_resource_class(kwargs['parent_type'])
            parent_obj = parent_class(fq_name=fq_name[:-1])
            parent_obj.uuid = kwargs.get('parent_uuid')
        obj = cls(parent_obj=parent_obj, **kwargs)
        obj.fq_name = fq_name
        obj.uuid = kwargs.get('uuid')
        obj_dict = json.loads(json.dumps(obj, default=_obj_serializer_all))
        for ref_name in cls.ref_fields & set(kwargs.keys()):
            obj_dict[ref_name] = copy.deepcopy(kwargs[ref_name])
        try:
            cls.server.internal_request_create(cls.resource_type, obj_dict)
        except HttpError as e:
            return False, (e.status_code, e.content)
        try:
            uuid = cls.db_conn.fq_name_to_uuid(cls.object_type, fq_name)
        except NoIdError as e:
            return False, (404, str(e))
        return cls.db_conn.dbe_read(cls.object_type, obj_id=uuid)
