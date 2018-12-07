#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
#
# This file contains code/hooks at different point during processing a request,
# specific to type of resource. For eg. allocation of mac/ip-addr for a port
# during its creation.
import copy
from functools import wraps
import itertools
import re
import socket
import netaddr
import uuid

from cfgm_common import jsonutils as json
from cfgm_common import get_lr_internal_vn_name
from cfgm_common import _obj_serializer_all
import cfgm_common
from cfgm_common.utils import _DEFAULT_ZK_COUNTER_PATH_PREFIX
import cfgm_common.exceptions
import vnc_quota
from vnc_quota import QuotaHelper
from provision_defaults import PERMS_RWX
from provision_defaults import PERMS_RX

from context import get_context
from context import is_internal_request
from vnc_api.gen.resource_xsd import *
from vnc_api.gen.resource_common import *
from netaddr import IPNetwork, IPAddress, IPRange
from pprint import pformat
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns import constants


class ServiceApplianceSetServer(Resource, ServiceApplianceSet):
    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        (ok, result, _) = db_conn.dbe_list(
            'loadbalancer_pool', back_ref_uuids=[id])
        if not ok:
            return (ok, (500, 'Error in dbe_list: %s' %(result)))
        if len(result) > 0:
            msg = "Service appliance set can not be updated as loadbalancer "\
                  "pools are using it"
            return (False, (409, msg))
        return True, ""
    # end pre_dbe_update
# end class ServiceApplianceSetServer


class BridgeDomainServer(Resource, BridgeDomain):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        vn_uuid = obj_dict.get('parent_uuid')
        if vn_uuid is None:
            vn_uuid = db_conn.fq_name_to_uuid('virtual_network', obj_dict['fq_name'][0:3])
        ok, result = cls.dbe_read(db_conn, 'virtual_network', vn_uuid,
                                  obj_fields=['bridge_domains'])
        if not ok:
            return ok, result
        if 'bridge_domains' in result and len(result['bridge_domains']) == 1:
            msg = "Virtual network(%s) can have only one bridge domain. Bridge"\
                  " domain(%s) is already created under this virtual network" %\
                  (vn_uuid, result['bridge_domains'][0]['uuid'])
            return (False, (400, msg))
        return True, ""
    # end pre_dbe_create
# end class BridgeDomainServer


class ServiceGroupServer(SecurityResourceBase, ServiceGroup):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls.check_draft_mode_state(obj_dict)
        if not ok:
            return False, result

        # create protcol id
        try:
            firewall_services = obj_dict['service_group_firewall_service_list']['firewall_service']
        except Exception as e:
            return True, ""

        for service in firewall_services:
            if service.get('protocol') is None:
                continue
            protocol = service['protocol']
            if protocol.isdigit():
                protocol_id = int(protocol)
                if protocol_id < 0 or protocol_id > 255:
                    return (False, (400, 'Rule with invalid protocol : %s' %
                                protocol))
            elif protocol not in cfgm_common.proto_dict:
                return (False, (400, 'Invalid protocol : %s' % protocol))
            else:
                protocol_id = cfgm_common.proto_dict[protocol]
            service['protocol_id'] = protocol_id

        return True, ""
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls.pre_dbe_create(None, obj_dict, db_conn)


class TagTypeServer(Resource, TagType):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        type_str = obj_dict['fq_name'][-1]
        obj_dict['name'] = type_str
        obj_dict['display_name'] = type_str

        if obj_dict.get('tag_type_id') is not None:
            msg = "Tag Type ID is not setable"
            return False, (400, msg)

        # Allocate ID for tag-type
        type_id = cls.vnc_zk_client.alloc_tag_type_id(type_str)

        def undo_type_id():
            cls.vnc_zk_client.free_tag_type_id(type_id, obj_dict['fq_name'])
            return True, ""
        get_context().push_undo(undo_type_id)

        obj_dict['tag_type_id'] = "0x{:04x}".format(type_id)

        return True, ""

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        # User can't update type or value once created
        if obj_dict.get('display_name') or obj_dict.get('tag_type_id'):
            msg = "Tag Type value or ID cannot be updated"
            return False, (400, msg)

        return True, ""

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn, **kwargs):
        # Deallocate tag-type ID
        cls.vnc_zk_client.free_tag_type_id(int(obj_dict['tag_type_id'], 0),
                                           obj_dict['fq_name'][-1])
        return True, ''

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        cls.vnc_zk_client.alloc_tag_type_id(
            ':'.join(obj_dict['fq_name']), int(obj_dict['tag_type_id'], 0))

        return True, ''

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        # Deallocate in memory tag-type ID
        cls.vnc_zk_client.free_tag_type_id(int(obj_dict['tag_type_id'], 0),
                                           obj_dict['fq_name'][-1],
                                           notify=True)

        return True, ''

    @classmethod
    def get_tag_type_id(cls, type_str):
        if type_str in constants.TagTypeNameToId:
            return True, constants.TagTypeNameToId[type_str]

        ok, result = cls.locate(fq_name=[type_str], create_it=False,
                                fields=['tag_type_id'])
        if not ok:
            return False, result

        return True, int(tag_type['tag_type_id'], 0)


class TagServer(Resource, Tag):
    @classmethod
    def pre_dbe_alloc(cls, obj_dict):
        type_str = obj_dict.get('tag_type_name')
        value_str = obj_dict.get('tag_value')

        if type_str is None or value_str is None:
            msg = "Tag must be created with a type and a value"
            return False, (400, msg)

        type_str = type_str.lower()
        name = '%s=%s' % (type_str, value_str)
        obj_dict['name'] = name
        obj_dict['fq_name'][-1] = name
        obj_dict['display_name'] = name
        obj_dict['tag_type_name'] = type_str
        obj_dict['tag_value'] = value_str

        return True, ''

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        type_str = obj_dict['tag_type_name']
        value_str = obj_dict['tag_value']

        if obj_dict.get('tag_id') is not None:
            msg = "Tag ID is not setable"
            return False, (400, msg)

        if obj_dict.get('tag_type_refs') is not None:
            msg = "Tag Type reference is not setable"
            return False, (400, msg)

        ok, result = TagTypeServer.locate(
            [type_str], id_perms=IdPermsType(user_visible=False))
        if not ok:
            return False, result
        tag_type = result

        def undo_tag_type():
            cls.server.internal_request_delete('tag-type', tag_type['uuid'])
            return True, ''
        get_context().push_undo(undo_tag_type)

        obj_dict['tag_type_refs'] = [
            {
                'uuid': tag_type['uuid'],
                'to': tag_type['fq_name'],
            },
        ]

        # Allocate ID for tag value. Use the all fq_name to distinguish same
        # tag values between global and scoped
        value_id = cls.vnc_zk_client.alloc_tag_value_id(
            type_str, ':'.join(obj_dict['fq_name']))

        def undo_value_id():
            cls.vnc_zk_client.free_tag_value_id(type_str, value_id,
                                                ':'.join(obj_dict['fq_name']))
            return True, ""
        get_context().push_undo(undo_value_id)

        # Compose Tag ID with the type ID and value ID
        obj_dict['tag_id'] = "{}{:04x}".format(tag_type['tag_type_id'],
                                               value_id)

        return True, ""

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        # User cannot update display_name, type, value or id once created
        if (obj_dict.get('display_name') is not None or
                obj_dict.get('tag_type_name') is not None or
                obj_dict.get('tag_value') is not None or
                obj_dict.get('tag_id') is not None):
            msg = "Tag name, type, value or ID cannot be updated"
            return (False, (400, msg))

        if obj_dict.get('tag_type_refs') is not None:
            msg = "Tag-type reference cannot be updated"
            return (False, (400, msg))

        return True, ""

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn, **kwargs):
        # Don't de-allocate ID and remove pre-defined tag types
        if obj_dict['tag_type_name'] in constants.TagTypeNameToId:
            return True, ''

        # Deallocate ID for tag value
        value_id = int(obj_dict['tag_id'], 0) & 0x0000ffff
        cls.vnc_zk_client.free_tag_value_id(obj_dict['tag_type_name'],
                                            value_id,
                                            ':'.join(obj_dict['fq_name']))

        # Try to delete referenced tag-type and ignore RefExistError which
        # means it's still in use by other Tag resource
        if obj_dict.get('tag_type_refs') is not None:
            # Tag can have only one tag-type reference
            tag_type_uuid = obj_dict['tag_type_refs'][0]['uuid']
            try:
                cls.server.internal_request_delete('tag-type', tag_type_uuid)
            except cfgm_common.exceptions.HttpError as e:
                if e.status_code != 409:
                    return False, (e.status_code, e.content)

        return True, ""

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        cls.vnc_zk_client.alloc_tag_value_id(
            obj_dict['tag_type_name'],
            ':'.join(obj_dict['fq_name']),
            int(obj_dict['tag_id'], 0) & 0x0000ffff,
        )

        return True, ''

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        cls.vnc_zk_client.free_tag_value_id(
            obj_dict['tag_type_name'],
            int(obj_dict['tag_id'], 0) & 0x0000ffff,
            ':'.join(obj_dict['fq_name']),
            notify=True,
        )

        return True, ''


class FirewallRuleServer(SecurityResourceBase, FirewallRule):
    @classmethod
    def _check_endpoint(cls, obj_dict):
        # TODO(ethuleau): Authorize only one condition per endpoint for the
        #                 moment.
        for ep_name in ['endpoint_1', 'endpoint_2']:
            ep = obj_dict.get(ep_name)
            if ep is None:
                continue

            filters = [
                ep.get('tags'),
                ep.get('address_group'),
                ep.get('virtual_network'),
                ep.get('subnet'),
                ep.get('any'),
            ]
            filters = [f for f in filters if f]
            if len(filters) > 1:
                msg = "Endpoint is limited to only one endpoint type at a time"
                return False, (400, msg)
            # check no ids present
            # check endpoints exclusivity clause
            # validate VN name in endpoints
        return True, ''

    @classmethod
    def _frs_fix_service(cls, service, service_group_refs):
        if service and service_group_refs:
            msg = ("Firewall Rule cannot have both defined 'service' property "
                   "and Service Group reference(s)")
            return False, (400, msg)

        if not service and not service_group_refs:
            msg = ("Firewall Rule requires at least 'service' property or "
                   "Service Group references(s)")
            return False, (400, msg)
        return True, ''

    @classmethod
    def _frs_fix_service_protocol(cls, obj_dict):
        if obj_dict.get('service') and obj_dict['service'].get('protocol'):
            protocol = obj_dict['service']['protocol']
            if protocol.isdigit():
                protocol_id = int(protocol)
                if protocol_id < 0 or protocol_id > 255:
                    return (False, (400, 'Rule with invalid protocol : %s' %
                                protocol))
            elif protocol not in cfgm_common.proto_dict:
                return (False, (400, 'Rule with invalid protocol : %s' % protocol))
            else:
                protocol_id = cfgm_common.proto_dict[protocol]
            obj_dict['service']['protocol_id'] = protocol_id

        return True, ""

    @classmethod
    def _frs_fix_match_tags(cls, obj_dict):
        if 'match_tags' in obj_dict:
            obj_dict['match_tag_types'] = {'tag_type': []}
            for tag_type in obj_dict['match_tags'].get('tag_list', []):
                tag_type = tag_type.lower()
                if tag_type == 'label':
                    return (False, (400, 'labels not allowed as match-tags'))

                ok, result = TagTypeServer.get_tag_type_id(tag_type)
                if not ok:
                    return False, result
                tag_type_id = result
                obj_dict['match_tag_types']['tag_type'].append(tag_type_id)

        return True, ""

    @classmethod
    def _frs_fix_endpoint_address_group(cls, obj_dict, db_obj_dict=None):
        ag_refs = []
        db_ag_refs = []
        if db_obj_dict:
            db_ag_refs = db_obj_dict.get('address_group_refs', [])

        if (not is_internal_request() and 'address_group_refs' in obj_dict and
                (db_obj_dict or obj_dict['address_group_refs'])):
            msg = ("Cannot directly define Address Group reference from a "
                   "Firewall Rule. Use 'address_group' endpoints property in "
                   "the Firewall Rule")
            return False, (400, msg)

        for ep_name in ['endpoint_1', 'endpoint_2']:
            if (ep_name not in obj_dict and db_obj_dict and
                    ep_name in db_obj_dict):
                ep = db_obj_dict.get(ep_name)
                if ep is None:
                    continue
                ag_fq_name_str = ep.get('address_group')
                if ag_fq_name_str:
                    ag_fq_name = ag_fq_name_str.split(':')
                    [ag_refs.append(ref) for ref in db_ag_refs
                     if ref['to'] == ag_fq_name]
            else:
                ep = obj_dict.get(ep_name)
                if ep is None:
                    continue
                ag_fq_name_str = ep.get('address_group')
                if ag_fq_name_str:
                    ag_fq_name = ag_fq_name_str.split(':')
                    try:
                        ag_uuid = cls.db_conn.fq_name_to_uuid('address_group',
                                                              ag_fq_name)
                    except cfgm_common.exceptions.NoIdError:
                        msg = ('No Address Group object found for %s' %
                               ag_fq_name_str)
                        return False, (404, msg)
                    ag_refs.append({'to': ag_fq_name, 'uuid': ag_uuid})

        if {r['uuid'] for r in ag_refs} != {r['uuid'] for r in db_ag_refs}:
            obj_dict['address_group_refs'] = ag_refs

        return True, ''

    @classmethod
    def _frs_fix_endpoint_tag(cls, obj_dict, db_obj_dict=None):
        draft_pm_name = constants.POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT
        obj_parent_type = obj_dict.get('parent_type')
        obj_fq_name = obj_dict.get('fq_name')
        tag_refs = []
        db_tag_refs = []
        if db_obj_dict:
            obj_fq_name = db_obj_dict['fq_name']
            obj_parent_type = db_obj_dict['parent_type']
            db_tag_refs = db_obj_dict.get('tag_refs', [])

        if (not is_internal_request() and 'tag_refs' in obj_dict and
                (db_obj_dict or obj_dict['tag_refs'])):
            msg = ("Cannot directly define Tags reference from a Firewall "
                   "Rule. Use 'tags' endpoints property in the Firewall Rule")
            return False, (400, msg)

        def _get_tag_fq_name(tag_name):
            # unless global, inherit project id from caller
            if "=" not in tag_name:
                return False, (404, "Invalid tag name '%s'" % tag_name)
            if tag_name[0:7] == 'global:':
                tag_fq_name = [tag_name[7:]]
            # Owned by a policy management, could be a global resource or a
            # global/scoped draft resource
            elif obj_parent_type == PolicyManagementServer.resource_type:
                # global: [default-pm:sec-res] => [tag-res]
                # draft global: [draft-pm:sec-res] => [tag-res]
                # draft scope: [domain:project:draft-pm:sec-res] =>
                #              [domain:project:tag-res]
                tag_fq_name = obj_fq_name[:-2] + [tag_name]
            # Project scoped resource, tag has to be in same scoped
            elif obj_parent_type == Project.resource_type:
                # scope: [domain:project:sec-res] =>
                #        [domain:project:tag-res]
                tag_fq_name = obj_fq_name[:-1] + [tag_name]
            else:
                msg = ("Firewall rule %s (%s) parent type '%s' is not "
                       "supported as security resource scope" %
                       (obj_parent_type, ':'.join(obj_fq_name),
                        obj_dict['uuid']))
                return False, (400, msg)

            return True, tag_fq_name

        for ep_name in ['endpoint_1', 'endpoint_2']:
            if (ep_name not in obj_dict and db_obj_dict and
                    ep_name in db_obj_dict):
                ep = db_obj_dict.get(ep_name)
                if ep is None:
                    continue
                for tag_name in set(ep.get('tags', [])):
                    ok, result = _get_tag_fq_name(tag_name)
                    if not ok:
                        return False, result
                    tag_fq_name = result
                    [tag_refs.append(ref) for ref in db_tag_refs
                     if ref['to'] == tag_fq_name]
            else:
                ep = obj_dict.get(ep_name)
                if ep is None:
                    continue
                ep['tag_ids'] = []
                for tag_name in set(ep.get('tags', [])):
                    ok, result = _get_tag_fq_name(tag_name)
                    if not ok:
                        return False, result
                    tag_fq_name = result
                    ok, result = TagServer.locate(
                        tag_fq_name, create_it=False, fields=['tag_id'])
                    if not ok:
                        return False, result
                    tag_dict = result
                    ep['tag_ids'].append(int(tag_dict['tag_id'], 0))
                    tag_refs.append(
                        {'to': tag_fq_name, 'uuid': tag_dict['uuid']})

        if {r['uuid'] for r in tag_refs} != {r['uuid'] for r in db_tag_refs}:
            obj_dict['tag_refs'] = tag_refs

        return True, ''

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls.check_draft_mode_state(obj_dict)
        if not ok:
            return False, result

        ok, result = cls.check_associated_firewall_resource_in_same_scope(
            obj_dict['uuid'],
            obj_dict['fq_name'],
            obj_dict,
            AddressGroup,
        )
        if not ok:
            return False, result

        ok, result = cls.check_associated_firewall_resource_in_same_scope(
            obj_dict['uuid'],
            obj_dict['fq_name'],
            obj_dict,
            ServiceGroupServer,
        )
        if not ok:
            return False, result

        ok, result = cls.check_associated_firewall_resource_in_same_scope(
            obj_dict['uuid'],
            obj_dict['fq_name'],
            obj_dict,
            VirtualNetworkServer,
        )
        if not ok:
            return False, result

        ok, result = cls._frs_fix_service(
            obj_dict.get('service'), obj_dict.get('service_group_refs'))
        if not ok:
            return False, result

        # create default match tag if use doesn't specifiy any explicitly
        if 'match_tags' not in obj_dict:
            obj_dict['match_tag_types'] = {
                'tag_type': constants.DEFAULT_MATCH_TAG_TYPE
            }

        # create protcol id
        ok, msg = cls._frs_fix_service_protocol(obj_dict)
        if not ok:
            return (ok, msg)

        # compile match-tags
        ok, msg = cls._frs_fix_match_tags(obj_dict)
        if not ok:
            return (ok, msg)

        ok, result = cls._check_endpoint(obj_dict)
        if not ok:
            return False, result

        ok, result = cls._frs_fix_endpoint_tag(obj_dict)
        if not ok:
            return False, result

        ok, result = cls._frs_fix_endpoint_address_group(obj_dict)
        if not ok:
            return False, result

        return True, ""
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, result = cls.check_draft_mode_state(obj_dict)
        if not ok:
            return False, result

        ok, result = cls.check_associated_firewall_resource_in_same_scope(
            id, fq_name, obj_dict, AddressGroup)
        if not ok:
            return False, result

        ok, result = cls.check_associated_firewall_resource_in_same_scope(
            id, fq_name, obj_dict, ServiceGroupServer)
        if not ok:
            return False, result

        ok, result = cls.check_associated_firewall_resource_in_same_scope(
            id, fq_name, obj_dict, VirtualNetworkServer)
        if not ok:
            return False, result

        ok, read_result = cls.dbe_read(db_conn, 'firewall_rule', id)
        if not ok:
            return ok, read_result
        db_obj_dict = read_result

        try:
            service = obj_dict['service']
        except KeyError:
            service = db_obj_dict.get('service')
        try:
            service_group_refs = obj_dict['service_group_refs']
        except KeyError:
            service_group_refs = db_obj_dict.get('service_group_refs')
        ok, result = cls._frs_fix_service(service, service_group_refs)
        if not ok:
            return False, result

        ok, msg = cls._frs_fix_service_protocol(obj_dict)
        if not ok:
            return (ok, msg)

        ok, msg = cls._frs_fix_match_tags(obj_dict)
        if not ok:
            return (ok, msg)

        ok, result = cls._check_endpoint(obj_dict)
        if not ok:
            return False, result

        ok, result = cls._frs_fix_endpoint_tag(obj_dict, db_obj_dict)
        if not ok:
            return False, result

        ok, result = cls._frs_fix_endpoint_address_group(obj_dict, db_obj_dict)
        if not ok:
            return False, result

        return True, ''


class FirewallPolicyServer(SecurityResourceBase, FirewallPolicy):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls.check_draft_mode_state(obj_dict)
        if not ok:
            return False, result

        return cls.check_associated_firewall_resource_in_same_scope(
            obj_dict['uuid'],
            obj_dict['fq_name'],
            obj_dict,
            FirewallRuleServer,
        )

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, result = cls.check_draft_mode_state(obj_dict)
        if not ok:
            return False, result

        return cls.check_associated_firewall_resource_in_same_scope(
            id, fq_name, obj_dict, FirewallRuleServer)


class ApplicationPolicySetServer(SecurityResourceBase, ApplicationPolicySet):
    @staticmethod
    def _check_all_applications_flag(obj_dict):
        # all_applications flag is read-only for user
        if not is_internal_request() and 'all_applications' in obj_dict:
            msg = "Application policy set 'all-applications' flag is read-only"
            return (False, (400, msg))
        return True, ""

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls.check_draft_mode_state(obj_dict)
        if not ok:
            return False, result

        ok, result = cls._check_all_applications_flag(obj_dict)
        if not ok:
            return False, result

        return cls.check_associated_firewall_resource_in_same_scope(
            obj_dict['uuid'],
            obj_dict['fq_name'],
            obj_dict,
            FirewallPolicyServer,
        )

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, result = cls.check_draft_mode_state(obj_dict)
        if not ok:
            return False, result

        ok, result = cls._check_all_applications_flag(obj_dict)
        if not ok:
            return False, result

        return cls.check_associated_firewall_resource_in_same_scope(
            id, fq_name, obj_dict, FirewallPolicyServer)

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        ok, result = cls.dbe_read(db_conn, 'application_policy_set', id)
        if not ok:
            return ok, result,None
        aps = result

        if aps.get('all_applications', False) and not is_internal_request():
            msg = ("Application Policy Set defined on all applications cannot "
                   "be deleted")
            return (False, (400, msg), None)

        return True, '',None


class VirtualRouterServer(Resource, VirtualRouter):

    @classmethod
    def _vr_to_pools(self, obj_dict):
        # given a VR return its allocation-pools in list

        ipam_refs = obj_dict.get('network_ipam_refs')
        if ipam_refs != None:
            pool_list = []
            for ref in ipam_refs:
                vr_ipam_data = ref['attr']
                vr_pools = vr_ipam_data.get('allocation_pools', [])
                pool_list.extend([(vr_pool['start'], vr_pool['end'])
                                 for vr_pool in vr_pools])
        else:
            pool_list = None

        return pool_list
    # end _vr_to_pools

    #check if any ip address from given alloc_pool sets is used in
    # in given virtual router, for instance_ip
    @classmethod
    def _check_vr_alloc_pool_delete(self, pool_set, vr_dict, db_conn):
        iip_refs = vr_dict.get('instance_ip_back_refs') or []
        if not iip_refs:
            return True, ""

        iip_uuid_list = [(iip_ref['uuid']) for iip_ref in iip_refs]
        (ok, iip_list, _) = db_conn.dbe_list('instance_ip',
                                obj_uuids=iip_uuid_list,
                                field_names=['instance_ip_address'])
        if not ok:
            return (ok, 400, 'Error in dbe_list: %s' % pformat(iip_list))

        for iip in iip_list:
            iip_addr = iip.get('instance_ip_address')
            if not iip_addr:
                self.config_log(
                    "Error in pool delete ip null: %s" %(iip['uuid']),
                    level=SandeshLevel.SYS_ERR)
                continue

            for alloc_pool in pool_set:
                if iip_addr in IPRange(alloc_pool[0], alloc_pool[1]):
                    return (False,
                        'Cannot Delete allocation pool, %s in use' % iip_addr)

        return True, ""
    # end _check_vr_alloc_pool_delete

    @classmethod
    def _vrouter_check_alloc_pool_delete(self, db_vr_dict, req_vr_dict,
                                         db_conn):
        if 'network_ipam_refs' not in req_vr_dict:
            # alloc_pools not modified in request
            return True, ""

        ipam_refs = req_vr_dict.get('network_ipam_refs')
        if not ipam_refs:
            iip_refs = db_vr_dict.get('instance_ip_back_refs')
            if iip_refs:
                return (False,
                        'Cannot Delete allocation pool,ip address in use')

        existing_vr_pools = self._vr_to_pools(db_vr_dict)
        if not existing_vr_pools:
            return True, ""

        requested_vr_pools = self._vr_to_pools(req_vr_dict)
        delete_set = set(existing_vr_pools) - set(requested_vr_pools)
        if not delete_set:
            return True, ""

        return self._check_vr_alloc_pool_delete(delete_set, db_vr_dict,
                                                db_conn)
    # end _vrouter_check_alloc_pool_delete

    @classmethod
    def _validate_vrouter_alloc_pools(cls, vrouter_dict, db_conn, ipam_refs):
        if not ipam_refs:
            return True, ''

        vrouter_uuid = vrouter_dict['uuid']
        ipam_uuid_list = []
        for ipam_ref in ipam_refs:
            if 'uuid' in ipam_ref:
                ipam_ref_uuid = ipam_ref.get('uuid')
            else:
                ipam_fq_name = ipam_ref['to']
                ipam_ref_uuid = db_conn.fq_name_to_uuid('network_ipam', ipam_fq_name)
            ipam_uuid_list.append(ipam_ref_uuid)
        (ok, ipam_list, _) = db_conn.dbe_list('network_ipam',
                                obj_uuids=ipam_uuid_list,
                                field_names=['ipam_subnet_method',
                                             'ipam_subnets',
                                             'virtual_router_back_refs'])
        if not ok:
            return (ok, 400, 'Error in dbe_list: %s' % pformat(ipam_list))

        for ipam_ref, ipam in zip(ipam_refs, ipam_list):
            subnet_method = ipam.get('ipam_subnet_method')
            if subnet_method != 'flat-subnet':
                return (False, "only flat-subnet ipam can be attached to vrouter")

            ipam_subnets = ipam.get('ipam_subnets', {})
            # read data on the link between vrouter and ipam
            # if alloc pool exists, then make sure that alloc-pools are
            # configured in ipam subnet with a flag indicating
            # vrouter specific allocation pool
            vr_ipam_data = ipam_ref['attr']
            vr_alloc_pools = vr_ipam_data.get('allocation_pools')
            if not vr_alloc_pools:
                return (False,
                        "No allocation-pools for this vrouter")

            for vr_alloc_pool in vr_alloc_pools:
                vr_alloc_pool.pop('vrouter_specific_pool', None)

            vr_subnets = vr_ipam_data.get('subnet', [])
            if vr_subnets:
                for vr_subnet in vr_subnets:
                    vr_sn_prefix = vr_subnet['ip_prefix']
                    vr_sn_prefix_len = vr_subnet['ip_prefix_len']
                    try:
                        prefix = IPAddress(vr_sn_prefix)
                    except netaddr.core.AddrFormatError:
                        return (False,
                                "vrouter subnet prefix is invalid")

            # get all allocation pools in this ipam
            subnets = ipam_subnets.get('subnets', [])
            ipam_alloc_pools = []
            for subnet in subnets:
                subnet_alloc_pools = subnet.get('allocation_pools', [])
                for subnet_alloc_pool in subnet_alloc_pools:
                    vr_flag = subnet_alloc_pool.get('vrouter_specific_pool')
                    if vr_flag:
                        ipam_alloc = {'start':subnet_alloc_pool['start'],
                                      'end':subnet_alloc_pool['end']}
                        ipam_alloc_pools.append(ipam_alloc)

            for vr_alloc_pool in vr_alloc_pools:
                if vr_alloc_pool not in ipam_alloc_pools:
                    return (False,
                            'vrouter allocation-pool start:%s, end:%s '
                            'not in ipam' %(vr_alloc_pool['start'],
                                            vr_alloc_pool['end']))

            if 'virtual_router_back_refs' not in ipam:
                continue

            vr_back_refs = ipam['virtual_router_back_refs']
            for vr_back_ref in vr_back_refs:
                if vr_back_ref['uuid'] == vrouter_uuid:
                    continue

                back_ref_ipam_data = vr_back_ref['attr']
                bref_alloc_pools = back_ref_ipam_data.get('allocation_pools')
                if not bref_alloc_pools:
                    continue

                for vr_alloc_pool in vr_alloc_pools:
                    if vr_alloc_pool in bref_alloc_pools:
                        return (False,
                                'vrouter allocation-pool start:%s, end:%s '
                                'is used in other vrouter:%s'
                                %(vr_alloc_pool['start'],
                                  vr_alloc_pool['end'],
                                  vr_back_ref['uuid']))

        return True, ''

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ipam_refs = obj_dict.get('network_ipam_refs') or []
        if not ipam_refs:
            return True, ''

        (ok, result) = cls._validate_vrouter_alloc_pools(obj_dict, db_conn,
                                                         ipam_refs)
        if not ok:
            return (False, (400, result))

        return True, ''

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):

        (ok, db_dict) = cls.dbe_read(db_conn, 'virtual_router', id)
        (ok, result) = cls._vrouter_check_alloc_pool_delete(db_dict, obj_dict,
                                                            db_conn)
        if not ok:
            return (False, (400, result))

        ipam_refs = obj_dict.get('network_ipam_refs')
        if ipam_refs:
                (ok, result) = cls._validate_vrouter_alloc_pools(obj_dict,
                                                                 db_conn,
                                                                 ipam_refs)
                if not ok:
                    return (False, (400, result))

        return True, ''
# end class VirtualRouterServer


class VirtualNetworkServer(Resource, VirtualNetwork):

    @classmethod
    def _check_is_provider_network_property(cls, obj_dict, db_conn,
                                            vn_ref=None):
        # no further checks if is_provider_network is not set
        if 'is_provider_network' not in obj_dict:
            return (True, '')
        if not vn_ref:
            # must be set as False for non provider VN
            if obj_dict.get('is_provider_network'):
                return (False,
                        'Non provider VN (%s) can not be '
                        'configured with is_provider_network = True' % (
                            obj_dict.get('uuid')))
        else:
            # compare obj_dict with db and fail
            # if not same as this is a read-only property
            if obj_dict.get('is_provider_network') != \
               vn_ref.get('is_provider_network', False):
                return (False,
                        'Update is_provider_network property of VN (%s) '
                        'is not allowed' % obj_dict.get('uuid'))
        return (True, '')
    # end _check_is_provider_network_property

    @classmethod
    def _check_provider_network(cls, obj_dict, db_conn, vn_ref=None):
        # no further checks if not linked
        # to a provider network
        if not obj_dict.get('virtual_network_refs'):
            return (True, '')
        # retrieve this VN
        if not vn_ref:
            vn_ref = {'uuid': obj_dict['uuid'],
                      'is_provider_network': obj_dict.get(
                          'is_provider_network')}
        uuids = []
        for vn in obj_dict.get('virtual_network_refs'):
            if 'uuid' not in vn:
                try:
                    uuids += [cls.db_conn.fq_name_to_uuid(cls.object_type,
                                                          vn['to'])]
                except cfgm_common.exceptions.NoIdError as e:
                    return (False, str(e))
            else:
                uuids += [vn['uuid']]

        # if not a provider_vn, not more
        # than one virtual_network_refs is allowed
        if not vn_ref.get('is_provider_network') and \
           len(obj_dict.get('virtual_network_refs')) != 1:
            return(False,
                   'Non Provider VN (%s) can connect to one provider VN but '
                   'trying to connect to VN (%s)' % (vn_ref['uuid'], uuids))

        # retrieve vn_refs of linked VNs
        (ok, provider_vns, _) = db_conn.dbe_list('virtual_network',
                                                 obj_uuids=uuids,
                                                 field_names=[
                                                     'is_provider_network'],
                                                 filters={
                                                     'is_provider_network': [
                                                         True]})
        if not ok:
            return (ok, 'Error reading VN refs (%s)' % uuids)
        if vn_ref.get('is_provider_network'):
            # this is a provider-VN, no linked provider_vns is expected
            if provider_vns:
                return (False,
                        'Provider VN (%s) can not connect to another '
                        'provider VN (%s)' % (vn_ref.get('uuid'), uuids))
        else:
            # this is a non-provider VN, only one provider vn is expected
            if len(provider_vns) != 1:
                return (False,
                        'Non Provider VN (%s) can connect only to one '
                        'provider VN but not (%s)' % (
                            vn_ref.get('uuid'), uuids))
        return (True, '')

    @classmethod
    def _check_route_targets(cls, obj_dict):
        rt_dict = obj_dict.get('route_target_list')
        if not rt_dict:
            return True, ''
        for rt in rt_dict.get('route_target') or []:
            ok, result = RouteTargetServer.is_user_defined(rt)
            if not ok:
                return False, result
            user_defined_rt = result
            if not user_defined_rt:
                 return (False, "Configured route target must use ASN that is "
                         "different from global ASN or route target value must"
                         " be less than %d" % cfgm_common.BGP_RTGT_MIN_ID)

        return (True, '')
    # end _check_route_targets

    @staticmethod
    def _check_vxlan_id(obj_dict):
        if ('virtual_network_properties' in obj_dict
                 and obj_dict['virtual_network_properties'] != None
                 and 'vxlan_network_identifier' in obj_dict['virtual_network_properties']):
            virtual_network_properties = obj_dict['virtual_network_properties']
            if virtual_network_properties['vxlan_network_identifier'] != None:
                return True, virtual_network_properties['vxlan_network_identifier']
            else:
                return True, None
        else:
            #return false only when the above fields are not found
            return False, None
    #end _check_vxlan_id

    @classmethod
    def _check_provider_details(cls, obj_dict, db_conn, create):

        properties = obj_dict.get('provider_properties')
        if not properties:
            return (True, '')

        if not create:
            ok, result = cls.dbe_read(db_conn, 'virtual_network',
                             obj_dict['uuid'],
                             obj_fields=['virtual_machine_interface_back_refs',
                                         'provider_properties'])
            if not ok:
                return ok, result

            old_properties = result.get('provider_properties')
            if 'virtual_machine_interface_back_refs' in result:
                if old_properties != properties:
                    return (False, "Provider values can not be changed when VMs are already using")

            if old_properties:
                if 'segmentation_id' not in properties:
                    properties['segmentation_id'] = old_properties.get('segmentation_id')

                if not properties.get('physical_network'):
                    properties['physical_network'] = old_properties.get('physical_network')

        if 'segmentation_id' not in properties:
            return (False, "Segmenation id must be configured")

        if not properties.get('physical_network'):
            return (False, "physical network must be configured")

        return (True, '')

    @classmethod
    def _is_multi_policy_service_chain_supported(cls, obj_dict, read_result=None):
        if not ('multi_policy_service_chains_enabled' in obj_dict or
                'route_target_list' in obj_dict or
                'import_route_target_list' in obj_dict or
                'export_route_target_list' in obj_dict):
            return (True, '')

        # Create Request
        if not read_result:
            read_result = {}

        result_obj_dict = copy.deepcopy(read_result)
        result_obj_dict.update(obj_dict)
        if result_obj_dict.get('multi_policy_service_chains_enabled'):
            import_export_targets = result_obj_dict.get('route_target_list') or {}
            import_targets = result_obj_dict.get('import_route_target_list') or {}
            export_targets = result_obj_dict.get('export_route_target_list') or  {}
            import_targets_set = set(import_targets.get('route_target') or [])
            export_targets_set = set(export_targets.get('route_target') or [])
            targets_in_both_import_and_export = \
                    import_targets_set.intersection(export_targets_set)
            if ((import_export_targets.get('route_target') or []) or
                    targets_in_both_import_and_export):
                msg = "Multi policy service chains are not supported, "
                msg += "with both import export external route targets"
                return (False, (409, msg))

        return (True, '')


    @classmethod
    def _check_net_mode_for_flat_ipam(cls, obj_dict, db_dict):
        net_mode = None
        vn_props = None
        if 'virtual_network_properties' in obj_dict:
            vn_props = obj_dict['virtual_network_properties']
        elif db_dict:
            vn_props = db_dict.get('virtual_network_properties')
        if vn_props:
           net_mode = vn_props.get('forwarding_mode')

        if net_mode != 'l3':
            return (False, "flat-subnet is allowed only with l3 network")
        else:
            return (True, "")

    @classmethod
    def _check_ipam_network_subnets(cls, obj_dict, db_conn, vn_uuid,
                                    db_dict=None):
        # if Network has subnets in network_ipam_refs, it should refer to
        # atleast one ipam with user-defined-subnet method. If network is
        # attached to all "flat-subnet", vn can not have any VnSubnetType cidrs

        if (('network_ipam_refs' not in obj_dict) and
           ('virtual_network_properties' in obj_dict)):
            # it is a network update without any changes in network_ipam_refs
            # but changes in virtual_network_properties
            # we need to read ipam_refs from db_dict and for any ipam if
            # if subnet_method is flat-subnet, network_mode should be l3
            if db_dict is None:
                return (True, 200, '')
            else:
                db_ipam_refs = db_dict.get('network_ipam_refs') or []

            ipam_with_flat_subnet = False
            ipam_uuid_list = [ipam['uuid'] for ipam in db_ipam_refs]
            if not ipam_uuid_list:
                return (True, 200, '')

            (ok, ipam_lists, _) = db_conn.dbe_list('network_ipam',
                                              obj_uuids=ipam_uuid_list,
                                              field_names=['ipam_subnet_method'])
            if not ok:
                return (ok, 500, 'Error in dbe_list: %s' % pformat(ipam_lists))
            for ipam in ipam_lists:
                if 'ipam_subnet_method' in ipam:
                    subnet_method = ipam['ipam_subnet_method']
                    ipam_with_flat_subnet = True
                    break

            if ipam_with_flat_subnet:
                (ok, result) = cls._check_net_mode_for_flat_ipam(obj_dict,
                                                                 db_dict)
                if not ok:
                    return (ok, 400, result)

        # validate ipam_refs in obj_dict either update or remove
        ipam_refs = obj_dict.get('network_ipam_refs') or []
        ipam_subnets_list = []
        ipam_with_flat_subnet = False
        for ipam in ipam_refs:
            ipam_fq_name = ipam['to']
            ipam_uuid = ipam.get('uuid')
            if not ipam_uuid:
                ipam_uuid = db_conn.fq_name_to_uuid('network_ipam',
                                                    ipam_fq_name)

            (ok, ipam_dict) = db_conn.dbe_read(obj_type='network_ipam',
                                               obj_id=ipam_uuid,
                                               obj_fields=['ipam_subnet_method',
                                                           'ipam_subnets'])
            if not ok:
                return (ok, 400, ipam_dict)

            subnet_method = ipam_dict.get('ipam_subnet_method')
            if subnet_method is None:
                subnet_method = 'user-defined-subnet'

            if subnet_method == 'flat-subnet':
                ipam_with_flat_subnet = True
                subnets_list = cls.addr_mgmt._ipam_to_subnets(ipam_dict)
                if not subnets_list:
                    subnets_list = []
                ipam_subnets_list = ipam_subnets_list + subnets_list

            # get user-defined-subnet information to validate
            # that there is cidr configured if subnet_method is 'flat-subnet'
            vnsn = ipam['attr']
            ipam_subnets = vnsn['ipam_subnets']
            if (subnet_method == 'flat-subnet'):
                for ipam_subnet in ipam_subnets:
                    subnet_dict = ipam_subnet.get('subnet')
                    if subnet_dict:
                        ip_prefix = subnet_dict.get('ip_prefix')
                        if ip_prefix is not None:
                            return (False, 400,
                                "with flat-subnet, network can not have user-defined subnet")

            if subnet_method == 'user-defined-subnet':
                (ok, result) = cls.addr_mgmt.net_check_subnet(ipam_subnets)
                if not ok:
                    return (ok, 409, result)

            if (db_conn.update_subnet_uuid(ipam_subnets)):
                db_conn.config_log(
                    'AddrMgmt: subnet uuid is updated for vn %s'
                        % (vn_uuid), level=SandeshLevel.SYS_DEBUG)
        # end of ipam in ipam_refs

        if ipam_with_flat_subnet:
            (ok, result) = cls._check_net_mode_for_flat_ipam(obj_dict,
                                                             db_dict)
            if not ok:
                return (ok, 400, result)

        (ok, result) = cls.addr_mgmt.net_check_subnet_quota(db_dict, obj_dict,
                                                            db_conn)

        if not ok:
            return (ok, vnc_quota.QUOTA_OVER_ERROR_CODE, result)

        vn_subnets_list = cls.addr_mgmt._vn_to_subnets(obj_dict)
        if not vn_subnets_list:
            vn_subnets_list = []
        (ok, result) = cls.addr_mgmt.net_check_subnet_overlap(vn_subnets_list,
                                                              ipam_subnets_list)
        if not ok:
            return (ok, 400, result)

        return (True, 200, '')
    # end _check_ipam_network_subnets

    @classmethod
    def _get_fabric_snat(cls, uuid, obj_dict, db_conn):
        ok, result = cls.dbe_read(db_conn, 'virtual_network',
                         uuid,
                         obj_fields=['fabric_snat'])
        if not ok:
            return False

        if 'fabric_snat' in result:
            return result['fabric_snat']

        return False

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        (ok, response) = cls._is_multi_policy_service_chain_supported(obj_dict)
        if not ok:
            return (ok, response)

        is_shared = obj_dict.get('is_shared')
        # neutorn <-> vnc sharing
        if obj_dict['perms2']['global_access'] == PERMS_RWX:
            obj_dict['is_shared'] = True
        elif is_shared:
            obj_dict['perms2']['global_access'] = PERMS_RWX

        # Does not authorize to set the virtual network ID as it's allocated
        # by the vnc server
        if obj_dict.get('virtual_network_network_id') is not None:
            return (False, (403, "Cannot set the virtual network ID"))

        #Allocate vxlan_id if it's present in request.
        vxlan_id = None
        (ok, vxlan_id) = cls._check_vxlan_id(obj_dict)
        if vxlan_id != None:
            try:
                vxlan_fq_name = ':'.join(obj_dict['fq_name']) + '_vxlan'
                cls.vnc_zk_client.alloc_vxlan_id(
                          vxlan_fq_name,
                          int(vxlan_id))
            except cfgm_common.exceptions.ResourceExistsError as e:
                msg = 'Cannot set VXLAN_ID: %s, it has already been set' % vxlan_id
                return False, (400, msg)

            def undo_vxlan_id():
                cls.vnc_zk_client.free_vxlan_id(
                    int(vxlan_id),
                    vxlan_fq_name)
                return True, ""
            get_context().push_undo(undo_vxlan_id)

        # Allocate virtual network ID
        vn_id = cls.vnc_zk_client.alloc_vn_id(
            ':'.join(obj_dict['fq_name']))
        def undo_vn_id():
            cls.vnc_zk_client.free_vn_id(
                vn_id, ':'.join(obj_dict['fq_name']))
            return True, ""
        get_context().push_undo(undo_vn_id)
        obj_dict['virtual_network_network_id'] = vn_id

        vn_uuid = obj_dict.get('uuid')
        (ok, return_code, result) = cls._check_ipam_network_subnets(obj_dict,
                                                                    db_conn,
                                                                    vn_uuid)
        if not ok:
            return (ok, (return_code, result))

        (ok, error) =  cls._check_route_targets(obj_dict)
        if not ok:
            return (False, (400, error))

        (ok, error) = cls._check_provider_details(obj_dict, db_conn, True)
        if not ok:
            return (False, (400, error))

        (ok, error) = cls._check_is_provider_network_property(obj_dict,
            db_conn)
        if not ok:
            return (False, (400, error))

        (ok, error) = cls._check_provider_network(obj_dict, db_conn)
        if not ok:
            return (False, (400, error))

        # Check if network forwarding mode support BGP VPN types
        ok, result = BgpvpnServer.check_network_supports_vpn_type(
            db_conn, obj_dict)
        if not ok:
            return ok, result

        # Check if we can reference the BGP VPNs
        ok, result = BgpvpnServer.check_network_has_bgpvpn_assoc_via_router(
            db_conn, obj_dict)
        if not ok:
            return ok, result

        ipam_refs = obj_dict.get('network_ipam_refs') or []
        try:
            cls.addr_mgmt.net_create_req(obj_dict)
            #for all ipams which are flat, we need to write a unique id as
            # subnet uuid for all cidrs in flat-ipam
            for ipam in ipam_refs:
                ipam_fq_name = ipam['to']
                ipam_uuid = ipam.get('uuid')
                if not ipam_uuid:
                    ipam_uuid = db_conn.fq_name_to_uuid('network_ipam',
                                                        ipam_fq_name)
                (ok, ipam_dict) = db_conn.dbe_read(obj_type='network_ipam',
                                                   obj_id=ipam_uuid)
                if not ok:
                    return (ok, (400, ipam_dict))

                subnet_method = ipam_dict.get('ipam_subnet_method')
                if (subnet_method != None and
                    subnet_method == 'flat-subnet'):
                    subnet_dict = {}
                    flat_subnet_uuid = str(uuid.uuid4())
                    subnet_dict['subnet_uuid'] = flat_subnet_uuid
                    ipam['attr']['ipam_subnets'] = [subnet_dict]

            def undo():
                cls.addr_mgmt.net_delete_req(obj_dict)
                return True, ""
            get_context().push_undo(undo)
        except Exception as e:
            return (False, (500, str(e)))

        return True, ""
    # end pre_dbe_create

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        api_server = db_conn.get_api_server()

        # Create native/vn-default routing instance
        ri_fq_name = obj_dict['fq_name'][:]
        ri_fq_name.append(obj_dict['fq_name'][-1])
        ri_obj = RoutingInstance(
            parent_type='virtual-network', fq_name=ri_fq_name,
            routing_instance_is_default=True)
        fabric_snat = cls._get_fabric_snat(obj_dict['uuid'], obj_dict, db_conn)
        ri_obj.set_routing_instance_fabric_snat(fabric_snat)
        api_server.internal_request_create(
            'routing-instance',
            ri_obj.serialize_to_json())

        return True, ''
    # end post_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        if ((fq_name == cfgm_common.LINK_LOCAL_VN_FQ_NAME)):
            return True,  ""

        # neutron <-> vnc sharing
        try:
            global_access =  obj_dict['perms2']['global_access']
        except KeyError:
            global_access = None
        is_shared = obj_dict.get('is_shared')
        if global_access is not None or is_shared is not None:
            if global_access is not None and is_shared is not None:
                if is_shared != (global_access != 0):
                    error = "Inconsistent is_shared (%s a) and global_access (%s)" % (is_shared, global_access)
                    return (False, (400, error))
            elif global_access is not None:
                obj_dict['is_shared'] = (global_access != 0)
            else:
                ok, result = cls.dbe_read(db_conn, 'virtual_network', id, obj_fields=['perms2'])
                if not ok:
                    return ok, result
                obj_dict['perms2'] = result['perms2']
                obj_dict['perms2']['global_access'] = PERMS_RWX if is_shared else 0

        (ok, error) =  cls._check_route_targets(obj_dict)
        if not ok:
            return (False, (400, error))

        (ok, error) = cls._check_provider_details(obj_dict, db_conn, False)
        if not ok:
            return (False, (409, error))

        ok, read_result = cls.dbe_read(db_conn, 'virtual_network', id)
        if not ok:
            return ok, read_result

        new_vn_id = obj_dict.get('virtual_network_network_id')
        # Does not authorize to update the virtual network ID as it's allocated
        # by the vnc server
        if (new_vn_id is not None and
                new_vn_id != read_result.get('virtual_network_network_id')):
            return (False, (403, "Cannot update the virtual network ID"))

        new_vxlan_id = None
        old_vxlan_id = None
        deallocated_vxlan_network_identifier = None

        (new_vxlan_status, new_vxlan_id) = cls._check_vxlan_id(obj_dict)
        (old_vxlan_status, old_vxlan_id) = cls._check_vxlan_id(read_result)

        if(new_vxlan_status and new_vxlan_id != old_vxlan_id ):

            vxlan_fq_name = ':'.join(fq_name) + '_vxlan'
            if(new_vxlan_id != None):
                #First, check if the new_vxlan_id being updated exist for some other VN.
                new_vxlan_fq_name_in_db = cls.vnc_zk_client.get_vn_from_id(int(new_vxlan_id))
                if(new_vxlan_fq_name_in_db != None):
                    if(new_vxlan_fq_name_in_db != vxlan_fq_name):
                        msg = 'Cannot set VXLAN_ID: %s, it has already been set' % (new_vxlan_id)
                        return (False, (400, msg))

                #Second, set the new_vxlan_id in Zookeeper.
                cls.vnc_zk_client.alloc_vxlan_id(
                  vxlan_fq_name,
                  int(new_vxlan_id))

                def undo_alloc():
                    cls.vnc_zk_client.free_vxlan_id(
                       int(old_vxlan_id), vxlan_fq_name)
                get_context().push_undo(undo_alloc)

            #Third, check if old_vxlan_id is not None, if so, delete it from Zookeeper
            if(old_vxlan_id != None):
                cls.vnc_zk_client.free_vxlan_id(
                    int(old_vxlan_id),
                    vxlan_fq_name)
                #Add old vxlan_network_identifier to handle dbe_update_notification
                deallocated_vxlan_network_identifier = old_vxlan_id
                def undo_free():
                    cls.vnc_zk_client.alloc_vxlan_id(
                       vxlan_fq_name, int(old_vxlan_id))
                get_context().push_undo(undo_free)


        (ok, error) = cls._check_is_provider_network_property(obj_dict,
            db_conn, vn_ref=read_result)
        if not ok:
            return (False, (400, error))

        (ok, error) = cls._check_provider_network(obj_dict,
            db_conn, vn_ref=read_result)
        if not ok:
            return (False, (400, error))

        (ok, response) = cls._is_multi_policy_service_chain_supported(obj_dict,
                                                                      read_result)
        if not ok:
            return (ok, response)

        (ok, return_code, result) = cls._check_ipam_network_subnets(obj_dict,
                                                                    db_conn,
                                                                    id,
                                                                    read_result)
        if not ok:
            return (ok, (return_code, result))

        (ok, result) = cls.addr_mgmt.net_check_subnet_delete(read_result,
                                                             obj_dict)
        if not ok:
            return (ok, (409, result))

        ipam_refs = obj_dict.get('network_ipam_refs') or []
        if ipam_refs:
            (ok, result) = cls.addr_mgmt.net_validate_subnet_update(read_result,
                                                                    obj_dict)
            if not ok:
                return (ok, (400, result))

        # Check if network forwarding mode support BGP VPN types
        ok, result = BgpvpnServer.check_network_supports_vpn_type(
            db_conn, obj_dict, read_result)
        if not ok:
            return ok, result

        # Check if we can reference the BGP VPNs
        ok, result = BgpvpnServer.check_network_has_bgpvpn_assoc_via_router(
            db_conn, obj_dict, read_result)
        if not ok:
            return ok, result

        try:
            cls.addr_mgmt.net_update_req(fq_name, read_result, obj_dict, id)
            #update link with a subnet_uuid if ipam in read_result or obj_dict
            # does not have it already
            for ipam in ipam_refs:
                ipam_fq_name = ipam['to']
                ipam_uuid = db_conn.fq_name_to_uuid('network_ipam',
                                                    ipam_fq_name)
                (ok, ipam_dict) = db_conn.dbe_read(obj_type='network_ipam',
                                                   obj_id=ipam_uuid)
                if not ok:
                    return (ok, (409, ipam_dict))

                subnet_method = ipam_dict.get('ipam_subnet_method')
                if (subnet_method != None and
                    subnet_method == 'flat-subnet'):
                    vnsn_data = ipam.get('attr') or {}
                    ipam_subnets = vnsn_data.get('ipam_subnets') or []
                    if (len(ipam_subnets) == 1):
                        continue

                    if (len(ipam_subnets) == 0):
                        subnet_dict = {}
                        flat_subnet_uuid = str(uuid.uuid4())
                        subnet_dict['subnet_uuid'] = flat_subnet_uuid
                        ipam['attr']['ipam_subnets'].insert(0, subnet_dict)
            def undo():
                # failed => update with flipped values for db_dict and req_dict
                cls.addr_mgmt.net_update_req(fq_name, obj_dict, read_result, id)
            # end undo
            get_context().push_undo(undo)
        except Exception as e:
            return (False, (500, str(e)))

        return True, {
            'deallocated_vxlan_network_identifier': deallocated_vxlan_network_identifier,
        }
    # end pre_dbe_update

    @classmethod
    def post_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        api_server = db_conn.get_api_server()

        # Create native/vn-default routing instance
        ri_fq_name = fq_name[:]
        ri_fq_name.append(fq_name[-1])
        ri_uuid = db_conn.fq_name_to_uuid('routing_instance', ri_fq_name)
        ri_obj = RoutingInstance(
            parent_type='virtual-network', fq_name=ri_fq_name,
            routing_instance_is_default=True)
        fabric_snat = cls._get_fabric_snat(id, obj_dict, db_conn)
        ri_obj.set_routing_instance_fabric_snat(fabric_snat)
        api_server.internal_request_update(
            'routing-instance',
            ri_uuid,
            ri_obj.serialize_to_json())

        return True, ''
    # end post_dbe_create

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        cls.addr_mgmt.net_delete_req(obj_dict)
        if obj_dict['id_perms'].get('user_visible', True) is not False:
            ok, result = QuotaHelper.get_project_dict_for_quota(
                obj_dict['parent_uuid'], db_conn)
            if not ok:
                return False, result,None
            proj_dict = result
            ok, (subnet_count, counter) = cls.addr_mgmt.get_subnet_quota_counter(
                    obj_dict, proj_dict)
            if subnet_count:
                counter -= subnet_count
            def undo_subnet():
                 ok, (subnet_count, counter) = cls.addr_mgmt.get_subnet_quota_counter(
                         obj_dict, proj_dict)
                 if subnet_count:
                     counter += subnet_count
            get_context().push_undo(undo_subnet)

        def undo():
            cls.addr_mgmt.net_create_req(obj_dict)
        get_context().push_undo(undo)
        return True, "", None
    # end pre_dbe_delete

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn, **kwargs):
        api_server = db_conn.get_api_server()

        # Delete native/vn-default routing instance
        # For this find backrefs and remove their ref to RI
        ri_fq_name = obj_dict['fq_name'][:]
        ri_fq_name.append(obj_dict['fq_name'][-1])
        ri_uuid = db_conn.fq_name_to_uuid('routing_instance', ri_fq_name)

        backref_fields = RoutingInstance.backref_fields
        children_fields = RoutingInstance.children_fields
        ok, result = cls.dbe_read(db_conn, 'routing_instance', ri_uuid,
                                  obj_fields=backref_fields|children_fields)
        if not ok:
            return ok, result

        ri_obj_dict = result
        backref_field_types = RoutingInstance.backref_field_types
        for backref_name in backref_fields:
            backref_res_type = backref_field_types[backref_name][0]
            def drop_ref(obj_uuid):
                # drop ref from ref_uuid to ri_uuid
                api_server.internal_request_ref_update(
                    backref_res_type, obj_uuid, 'DELETE',
                    'routing-instance', ri_uuid)
            # end drop_ref
            for backref in ri_obj_dict.get(backref_name) or []:
                drop_ref(backref['uuid'])

        children_field_types = RoutingInstance.children_field_types
        for child_name in children_fields:
            child_res_type = children_field_types[child_name][0]
            for child in ri_obj_dict.get(child_name) or []:
                try:
                    api_server.internal_request_delete(child_res_type, child['uuid'])
                except cfgm_common.exceptions.HttpError as e:
                    if e.status_code == 404:
                        pass
                    else:
                        raise
                except cfgm_common.exceptions.NoIdError as e:
                    pass
        api_server.internal_request_delete('routing-instance', ri_uuid)

        # Deallocate the virtual network ID
        cls.vnc_zk_client.free_vn_id(
            obj_dict.get('virtual_network_network_id'),
            ':'.join(obj_dict['fq_name']),
        )

        (ok, vxlan_id) = cls._check_vxlan_id(obj_dict)
        if vxlan_id != None:
            vxlan_fq_name = ':'.join(obj_dict['fq_name']) + '_vxlan'
            cls.vnc_zk_client.free_vxlan_id(
                  int(vxlan_id),
                  vxlan_fq_name)

        return True, ""
    # end post_dbe_delete


    @classmethod
    def ip_alloc(cls, vn_fq_name, subnet_uuid=None, count=1, family=None):
        if family:
            ip_version = 6 if family == 'v6' else 4
        else:
            ip_version = None

        ip_list = [cls.addr_mgmt.ip_alloc_req(vn_fq_name, sub=subnet_uuid,
                                              asked_ip_version=ip_version,
                                              alloc_id=str(uuid.uuid4()))[0]
                   for i in range(count)]
        msg = 'AddrMgmt: reserve %d IP for vn=%s, subnet=%s - %s' \
            % (count, vn_fq_name, subnet_uuid or '', ip_list)
        cls.addr_mgmt.config_log(msg, level=SandeshLevel.SYS_DEBUG)
        return {'ip_addr': ip_list}
    # end ip_alloc

    @classmethod
    def ip_free(cls, vn_fq_name, ip_list):
        msg = 'AddrMgmt: release IP %s for vn=%s' % (ip_list, vn_fq_name)
        cls.addr_mgmt.config_log(msg, level=SandeshLevel.SYS_DEBUG)
        for ip_addr in ip_list:
            cls.addr_mgmt.ip_free_req(ip_addr, vn_fq_name)
    # end ip_free

    @classmethod
    def subnet_ip_count(cls, vn_fq_name, subnet_list):
        ip_count_list = []
        for item in subnet_list:
            ip_count_list.append(cls.addr_mgmt.ip_count_req(vn_fq_name, item))
        return {'ip_count_list': ip_count_list}
    # end subnet_ip_count

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        cls.vnc_zk_client.alloc_vn_id(
            ':'.join(obj_dict['fq_name']),
            obj_dict['virtual_network_network_id'],
        )
        vxlan_id = None
        (ok, vxlan_id) = cls._check_vxlan_id(obj_dict)
        if vxlan_id != None:
            try:
                vxlan_fq_name = ':'.join(obj_dict['fq_name']) + '_vxlan'
                cls.vnc_zk_client.alloc_vxlan_id(
                          vxlan_fq_name,
                          int(vxlan_id),
                          notify=True)
            except cfgm_common.exceptions.ResourceExistsError as e:
                msg = 'Cannot set VXLAN_ID: %s, it has already been set' % vxlan_id
                #Don't return false here.
        cls.addr_mgmt.net_create_notify(obj_id, obj_dict)

        return True, ''
    # end dbe_create_notification

    @classmethod
    def dbe_update_notification(cls, obj_id, extra_dict=None):
        ok, read_result = cls.dbe_read(cls.db_conn, 'virtual_network', obj_id)
        if not ok:
            return ok, read_result

        obj_dict = read_result
        new_vxlan_id = None
        old_vxlan_id = None

        (new_vxlan_status, new_vxlan_id) = cls._check_vxlan_id(obj_dict)

        if extra_dict is not None:
            old_vxlan_id = extra_dict.get('deallocated_vxlan_network_identifier')

        if(new_vxlan_status and new_vxlan_id != old_vxlan_id):

            vxlan_fq_name = ':'.join(obj_dict['fq_name']) + '_vxlan'
            if(new_vxlan_id != None):
                #First, check if the new_vxlan_id being updated exist for some other VN.
                new_vxlan_fq_name_in_db = cls.vnc_zk_client.get_vn_from_id(int(new_vxlan_id))
                if(new_vxlan_fq_name_in_db != None):
                    if(new_vxlan_fq_name_in_db != vxlan_fq_name):
                        msg = 'Cannot set VXLAN_ID: %s, it has already been set' % (new_vxlan_id)
                        return

                #Second, set the new_vxlan_id in Zookeeper.
                cls.vnc_zk_client.alloc_vxlan_id(
                      vxlan_fq_name,
                      int(new_vxlan_id),
                      notify=True)

            #Third, check if old_vxlan_id is not None, if so, delete it from Zookeeper
            if(old_vxlan_id != None):
                cls.vnc_zk_client.free_vxlan_id(
                    int(old_vxlan_id),
                    vxlan_fq_name,
                    notify=True)


        return cls.addr_mgmt.net_update_notify(obj_id)
    # end dbe_update_notification

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        cls.vnc_zk_client.free_vn_id(
            obj_dict.get('virtual_network_network_id'),
            ':'.join(obj_dict['fq_name']),
            notify=True,
        )

        vxlan_id = None
        (ok, vxlan_id) = cls._check_vxlan_id(obj_dict)
        if vxlan_id != None:
            vxlan_fq_name = ':'.join(obj_dict['fq_name']) + '_vxlan'
            cls.vnc_zk_client.free_vxlan_id(
                  int(vxlan_id),
                  vxlan_fq_name,
                  notify=True )

        cls.addr_mgmt.net_delete_notify(obj_id, obj_dict)

        return True, ''
    # end dbe_delete_notification

# end class VirtualNetworkServer


class NetworkIpamServer(Resource, NetworkIpam):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):

        subnet_method = obj_dict.get('ipam_subnet_method', 'user-defined-subnet')
        ipam_subnets = obj_dict.get('ipam_subnets')
        if ((ipam_subnets != None) and (subnet_method != 'flat-subnet')):
            return (False, (400, 'ipam-subnets are allowed only with flat-subnet'))

        ipam_subnetting = obj_dict.get('ipam_subnetting', False)
        if (ipam_subnetting and (subnet_method != 'flat-subnet')):
            return (False, (400, 'subnetting is allowed only with flat-subnet'))

        if  (subnet_method != 'flat-subnet'):
            return True, ""


        ipam_subnets = obj_dict.get('ipam_subnets')
        if ipam_subnets is None:
            return True, ""

        ipam_subnets_list = cls.addr_mgmt._ipam_to_subnets(obj_dict)
        if not ipam_subnets_list:
            ipam_subnets_list = []

        (ok, result) = cls.addr_mgmt.net_check_subnet_overlap(ipam_subnets_list)
        if not ok:
            return (ok, (400, result))

        subnets = ipam_subnets.get('subnets') or []
        (ok, result) = cls.addr_mgmt.net_check_subnet(subnets)
        if not ok:
            return (ok, (409, result))

        try:
            cls.addr_mgmt.ipam_create_req(obj_dict)
            def undo():
                cls.addr_mgmt.ipam_delete_req(obj_dict)
                return True, ""
            get_context().push_undo(undo)
        except Exception as e:
            return (False, (500, str(e)))

        return True, ""
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, read_result = cls.dbe_read(db_conn, 'network_ipam', id)
        if not ok:
            return ok, read_result
        def ipam_mgmt_check():
            old_ipam_mgmt = read_result.get('network_ipam_mgmt')
            new_ipam_mgmt = obj_dict.get('network_ipam_mgmt')
            if not old_ipam_mgmt or not new_ipam_mgmt:
                return True, ""

            old_dns_method = old_ipam_mgmt.get('ipam_dns_method')
            new_dns_method = new_ipam_mgmt.get('ipam_dns_method')
            if not cls.is_change_allowed(old_dns_method, new_dns_method,
                                         read_result, db_conn):
                return (False, (400, "Cannot change DNS Method " +
                        " with active VMs referring to the IPAM"))
            return True, ""
        # end ipam_mgmt_check

        ok, result = ipam_mgmt_check()
        if not ok:
            return ok, result

        old_subnet_method = read_result.get('ipam_subnet_method')
        if 'ipam_subnet_method' in obj_dict:
            new_subnet_method = obj_dict.get('ipam_subnet_method')
            if (old_subnet_method != new_subnet_method):
                return (False, (400, 'ipam_subnet_method can not be changed'))

        if (old_subnet_method != 'flat-subnet'):
            if 'ipam_subnets' in obj_dict:
                return (False,
                        (400, 'ipam-subnets are allowed only with flat-subnet'))
            return True, ""

        old_subnetting = read_result.get('ipam_subnetting')
        if 'ipam_subnetting' in obj_dict:
            subnetting = obj_dict.get('ipam_subnetting', False)
            if (old_subnetting != subnetting):
                return (False, (400, 'ipam_subnetting can not be changed'))

        if 'ipam_subnets' in obj_dict:
            req_subnets_list = cls.addr_mgmt._ipam_to_subnets(obj_dict)

            #First check the overlap condition within ipam_subnets
            (ok, result) = cls.addr_mgmt.net_check_subnet_overlap(
                               req_subnets_list)
            if not ok:
                return (ok, (400, result))

            #if subnets are modified then make sure new subnet lists are
            #not in overlap conditions with VNs subnets and other ipams
            #referred by all VNs referring this ipam
            vn_refs = read_result.get('virtual_network_back_refs', [])
            ref_ipam_uuid_list = []
            refs_subnets_list = []
            for ref in vn_refs:
                vn_id = ref.get('uuid')
                try:
                    (ok, vn_dict) = db_conn.dbe_read('virtual_network', vn_id)
                except cfgm_common.exceptions.NoIdError:
                    continue
                if not ok:
                    self.config_log("Error in reading vn: %s" %(vn_dict),
                                    level=SandeshLevel.SYS_ERR)
                    return (ok, 409, vn_dict)
                #get existing subnets on this VN and on other ipams
                #this VN refers and run a overlap check.
                ipam_refs = vn_dict.get('network_ipam_refs', [])
                for ipam in ipam_refs:
                    ref_ipam_uuid = ipam['uuid']
                    if ref_ipam_uuid == id:
                        #This is a ipam for which update request has come
                        continue

                    if ref_ipam_uuid in ref_ipam_uuid_list:
                        continue

                    #check if ipam is a flat-subnet, for flat-subnet ipam
                    # add uuid in ref_ipam_uuid_list, to read ipam later
                    # to get current ipam_subnets from ipam
                    vnsn_data = ipam.get('attr') or {}
                    ref_ipam_subnets = vnsn_data.get('ipam_subnets') or []

                    if len(ref_ipam_subnets) == 1:
                        #flat subnet ipam will have only one entry in
                        #vn->ipam link without any ip_prefix
                        ref_ipam_subnet = ref_ipam_subnets[0]
                        ref_subnet = ref_ipam_subnet.get('subnet') or {}
                        if 'ip_prefix' not in ref_subnet:
                            #This is a flat-subnet,
                            ref_ipam_uuid_list.append(ref_ipam_uuid)

                #vn->ipam link to the refs_subnets_list
                vn_subnets_list = cls.addr_mgmt._vn_to_subnets(vn_dict)
                if vn_subnets_list:
                    refs_subnets_list += vn_subnets_list
            #for each vn

            for ipam_uuid in ref_ipam_uuid_list:
                (ok, ipam_dict) = cls.dbe_read(db_conn, 'network_ipam',
                                               ipam_uuid)
                if not ok:
                    return (ok, 409, ipam_dict)
                ref_subnets_list = cls.addr_mgmt._ipam_to_subnets(ipam_dict)
                refs_subnets_list += ref_subnets_list

            (ok, result) = cls.addr_mgmt.check_overlap_with_refs(
                                   refs_subnets_list, req_subnets_list)
            if not ok:
                return (ok, (400, result))
        #if ipam_subnets changed in the update

        ipam_subnets = obj_dict.get('ipam_subnets')
        if ipam_subnets != None:
            subnets = ipam_subnets.get('subnets') or []
            (ok, result) = cls.addr_mgmt.net_check_subnet(subnets)
            if not ok:
                return (ok, (409, result))

        (ok, result) = cls.addr_mgmt.ipam_check_subnet_delete(read_result,
                                                              obj_dict)
        if not ok:
            return (ok, (409, result))

        (ok, result) = cls.addr_mgmt.ipam_validate_subnet_update(read_result,
                                                                 obj_dict)
        if not ok:
            return (ok, (400, result))

        try:
            cls.addr_mgmt.ipam_update_req(fq_name, read_result, obj_dict, id)
            def undo():
                # failed => update with flipped values for db_dict and req_dict
                cls.addr_mgmt.ipam_update_req(fq_name, obj_dict, read_result, id)
            # end undo
            get_context().push_undo(undo)
        except Exception as e:
            return (False, (500, str(e)))

        return True, ""
    # end pre_dbe_update

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        ok, read_result = cls.dbe_read(db_conn, 'network_ipam', id)
        if not ok:
            return ok, read_result, None

        subnet_method = read_result.get('ipam_subnet_method')
        if subnet_method is None or subnet_method != 'flat-subnet':
            return True, "", None

        ipam_subnets = read_result.get('ipam_subnets')
        if ipam_subnets is None:
            return True, "", None

        cls.addr_mgmt.ipam_delete_req(obj_dict)
        def undo():
            cls.addr_mgmt.ipam_create_req(obj_dict)
        get_context().push_undo(undo)
        return True, "", None
    # end pre_dbe_delete

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        cls.addr_mgmt.ipam_create_notify(obj_dict)

        return True, ''
    # end dbe_create_notification

    @classmethod
    def dbe_update_notification(cls, obj_id, extra_dict=None):
        return cls.addr_mgmt.ipam_update_notify(obj_id)
    # end dbe_update_notification

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        cls.addr_mgmt.ipam_delete_notify(obj_id, obj_dict)

        return True, ''
    # end dbe_delete_notification

    @classmethod
    def is_change_allowed(cls, old, new, obj_dict, db_conn):
        active_vm_present = cls.is_active_vm_present(obj_dict, db_conn)
        if active_vm_present:
            if (old == "default-dns-server" or old == "virtual-dns-server"):
                if (new is "tenant-dns-server" or new is None):
                    return False
            if (old is "tenant-dns-server" and new != old):
                return False
            if (old is None and new != old):
                return False
        return True
    # end is_change_allowed

    @classmethod
    def is_active_vm_present(cls, obj_dict, db_conn):
        for vn in obj_dict.get('virtual_network_back_refs') or []:
            ok, result = cls.dbe_read(db_conn, 'virtual_network', vn['uuid'],
                            obj_fields=['virtual_machine_interface_back_refs'])
            if not ok:
                code, msg = result
                if code == 404:
                    continue
                db_conn.config_log('Error in active vm check %s'
                                   %(result),
                                   level=SandeshLevel.SYS_ERR)
                # Cannot determine, err on side of caution
                return True
            if result.get('virtual_machine_interface_back_refs'):
                return True
        return False
    # end is_active_vm_present

# end class NetworkIpamServer

class DomainServer(Resource, Domain):

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        # enable domain level sharing for domain template
        share_item = {
            'tenant': 'domain:%s' % obj_dict.get('uuid'),
            'tenant_access': cfgm_common.DOMAIN_SHARING_PERMS
        }
        obj_dict['perms2']['share'].append(share_item)
        return (True, "")
    # end pre_dbe_create


class ServiceTemplateServer(Resource, ServiceTemplate):
    generate_default_instance = False

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        # enable domain level sharing for service template
        domain_uuid = obj_dict.get('parent_uuid')
        if domain_uuid is None:
            domain_uuid = db_conn.fq_name_to_uuid('domain', obj_dict['fq_name'][0:1])
        share_item = {
            'tenant': 'domain:%s' % domain_uuid,
            'tenant_access': PERMS_RX
        }
        obj_dict['perms2']['share'].append(share_item)
        return (True, "")
    # end pre_dbe_create

class VirtualDnsServer(Resource, VirtualDns):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        # enable domain level sharing for virtual DNS
        domain_uuid = obj_dict.get('parent_uuid')
        if domain_uuid is None:
            domain_uuid = db_conn.fq_name_to_uuid('domain', obj_dict['fq_name'][0:1])
        share_item = {
            'tenant': 'domain:%s' % domain_uuid,
            'tenant_access': PERMS_RX
        }
        obj_dict['perms2']['share'].append(share_item)
        return cls.validate_dns_server(obj_dict, db_conn)
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls.validate_dns_server(obj_dict, db_conn)
    # end pre_dbe_update

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        vdns_name = ":".join(obj_dict['fq_name'])
        if 'parent_uuid' in obj_dict:
            ok, read_result = cls.dbe_read(db_conn, 'domain',
                                           obj_dict['parent_uuid'])
            if not ok:
                return ok, read_result, None
            virtual_DNSs = read_result.get('virtual_DNSs') or []
            for vdns in virtual_DNSs:
                vdns_uuid = vdns['uuid']
                vdns_id = {'uuid': vdns_uuid}
                ok, read_result = cls.dbe_read(db_conn, 'virtual_DNS',
                                               vdns['uuid'])
                if not ok:
                    code, msg = read_result
                    if code == 404:
                        continue
                    return ok, (code, msg), None

                vdns_data = read_result['virtual_DNS_data']
                if 'next_virtual_DNS' in vdns_data:
                    if vdns_data['next_virtual_DNS'] == vdns_name:
                        return (
                            False,
                            (403,
                             "Virtual DNS server is referred"
                             " by other virtual DNS servers"), None)
        return True, "", None
    # end pre_dbe_delete

    @classmethod
    def is_valid_dns_name(cls, name):
        if len(name) > 255:
            return False
        if name.endswith("."):  # A single trailing dot is legal
            # strip exactly one dot from the right, if present
            name = name[:-1]
        disallowed = re.compile("[^A-Z\d-]", re.IGNORECASE)
        return all(  # Split by labels and verify individually
            (label and len(label) <= 63  # length is within proper range
             # no bordering hyphens
             and not label.startswith("-") and not label.endswith("-")
             and not disallowed.search(label))  # contains only legal char
            for label in name.split("."))
    # end is_valid_dns_name

    @classmethod
    def is_valid_ipv4_address(cls, address):
        parts = address.split(".")
        if len(parts) != 4:
            return False
        for item in parts:
            try:
                if not 0 <= int(item) <= 255:
                    return False
            except ValueError:
                return False
        return True
    # end is_valid_ipv4_address

    @classmethod
    def is_valid_ipv6_address(cls, address):
        try:
            socket.inet_pton(socket.AF_INET6, address)
        except socket.error:
            return False
        return True
    # end is_valid_ipv6_address

    @classmethod
    def validate_dns_server(cls, obj_dict, db_conn):
        if 'fq_name' in obj_dict:
            virtual_dns = obj_dict['fq_name'][1]
            disallowed = re.compile("[^A-Z\d-]", re.IGNORECASE)
            if disallowed.search(virtual_dns) or virtual_dns.startswith("-"):
                return (False, (403,
                        "Special characters are not allowed in " +
                        "Virtual DNS server name"))

        vdns_data = obj_dict['virtual_DNS_data']
        if not cls.is_valid_dns_name(vdns_data['domain_name']):
            return (
                False,
                (403, "Domain name does not adhere to DNS name requirements"))

        record_order = ["fixed", "random", "round-robin"]
        if not str(vdns_data['record_order']).lower() in record_order:
            return (False, (403, "Invalid value for record order"))

        ttl = vdns_data['default_ttl_seconds']
        if ttl < 0 or ttl > 2147483647:
            return (False, (400, "Invalid value for TTL"))

        if 'next_virtual_DNS' in vdns_data:
            vdns_next = vdns_data['next_virtual_DNS']
            if not vdns_next or vdns_next is None:
                return True, ""
            next_vdns = vdns_data['next_virtual_DNS'].split(":")
            # check that next vdns exists
            try:
                next_vdns_uuid = db_conn.fq_name_to_uuid(
                    'virtual_DNS', next_vdns)
            except Exception as e:
                if not cls.is_valid_ipv4_address(
                        vdns_data['next_virtual_DNS']):
                    return (
                        False,
                        (400,
                         "Invalid Virtual Forwarder(next virtual dns server)"))
                else:
                    return True, ""
            # check that next virtual dns servers arent referring to each other
            # above check doesnt allow during create, but entry could be
            # modified later
            ok, read_result = cls.dbe_read(db_conn, 'virtual_DNS',
                                           next_vdns_uuid)
            if ok:
                next_vdns_data = read_result['virtual_DNS_data']
                if 'next_virtual_DNS' in next_vdns_data:
                    vdns_name = ":".join(obj_dict['fq_name'])
                    if next_vdns_data['next_virtual_DNS'] == vdns_name:
                        return (
                            False,
                            (403,
                             "Cannot have Virtual DNS Servers "
                             "referring to each other"))
        return True, ""
    # end validate_dns_server
# end class VirtualDnsServer


class VirtualDnsRecordServer(Resource, VirtualDnsRecord):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls.validate_dns_record(obj_dict, db_conn)
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls.validate_dns_record(obj_dict, db_conn)
    # end pre_dbe_update

    @classmethod
    def validate_dns_record(cls, obj_dict, db_conn):
        rec_data = obj_dict['virtual_DNS_record_data']
        rec_types = ["a", "cname", "ptr", "ns", "mx", "aaaa"]
        rec_type = str(rec_data['record_type']).lower()
        if not rec_type in rec_types:
            return (False, (403, "Invalid record type"))
        if str(rec_data['record_class']).lower() != "in":
            return (False, (403, "Invalid record class"))

        rec_name = rec_data['record_name']
        rec_value = rec_data['record_data']

        # check rec_name validity
        if rec_type == "ptr":
            if (not VirtualDnsServer.is_valid_ipv4_address(rec_name) and
                    not "in-addr.arpa" in rec_name.lower()):
                return (
                    False,
                    (403,
                     "PTR Record name has to be IP address"
                     " or reverse.ip.in-addr.arpa"))
        elif not VirtualDnsServer.is_valid_dns_name(rec_name):
            return (
                False,
                (403, "Record name does not adhere to DNS name requirements"))

        # check rec_data validity
        if rec_type == "a":
            if not VirtualDnsServer.is_valid_ipv4_address(rec_value):
                return (False, (403, "Invalid IP address"))
        elif rec_type == "aaaa":
            if not VirtualDnsServer.is_valid_ipv6_address(rec_value):
                return (False, (403, "Invalid IPv6 address"))
        elif rec_type == "cname" or rec_type == "ptr" or rec_type == "mx":
            if not VirtualDnsServer.is_valid_dns_name(rec_value):
                return (
                    False,
                    (403,
                     "Record data does not adhere to DNS name requirements"))
        elif rec_type == "ns":
            try:
                vdns_name = rec_value.split(":")
                vdns_uuid = db_conn.fq_name_to_uuid('virtual_DNS', vdns_name)
            except Exception as e:
                if (not VirtualDnsServer.is_valid_ipv4_address(rec_value) and
                        not VirtualDnsServer.is_valid_dns_name(rec_value)):
                    return (
                        False,
                        (403, "Invalid virtual dns server in record data"))

        ttl = rec_data['record_ttl_seconds']
        if ttl < 0 or ttl > 2147483647:
            return (False, (403, "Invalid value for TTL"))

        if rec_type == "mx":
            preference = rec_data['record_mx_preference']
            if preference < 0 or preference > 65535:
                return (False, (403, "Invalid value for MX record preference"))

        return True, ""
    # end validate_dns_record
# end class VirtualDnsRecordServer

def _check_policy_rules(entries, network_policy_rule=False):
    if not entries:
        return True, ""
    rules = entries.get('policy_rule') or []
    ignore_keys = ['rule_uuid', 'created', 'last_modified']
    rules_no_uuid = [dict((k, v) for k, v in r.items() if k not in ignore_keys)
                     for r in rules]
    for index, rule in enumerate(rules_no_uuid):
        rules_no_uuid[index] = None
        if rule in rules_no_uuid:
            try:
                rule_uuid = rules[index]['rule_uuid']
            except KeyError:
                rule_uuid = None
            return (False, (409, 'Rule already exists : %s' % rule_uuid))
    for rule in rules:
        if not rule.get('rule_uuid'):
            rule['rule_uuid'] = str(uuid.uuid4())
        protocol = rule['protocol']
        if protocol.isdigit():
            if int(protocol) < 0 or int(protocol) > 255:
                return (False, (400, 'Rule with invalid protocol : %s' %
                                protocol))
        else:
            valids = ['any', 'icmp', 'tcp', 'udp', 'icmp6']
            if protocol not in valids:
                return (False, (400, 'Rule with invalid protocol : %s' %
                                protocol))
        src_sg_list = [addr.get('security_group') for addr in
                  rule.get('src_addresses') or []]
        dst_sg_list = [addr.get('security_group') for addr in
                  rule.get('dst_addresses') or []]

        if network_policy_rule:
            if rule.get('action_list') is None:
                return (False, (400, 'Action is required'))

            src_sg = [True for sg in src_sg_list if sg != None]
            dst_sg = [True for sg in dst_sg_list if sg != None]
            if True in src_sg or True in dst_sg:
                return (False, (400, 'Config Error: policy rule refering to'
                                      ' security group is not allowed'))
        else:
            ethertype = rule.get('ethertype')
            if ethertype is not None:
                for addr in itertools.chain(rule.get('src_addresses') or [],
                                            rule.get('dst_addresses') or []):
                    if addr.get('subnet') is not None:
                        ip_prefix = addr["subnet"].get('ip_prefix')
                        ip_prefix_len = addr["subnet"].get('ip_prefix_len')
                        network = IPNetwork("%s/%s" % (ip_prefix, ip_prefix_len))
                        if not ethertype == "IPv%s" % network.version:
                            return (False, (400, "Rule subnet %s doesn't match ethertype %s" %
                                            (network, ethertype)))

            if ('local' not in src_sg_list and 'local' not in dst_sg_list):
                return (False, (400, "At least one of source or destination"
                                     " addresses must be 'local'"))
    return True, ""
# end _check_policy_rules

class SecurityGroupServer(Resource, SecurityGroup):
    get_nested_key_as_list = classmethod(lambda cls, x, y, z: (x.get(y).get(z)
                                 if (type(x) is dict and
                                    x.get(y) and x.get(y).get(z)) else []))

    @classmethod
    def _set_configured_security_group_id(cls, obj_dict):
        fq_name_str = ':'.join(obj_dict['fq_name'])
        configured_sg_id = obj_dict.get('configured_security_group_id') or 0
        sg_id = obj_dict.get('security_group_id')
        if sg_id is not None:
            sg_id = int(sg_id)

        if configured_sg_id > 0:
            if sg_id is not None:
                cls.vnc_zk_client.free_sg_id(sg_id, fq_name_str)
                def undo_dealloacte_sg_id():
                    # In case of error try to re-allocate the same ID as it was
                    # not yet freed on other node
                    new_sg_id = cls.vnc_zk_client.alloc_sg_id(fq_name_str,
                                                              sg_id)
                    if new_sg_id != sg_id:
                        cls.vnc_zk_client.alloc_sg_id(fq_name_str)
                        cls.server.internal_request_update(
                            cls.resource_type,
                            obj_dict['uuid'],
                            {'security_group_id': new_sg_id},
                        )
                    return True, ""
                get_context().push_undo(undo_dealloacte_sg_id)
            obj_dict['security_group_id'] = configured_sg_id
        else:
            if (sg_id is not None and
                    fq_name_str == cls.vnc_zk_client.get_sg_from_id(sg_id)):
                obj_dict['security_group_id'] = sg_id
            else:
                sg_id_allocated = cls.vnc_zk_client.alloc_sg_id(fq_name_str)
                def undo_allocate_sg_id():
                    cls.vnc_zk_client.free_sg_id(sg_id_allocated, fq_name_str)
                    return True, ""
                get_context().push_undo(undo_allocate_sg_id)
                obj_dict['security_group_id'] = sg_id_allocated

        return True, ''

    @classmethod
    def check_security_group_rule_quota(
            cls, proj_dict, db_conn, rule_count):
        quota_counter = cls.server.quota_counter
        obj_type = 'security_group_rule'
        quota_limit = QuotaHelper.get_quota_limit(proj_dict, obj_type)

        if (rule_count and quota_limit >= 0):
            path_prefix = _DEFAULT_ZK_COUNTER_PATH_PREFIX + proj_dict['uuid']
            path = path_prefix + "/" + obj_type
            if not quota_counter.get(path):
                # Init quota counter for security group rule
                QuotaHelper._zk_quota_counter_init(
                        path_prefix, {obj_type : quota_limit},
                        proj_dict['uuid'], db_conn, quota_counter)
            ok, result = QuotaHelper.verify_quota(
                obj_type, quota_limit, quota_counter[path],
                count=rule_count)
            if not ok:
                return (False,
                        (vnc_quota.QUOTA_OVER_ERROR_CODE,
                         'security_group_entries: ' + str(quota_limit)))
            def undo():
                # Revert back quota count
                quota_counter[path] -= rule_count
            get_context().push_undo(undo)

        return True, ""

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):

        ok, response = _check_policy_rules(
            obj_dict.get('security_group_entries'))
        if not ok:
            return (ok, response)

        # Does not authorize to set the security group ID as it's allocated
        # by the vnc server
        if obj_dict.get('security_group_id') is not None:
            return (False, (403, "Cannot set the security group ID"))

        if obj_dict['id_perms'].get('user_visible', True):
            ok, result = QuotaHelper.get_project_dict_for_quota(
                obj_dict['parent_uuid'], db_conn)
            if not ok:
                return False, result
            proj_dict = result

            rule_count = len(cls.get_nested_key_as_list(obj_dict,
                             'security_group_entries', 'policy_rule'))
            ok, result = cls.check_security_group_rule_quota(
                    proj_dict, db_conn, rule_count)
            if not ok:
                return ok, result

        # Allocate security group ID if necessary
        return cls._set_configured_security_group_id(obj_dict)
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        deallocated_security_group_id = None
        ok, result = cls.dbe_read(db_conn, 'security_group', id)
        if not ok:
            return ok, result
        sg_dict = result

        # Does not authorize to update the security group ID as it's allocated
        # by the vnc server
        new_sg_id = obj_dict.get('security_group_id')
        if (new_sg_id is not None and
                int(new_sg_id) != sg_dict['security_group_id']):
            return (False, (403, "Cannot update the security group ID"))

        # Update the configured security group ID
        if 'configured_security_group_id' in obj_dict:
            actual_sg_id = sg_dict['security_group_id']
            sg_dict['configured_security_group_id'] =\
                obj_dict['configured_security_group_id']
            ok, result = cls._set_configured_security_group_id(sg_dict)
            if not ok:
                return ok, result
            if actual_sg_id != sg_dict['security_group_id']:
                deallocated_security_group_id = actual_sg_id
            obj_dict['security_group_id'] = sg_dict['security_group_id']

        ok, result = _check_policy_rules(
                obj_dict.get('security_group_entries'))
        if not ok:
            return ok, result

        if sg_dict['id_perms'].get('user_visible', True):
            ok, result = QuotaHelper.get_project_dict_for_quota(
                sg_dict['parent_uuid'], db_conn)
            if not ok:
                return False, result
            proj_dict = result
            new_rule_count = len(cls.get_nested_key_as_list(obj_dict,
                                 'security_group_entries', 'policy_rule'))
            existing_rule_count = len(cls.get_nested_key_as_list(sg_dict,
                                 'security_group_entries', 'policy_rule'))
            rule_count = (new_rule_count - existing_rule_count)
            ok, result = cls.check_security_group_rule_quota(
                    proj_dict, db_conn, rule_count)
            if not ok:
                return ok, result

        return True, {
            'deallocated_security_group_id': deallocated_security_group_id,
        }
    # end pre_dbe_update

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        ok, result = cls.dbe_read(db_conn, 'security_group', id)
        if not ok:
            return ok, result, None
        sg_dict = result

        if sg_dict['id_perms'].get('user_visible', True) is not False:
            ok, result = QuotaHelper.get_project_dict_for_quota(
                sg_dict['parent_uuid'], db_conn)
            if not ok:
                return False, result, None
            proj_dict = result
            obj_type = 'security_group_rule'
            quota_limit = QuotaHelper.get_quota_limit(proj_dict, obj_type)

            if ('security_group_entries' in obj_dict and quota_limit >= 0):
                rule_count = len(obj_dict['security_group_entries']['policy_rule'])
                path_prefix = _DEFAULT_ZK_COUNTER_PATH_PREFIX + proj_dict['uuid']
                path = path_prefix + "/" + obj_type
                quota_counter = cls.server.quota_counter
                # If the SG has been created before R3, there is no
                # path in ZK. It is created on next update and we
                # can ignore it for now
                if quota_counter.get(path):
                    quota_counter[path] -= rule_count

                    def undo():
                        # Revert back quota count
                        quota_counter[path] += rule_count
                    get_context().push_undo(undo)

        return True, "", None
    # end pre_dbe_delete

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn, **kwargs):
        # Deallocate the security group ID
        cls.vnc_zk_client.free_sg_id(
            obj_dict.get('security_group_id'), ':'.join(obj_dict['fq_name']))
        return True, ""
    # end post_dbe_delete

    @classmethod
    def _notify_sg_id_modified(cls, obj_dict,
                               deallocated_security_group_id=None):
        fq_name_str = ':'.join(obj_dict['fq_name'])
        sg_id = obj_dict.get('security_group_id')

        if deallocated_security_group_id is not None:
            cls.vnc_zk_client.free_sg_id(deallocated_security_group_id,
                                         fq_name_str, notify=True)
        if sg_id is not None:
            cls.vnc_zk_client.alloc_sg_id(fq_name_str, sg_id)

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        cls._notify_sg_id_modified(obj_dict)

        return True, ''

    @classmethod
    def dbe_update_notification(cls, obj_id, extra_dict=None):
        ok, result = cls.dbe_read(cls.db_conn, cls.object_type, obj_id,
                                  obj_fields=['fq_name', 'security_group_id'])
        if not ok:
            return False, result
        obj_dict = result

        if extra_dict is not None:
            cls._notify_sg_id_modified(
                obj_dict, extra_dict.get('deallocated_security_group_id'))
        else:
            cls._notify_sg_id_modified(obj_dict)

        return True, ''

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        cls.vnc_zk_client.free_sg_id(
            obj_dict.get('security_group_id'),
            ':'.join(obj_dict['fq_name']),
            notify=True,
        )

        return True, ''
# end class SecurityGroupServer


class NetworkPolicyServer(Resource, NetworkPolicy):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return _check_policy_rules(obj_dict.get('network_policy_entries'), True)
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, result = cls.dbe_read(db_conn, 'network_policy', id)
        if not ok:
            return ok, result

        return _check_policy_rules(obj_dict.get('network_policy_entries'), True)
    # end pre_dbe_update

# end class NetworkPolicyServer


class LogicalInterfaceServer(Resource, LogicalInterface):

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        (ok, result) = cls._check_vlan(obj_dict, db_conn)
        if not ok:
            return (ok, result)

        vlan = 0
        if 'logical_interface_vlan_tag' in obj_dict:
            vlan = obj_dict['logical_interface_vlan_tag']

        ok, result = PhysicalInterfaceServer._check_interface_name(obj_dict,
                                                                   db_conn,
                                                                   vlan)
        if not ok:
            return ok, result

        ok, result = cls._check_esi(obj_dict, db_conn, vlan,
                                    obj_dict.get('parent_type'),
                                    obj_dict.get('parent_uuid'))
        if not ok:
            return ok, result

        return (True, '')
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        (ok, result) = cls._check_vlan(obj_dict, db_conn)
        if not ok:
            return (ok, result)

        ok, read_result = cls.dbe_read(db_conn, 'logical_interface', id)
        if not ok:
            return ok, read_result

        # do not allow change in display name
        if 'display_name' in obj_dict:
            if obj_dict['display_name'] != read_result.get('display_name'):
                return (False, (403, "Cannot change display name !"))

        vlan = None
        if 'logical_interface_vlan_tag' in obj_dict:
            vlan = obj_dict['logical_interface_vlan_tag']
            if 'logical_interface_vlan_tag' in read_result:
                if int(vlan) != int(read_result.get('logical_interface_vlan_tag')):
                    return (False, (403, "Cannot change Vlan id"))

        if vlan == None:
            vlan = read_result.get('logical_interface_vlan_tag')

        obj_dict['display_name'] = read_result.get('display_name')
        obj_dict['fq_name'] = read_result['fq_name']
        obj_dict['parent_type'] = read_result['parent_type']
        if 'logical_interface_type' not in obj_dict:
            existing_li_type = read_result.get('logical_interface_type')
            if existing_li_type:
                obj_dict['logical_interface_type'] = existing_li_type
        ok, result = PhysicalInterfaceServer._check_interface_name(obj_dict,
                                                                   db_conn,
                                                                   vlan)
        if not ok:
            return ok, result

        ok, result = cls._check_esi(obj_dict, db_conn,
                                    read_result.get('logical_interface_vlan_tag'),
                                    read_result.get('parent_type'),
                                    read_result.get('parent_uuid'))
        if not ok:
            return ok, result

        return True, ""
    # end pre_dbe_update

    @classmethod
    def _check_esi(cls, obj_dict, db_conn, vlan, p_type, p_uuid):
        vmis_ref = obj_dict.get('virtual_machine_interface_refs')
        # Created Logical Interface does not point to a VMI.
        # Nothing to validate.
        if not vmis_ref:
            return (True, '')

        vmis = {x.get('uuid') for x in vmis_ref}
        if p_type == 'physical-interface':
            ok, result = cls.dbe_read(db_conn,
                                      'physical_interface', p_uuid)
            if not ok:
                return ok, result
            esi = result.get('ethernet_segment_identifier')
            if esi:
                filters = {'ethernet_segment_identifier' : [esi]}
                obj_fields = [u'logical_interfaces']
                ok, result, _ = db_conn.dbe_list(obj_type='physical_interface',
                                                 filters=filters,
                                                 field_names=obj_fields)
                if not ok:
                    return ok, result

                for pi in result:
                    for li in pi.get('logical_interfaces') or []:
                        if li.get('uuid') == obj_dict.get('uuid'):
                            continue
                        ok, li_obj = cls.dbe_read(db_conn,
                                                  'logical_interface',
                                                  li.get('uuid'))
                        if not ok:
                            return ok, li_obj

                        # If the LI belongs to a different VLAN than the one created,
                        # then no-op.
                        li_vlan = li_obj.get('logical_interface_vlan_tag')
                        if vlan != li_vlan:
                            continue

                        peer_li_vmis = {x.get('uuid')
                                        for x in li_obj.get('virtual_machine_interface_refs') or []}
                        if peer_li_vmis != vmis:
                            return (False, (403, "LI should refer to the same set " +
                                                 "of VMIs as peer LIs belonging to the same ESI"))
        return (True, "")

    @classmethod
    def _check_vlan(cls, obj_dict, db_conn):
        if 'logical_interface_vlan_tag' in obj_dict:
            vlan = obj_dict['logical_interface_vlan_tag']
            if vlan < 0 or vlan > 4094:
                return (False, (403, "Invalid Vlan id"))
        return True, ""
    # end _check_vlan

# end class LogicalInterfaceServer

class RouteTableServer(Resource, RouteTable):

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls._check_prefixes(obj_dict)
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls._check_prefixes(obj_dict)
    # end pre_dbe_update

    @classmethod
    def _check_prefixes(cls, obj_dict):
        routes = obj_dict.get('routes') or {}
        in_routes = routes.get("route") or []
        in_prefixes = [r.get('prefix') for r in in_routes]
        in_prefixes_set = set(in_prefixes)
        if len(in_prefixes) != len(in_prefixes_set):
            return (False, (400, 'duplicate prefixes not '
                                      'allowed: %s' % obj_dict.get('uuid')))

        return (True, "")
    # end _check_prefixes

# end class RouteTableServer

class PhysicalInterfaceServer(Resource, PhysicalInterface):

    @classmethod
    def _check_esi_string(cls, esi):
        res = re.match(r'^([0-9A-Fa-f]{2}[:]){9}[0-9A-Fa-f]{2}', esi)
        if not res:
            return (False, (400, "Invalid ESI string format"))
        return (True, '')

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls._check_interface_name(obj_dict, db_conn, None)
        if not ok:
            return ok, result

        esi = obj_dict.get('ethernet_segment_identifier')
        if esi:
            ok, result = cls._check_esi_string(esi)
            if not ok:
                return ok, result

        return (True, '')
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, read_result = cls.dbe_read(db_conn, 'physical_interface', id,
                                       obj_fields=['display_name',
                                                   'logical_interfaces'])
        if not ok:
            return ok, read_result

        # do not allow change in display name
        if 'display_name' in obj_dict:
            if obj_dict['display_name'] != read_result.get('display_name'):
                return (False, (403, "Cannot change display name !"))

        esi = obj_dict.get('ethernet_segment_identifier')
        if esi and read_result.get('logical_interfaces'):
            ok, result = cls._check_esi_string(esi)
            if not ok:
                return ok, result

            ok, result = cls._check_esi(obj_dict, db_conn, esi,
                                        read_result.get('logical_interfaces'))
            if not ok:
                return ok, result
        return True, ""
    # end pre_dbe_update

    @classmethod
    def _check_esi(cls, obj_dict, db_conn, esi, li_refs):
        # Collecting a set of VMIs associated with LIs
        # associated to a PI.
        vlan_vmis = {}
        for li in li_refs:
            ok, li_obj = cls.dbe_read(db_conn,
                                  'logical_interface',
                                  li.get('uuid'))
            if not ok:
                return ok, li_obj

            vmi_refs = li_obj.get('virtual_machine_interface_refs')
            if vmi_refs:
                vlan_tag = li_obj.get('logical_interface_vlan_tag')
                vlan_vmis[vlan_tag] = {x.get('uuid') for x in vmi_refs}

        filters = {'ethernet_segment_identifier' : [esi]}
        obj_fields = [u'logical_interfaces']
        ok, result, _ = db_conn.dbe_list(obj_type='physical_interface',
                                         filters=filters,
                                         field_names=obj_fields)
        if not ok:
            return ok, result
        for pi in result:
            for li in pi.get('logical_interfaces') or []:
                ok, li_obj = cls.dbe_read(db_conn,
                                          'logical_interface',
                                          li.get('uuid'))
                if not ok:
                    return ok, li_obj

                vlan_to_check = li_obj.get('logical_interface_vlan_tag')
                # Ignore LI's with no VMI association
                if not li_obj.get('virtual_machine_interface_refs'):
                    continue

                vmis_to_check = {x.get('uuid')
                                 for x in li_obj.get('virtual_machine_interface_refs')}
                if vlan_vmis.get(vlan_to_check) != vmis_to_check:
                    return (False, (403, 'LI associated with the PI should have the' +
                                         ' same VMIs as LIs (associated with the PIs)' +
                                         ' of the same ESI family'))
        return (True, '')

    @classmethod
    def _check_interface_name(cls, obj_dict, db_conn, vlan_tag):

        interface_name = obj_dict['display_name']
        router = obj_dict['fq_name'][:2]
        try:
            router_uuid = db_conn.fq_name_to_uuid('physical_router', router)
        except cfgm_common.exceptions.NoIdError:
            return (False, (500, 'Internal error : Physical router ' +
                                 ":".join(router) + ' not found'))
        physical_interface_uuid = ""
        if obj_dict['parent_type'] == 'physical-interface':
            try:
                physical_interface_name = obj_dict['fq_name'][:3]
                physical_interface_uuid = db_conn.fq_name_to_uuid('physical_interface', physical_interface_name)
            except cfgm_common.exceptions.NoIdError:
                return (False, (500, 'Internal error : Physical interface ' +
                                     ":".join(physical_interface_name) + ' not found'))

        ok, result = cls.dbe_read(db_conn, 'physical_router', router_uuid,
                                  obj_fields=['physical_interfaces',
                                              'physical_router_product_name'])
        if not ok:
            return ok, result

        physical_router = result
        # In case of QFX, check that VLANs 1, 2 and 4094 are not used
        product_name = physical_router.get('physical_router_product_name') or ""
        if product_name.lower().startswith("qfx") and vlan_tag != None:
            li_type = obj_dict.get('logical_interface_type', '').lower()
            if li_type =='l2' and vlan_tag in constants.RESERVED_QFX_L2_VLAN_TAGS:
                return (False, (400, "Vlan ids " + str(constants.RESERVED_QFX_L2_VLAN_TAGS) +
                                " are not allowed on QFX"
                                " logical interface type: " + li_type))
        for physical_interface in physical_router.get('physical_interfaces') or []:
            # Read only the display name of the physical interface
            (ok, interface_object) = cls.dbe_read(db_conn,
                                                  'physical_interface',
                                                  physical_interface['uuid'],
                                                  obj_fields=['display_name'])
            if not ok:
                code, msg = interface_object
                if code == 404:
                    continue
                return ok, (code, msg)

            if 'display_name' in interface_object:
                if interface_name == interface_object['display_name']:
                    return (False, (403, "Display name already used in another interface :" +
                                         physical_interface['uuid']))

            # Need to check vlan only when request is for logical interfaces and
            # When the current physical_interface is the parent
            if vlan_tag is None or \
               physical_interface['uuid'] != physical_interface_uuid:
                continue

            # Read the logical interfaces in the physical interface.
            # This isnt read in the earlier DB read to avoid reading them for
            # all interfaces.

            obj_fields = [u'logical_interface_vlan_tag']
            (ok, result, _) = db_conn.dbe_list('logical_interface',
                    [physical_interface['uuid']], field_names=obj_fields)
            if not ok:
                return (False, (500, 'Internal error : Read logical interface list for ' +
                                     physical_interface['uuid'] + ' failed'))
            for li_object in result:
                # check vlan tags on the same physical interface
                if 'logical_interface_vlan_tag' in li_object:
                    if vlan_tag == int(li_object['logical_interface_vlan_tag']):
                        if li_object['uuid'] != obj_dict['uuid']:
                            return (False, (403, "Vlan tag  " + str(vlan_tag) +
                                            " already used in another "
                                            "interface : " + li_object['uuid']))

        return True, ""
    # end _check_interface_name

# end class PhysicalInterfaceServer


class ProjectServer(Resource, Project):
    @classmethod
    def _ensure_default_application_policy_set(cls, project_uuid,
                                               project_fq_name):
        default_name = 'default-%s' % ApplicationPolicySetServer.resource_type
        attrs = {
            'parent_type': cls.object_type,
            'parent_uuid': project_uuid,
            'name': default_name,
            'display_name': default_name,
            'all_applications': True,
        }
        ok, result = ApplicationPolicySetServer.locate(
            project_fq_name + [default_name], **attrs)
        if not ok:
            return False, result
        default_aps = result

        return cls.server.internal_request_ref_update(
            cls.resource_type,
            project_uuid,
            'ADD',
            ApplicationPolicySetServer.resource_type,
            default_aps['uuid'],
        )

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls._ensure_default_application_policy_set(
            obj_dict['uuid'], obj_dict['fq_name'])
        if not ok:
            return False, result

        SecurityResourceBase.server = cls.server
        return SecurityResourceBase.set_policy_management_for_security_draft(
            cls.resource_type, obj_dict)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        if ('vxlan_routing' not in obj_dict and
                'enable_security_policy_draft' not in obj_dict):
            return True, ''

        fields = ['vxlan_routing', 'logical_routers',
                  'enable_security_policy_draft']
        ok, result = cls.dbe_read(db_conn, cls.object_type, id,
                                  obj_fields=fields)
        if not ok:
            return ok, result
        db_obj_dict = result

        if 'vxlan_routing' in obj_dict:
            # VxLAN routing can be enabled or disabled
            # only when the project does not have any
            # Logical routers already attached.
            if (db_obj_dict.get('vxlan_routing') != obj_dict['vxlan_routing']
                    and 'logical_routers' in db_obj_dict):
                return (False, (400, 'VxLAN Routing update cannot be ' +
                                'done when Logical Routers are configured'))

        if 'enable_security_policy_draft' in obj_dict:
            obj_dict['fq_name'] = db_obj_dict['fq_name']
            obj_dict['uuid'] = db_obj_dict['uuid']
            SecurityResourceBase.server = cls.server
            ok, result = SecurityResourceBase.\
                set_policy_management_for_security_draft(
                    cls.resource_type,
                    obj_dict,
                    draft_mode_enabled=
                        db_obj_dict.get('enable_security_policy_draft', False),
                )
            if not ok:
                return False, result

        return True, ""

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        draft_pm_uuid = None
        draft_pm_name = constants.POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT
        draft_pm_fq_name = obj_dict['fq_name'] + [draft_pm_name]
        try:
            draft_pm_uuid = db_conn.fq_name_to_uuid(
                    PolicyManagementServer.object_type, draft_pm_fq_name)
        except cfgm_common.exceptions.NoIdError:
            pass
        if draft_pm_uuid is not None:
            try:
                # If pending security modifications, it fails to delete the
                # draft PM
                cls.server.internal_request_delete(
                    PolicyManagementServer.resource_type, draft_pm_uuid)
            except cfgm_common.exceptions.HttpError as e:
                if e.status_code != 404:
                    return False, (e.status_code, e.content), None

        default_aps_uuid = None
        defaut_aps_fq_name = obj_dict['fq_name'] +\
            ['default-%s' % ApplicationPolicySetServer.resource_type]
        try:
            default_aps_uuid = db_conn.fq_name_to_uuid(
                ApplicationPolicySetServer.object_type, defaut_aps_fq_name)
        except cfgm_common.exceptions.NoIdError:
            pass
        if default_aps_uuid is not None:
            try:
                cls.server.internal_request_ref_update(
                    cls.resource_type,
                    id,
                    'DELETE',
                    ApplicationPolicySetServer.resource_type,
                    default_aps_uuid,
                )
                cls.server.internal_request_delete(
                    ApplicationPolicySetServer.resource_type, default_aps_uuid)
            except cfgm_common.exceptions.HttpError as e:
                if e.status_code != 404:
                    return False, (e.status_code, e.content), None

            def undo():
                return cls._ensure_default_application_policy_set(
                    default_aps_uuid, fq_name)
            get_context().push_undo(undo)

        return True, '', None

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn, **kwargs):
        # Delete the zookeeper counter nodes
        path = _DEFAULT_ZK_COUNTER_PATH_PREFIX + id
        if db_conn._zk_db.quota_counter_exists(path):
            db_conn._zk_db.delete_quota_counter(path)
        return True, ""
    # end post_dbe_delete

    @classmethod
    def pre_dbe_read(cls, id, fq_name, db_conn):
        return cls._ensure_default_application_policy_set(id, fq_name)

    @classmethod
    def dbe_update_notification(cls, obj_id, extra_dict=None):
        quota_counter = cls.server.quota_counter
        db_conn = cls.server._db_conn
        ok, result = QuotaHelper.get_project_dict_for_quota(obj_id, db_conn)
        if not ok:
            return False, result
        proj_dict = result

        for obj_type, quota_limit in proj_dict.get('quota', {}).items():
            path_prefix = _DEFAULT_ZK_COUNTER_PATH_PREFIX + obj_id
            path = path_prefix + "/" + obj_type
            if (quota_counter.get(path) and (quota_limit == -1 or
                                            quota_limit is None)):
                # free the counter from cache for resources updated
                # with unlimted quota
                del quota_counter[path]

        return True, ''
    #end dbe_update_notification

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        quota_counter = cls.server.quota_counter
        for obj_type in obj_dict.get('quota', {}).keys():
            path_prefix = _DEFAULT_ZK_COUNTER_PATH_PREFIX + obj_id
            path = path_prefix + "/" + obj_type
            if quota_counter.get(path):
                # free the counter from cache
                del quota_counter[path]

        return True, ''
    #end dbe_delete_notification
# end ProjectServer

class RouteAggregateServer(Resource, RouteAggregate):
    @classmethod
    def _check(cls, obj_dict, db_conn):
        si_refs = obj_dict.get('service_instance_refs') or []
        if len(si_refs) > 1:
            return (False, (400, 'RouteAggregate objects can refer to only '
                                 'one service instance'))
        family = None
        entries = obj_dict.get('aggregate_route_entries') or {}
        for route in entries.get('route') or []:
            try:
                route_family = IPNetwork(route).version
            except TypeError:
                return (False, (400, 'Invalid route: %s' % route))
            if family and route_family != family:
                return (False, (400, 'All prefixes in a route aggregate '
                                'object must be of same ip family'))
            family = route_family
        return True, ""
    # end _check

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls._check(obj_dict, db_conn)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls._check(obj_dict, db_conn)

# end class RouteAggregateServer

class ForwardingClassServer(Resource, ForwardingClass):
    @classmethod
    def _check_fc_id(cls, obj_dict, db_conn):
        fc_id = 0
        if obj_dict.get('forwarding_class_id'):
            fc_id = obj_dict.get('forwarding_class_id')

        id_filters = {'forwarding_class_id' : [fc_id]}
        (ok, forwarding_class_list, _) = db_conn.dbe_list('forwarding_class',
                                                       filters = id_filters)
        if not ok:
            return (ok, (500, 'Error in dbe_list: %s' %(forwarding_class_list)))

        if len(forwarding_class_list) != 0:
            return (False, (400, "Forwarding class %s is configured "
                    "with a id %d" % (forwarding_class_list[0][0],
                     fc_id)))
        return (True, '')
    # end _check_fc_id

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
# end class ForwardingClassServer


class AlarmServer(Resource, Alarm):

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        if 'alarm_rules' not in obj_dict or obj_dict['alarm_rules'] is None:
            return (False, (400, 'alarm_rules not specified or null'))
        (ok, error) = cls._check_alarm_rules(obj_dict['alarm_rules'])
        if not ok:
            return (False, error)
        return True, ''
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        if 'alarm_rules' in obj_dict:
            if obj_dict['alarm_rules'] is None:
                return (False, (400, 'alarm_rules cannot be removed'))
            (ok, error) = cls._check_alarm_rules(obj_dict['alarm_rules'])
            if not ok:
                return (False, error)
        return True, ''
    # end pre_dbe_update

    @classmethod
    def _check_alarm_rules(cls, alarm_rules):
        operand2_fields = ['uve_attribute', 'json_value']
        try:
            for and_list in alarm_rules['or_list']:
                for and_cond in and_list['and_list']:
                    if any(k in and_cond['operand2'] for k in operand2_fields):
                        uve_attr = and_cond['operand2'].get('uve_attribute')
                        json_val = and_cond['operand2'].get('json_value')
                        if uve_attr is not None and json_val is not None:
                            return (False, (400, 'operand2 should have '
                                'either "uve_attribute" or "json_value", '
                                'not both'))
                        if json_val is not None:
                            try:
                                json.loads(json_val)
                            except ValueError:
                                return (False, (400, 'Invalid json_value %s '
                                    'specified in alarm_rules' % (json_val)))
                        if and_cond['operation'] == 'range':
                            if json_val is None:
                                return (False, (400, 'json_value not specified'
                                    ' for "range" operation'))
                            val = json.loads(json_val)
                            if not (isinstance(val, list) and
                                    len(val) == 2 and
                                    isinstance(val[0], (int, long, float)) and
                                    isinstance(val[1], (int, long, float)) and
                                    val[0] < val[1]):
                                return (False, (400, 'Invalid json_value %s '
                                    'for "range" operation. json_value should '
                                    'be specified as "[x, y]", where x < y' %
                                    (json_val)))
                    else:
                        return (False, (400, 'operand2 should have '
                            '"uve_attribute" or "json_value"'))
        except Exception as e:
            return (False, (400, 'Invalid alarm_rules'))
        return (True, '')
    # end _check_alarm_rules

# end class AlarmServer


class QosConfigServer(Resource, QosConfig):
    @classmethod
    def _check_qos_values(cls, obj_dict, db_conn):
        fc_pair = 'qos_id_forwarding_class_pair'
        if 'dscp_entries' in obj_dict:
            for qos_id_pair in obj_dict['dscp_entries'].get(fc_pair) or []:
                dscp = qos_id_pair.get('key')
                if dscp and dscp < 0 or dscp > 63:
                    return (False, (400, "Invalid DSCP value %d"
                                   % qos_id_pair.get('key')))

        if 'vlan_priority_entries' in obj_dict:
            for qos_id_pair in obj_dict['vlan_priority_entries'].get(fc_pair) or []:
                vlan_priority = qos_id_pair.get('key')
                if vlan_priority and vlan_priority < 0 or vlan_priority > 7:
                    return (False, (400, "Invalid 802.1p value %d"
                                    % qos_id_pair.get('key')))

        if 'mpls_exp_entries' in obj_dict:
            for qos_id_pair in obj_dict['mpls_exp_entries'].get(fc_pair) or []:
                mpls_exp = qos_id_pair.get('key')
                if mpls_exp and mpls_exp < 0 or mpls_exp > 7:
                    return (False, (400, "Invalid MPLS EXP value %d"
                                          % qos_id_pair.get('key')))
        return (True, '')

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        obj_dict['global_system_config_refs'] = [{'to': ['default-global-system-config']}]
        return cls._check_qos_values(obj_dict, db_conn)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls._check_qos_values(obj_dict, db_conn)
# end class QosConfigServer


class FloatingIpPoolServer(Resource, FloatingIpPool):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        #
        # Floating-ip-pools corresponding to a virtual-network can be
        # 'optionally' configured to be specific to a ipam subnet on a virtual-
        # network.
        #
        # If the subnet is specified, sanity check the config to verify that
        # the subnet exists in the virtual-network.
        #

        # If subnet info is not specified in the floating-ip-pool object, then
        # there is nothing to validate. Just return.
        try:
            if (obj_dict['parent_type'] != 'virtual-network' or\
                obj_dict['floating_ip_pool_subnets'] is None or\
                obj_dict['floating_ip_pool_subnets']['subnet_uuid'] is None or\
                not obj_dict['floating_ip_pool_subnets']['subnet_uuid']):
                    return True, ""
        except KeyError, TypeError:
            return True, ""

        try:
            # Get the virtual-network object.
            vn_fq_name = obj_dict['fq_name'][:-1]
            vn_id = db_conn.fq_name_to_uuid('virtual_network', vn_fq_name)
            ok, vn_dict = cls.dbe_read(db_conn, 'virtual_network', vn_id)
            if not ok:
                return ok, vn_dict

            # Iterate through configured subnets on this FloatingIpPool.
            # Validate the requested subnets are found on the virtual-network.
            for fip_pool_subnet in \
                obj_dict['floating_ip_pool_subnets']['subnet_uuid']:
                vn_ipams = vn_dict['network_ipam_refs']
                subnet_found = False
                for ipam in vn_ipams:
                     if not ipam['attr']:
                         continue
                     for ipam_subnet in ipam['attr']['ipam_subnets']:
                         ipam_subnet_info = None
                         if ipam_subnet['subnet_uuid']:
                             if ipam_subnet['subnet_uuid'] != fip_pool_subnet:
                                 # Subnet uuid does not match. This is not the
                                 # requested subnet. Keep looking.
                                 continue

                             # Subnet of interest was found.
                             subnet_found = True
                             break

                if not subnet_found:
                    # Specified subnet was not found on the virtual-network.
                    # Return failure.
                    msg = "Subnet %s was not found in virtual-network %s" %\
                        (fip_pool_subnet, vn_id)
                    return (False, (400, msg))

        except KeyError:
            return (False,
                (400, 'Incomplete info to create a floating-ip-pool'))

        return True, ""
# end class FloatingIpPoolServer


class BgpAsAServiceServer(Resource, BgpAsAService):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        if (not obj_dict.get('bgpaas_shared') == True or
           obj_dict.get('bgpaas_ip_address') != None):
            return True, ''
        return (False, (400, 'BGPaaS IP Address needs to be ' +
                             'configured if BGPaaS is shared'))
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        if 'bgpaas_shared' in obj_dict:
            ok, result = cls.dbe_read(db_conn, 'bgp_as_a_service', id)

            if not ok:
                return ok, result
            if result.get('bgpaas_shared', False) != obj_dict['bgpaas_shared']:
                return (False, (400, 'BGPaaS sharing cannot be modified'))
        return True, ""
    # end pre_dbe_update
# end BgpAsAServiceServer


class PhysicalRouterServer(Resource, PhysicalRouter):
    @classmethod
    def post_dbe_read(cls, obj_dict, db_conn):
        if obj_dict.get('physical_router_user_credentials'):
            if obj_dict['physical_router_user_credentials'].get('password'):
                obj_dict['physical_router_user_credentials']['password'] = "**Password Hidden**"
        return True, ''

    @classmethod
    def post_dbe_list(cls, obj_result_list, db_conn):
        for obj_result in obj_result_list:
            if obj_result.get('physical-router'):
                (ok, err_msg) = cls.post_dbe_read(obj_result['physical-router'], db_conn)
                if not ok:
                    return ok, err_msg
        return True, ''
# end class PhysicalRouterServer


class BgpvpnServer(Resource, Bgpvpn):
    @classmethod
    def check_network_supports_vpn_type(
            cls, db_conn, vn_dict, db_vn_dict=None):
        """
        Validate the associated bgpvpn types correspond to the virtual
        forwarding type.
        """
        if not vn_dict:
            return True, ''

        if ('bgpvpn_refs' not in vn_dict and
                'virtual_network_properties' not in vn_dict):
            return True, ''

        forwarding_mode = 'l2_l3'
        vn_props = None
        if 'virtual_network_properties' in vn_dict:
            vn_props = vn_dict['virtual_network_properties']
        elif db_vn_dict and 'virtual_network_properties' in db_vn_dict:
            vn_props = db_vn_dict['virtual_network_properties']
        if vn_props is not None:
            forwarding_mode = vn_props.get('forwarding_mode', 'l2_l3')
        # Forwarding mode 'l2_l3' (default mode) can support all vpn types
        if forwarding_mode == 'l2_l3':
            return True, ''


        bgpvpn_uuids = []
        if 'bgpvpn_refs' in vn_dict:
            bgpvpn_uuids = set(bgpvpn_ref['uuid'] for bgpvpn_ref
                               in vn_dict.get('bgpvpn_refs', []))
        elif db_vn_dict:
            bgpvpn_uuids = set(bgpvpn_ref['uuid'] for bgpvpn_ref
                               in db_vn_dict.get('bgpvpn_refs', []))

        if not bgpvpn_uuids:
            return True, ''

        (ok, result, _) = db_conn.dbe_list('bgpvpn',
                                      obj_uuids=list(bgpvpn_uuids),
                                      field_names=['bgpvpn_type'])
        if not ok:
            return ok, (500, 'Error in dbe_list: %s' % pformat(result))
        bgpvpns = result

        vpn_types = set(bgpvpn.get('bgpvpn_type', 'l3') for bgpvpn in bgpvpns)
        if len(vpn_types) > 1:
            msg = ("Cannot associate different bgpvpn types '%s' on a "
                   "virtual network with a forwarding mode different to"
                   "'l2_l3'" % vpn_types)
            return False, (400, msg)
        elif set([forwarding_mode]) != vpn_types:
            msg = ("Cannot associate bgpvpn type '%s' with a virtual "
                   "network in forwarding mode '%s'" % (vpn_types.pop(),
                                                        forwarding_mode))
            return False, (400, msg)
        return True, ''

    @classmethod
    def check_router_supports_vpn_type(cls, db_conn, lr_dict):
        """Limit associated bgpvpn types to 'l3' for logical router."""

        if not lr_dict or 'bgpvpn_refs' not in lr_dict:
            return True, ''

        bgpvpn_uuids = set(bgpvpn_ref['uuid'] for bgpvpn_ref
                           in lr_dict.get('bgpvpn_refs', []))
        if not bgpvpn_uuids:
            return True, ''

        (ok, result, _) = db_conn.dbe_list('bgpvpn',
                                      obj_uuids=list(bgpvpn_uuids),
                                      field_names=['bgpvpn_type'])
        if not ok:
            return ok, (500, 'Error in dbe_list: %s' % pformat(result))
        bgpvpns = result

        bgpvpn_not_supported = [bgpvpn for bgpvpn in bgpvpns
                                if bgpvpn.get('bgpvpn_type', 'l3') != 'l3']

        if not bgpvpn_not_supported:
            return True, ''

        msg = "Only bgpvpn type 'l3' can be associated to a logical router:\n"
        for bgpvpn in bgpvpn_not_supported:
            msg += ("- bgpvpn %s(%s) type is %s\n" %
                    (bgpvpn.get('display_name', bgpvpn['fq_name'][-1]),
                     bgpvpn['uuid'], bgpvpn.get('bgpvpn_type', 'l3')))
        return False, (400, msg)

    @classmethod
    def check_network_has_bgpvpn_assoc_via_router(
            cls, db_conn, vn_dict, db_vn_dict=None):
        """
        Check if logical routers attached to the network already have
        a bgpvpn associated to it. If yes, forbid to add bgpvpn to that
        networks.
        """
        if vn_dict.get('bgpvpn_refs') is None:
            return True, ''

        # List all logical router's vmis of networks
        filters = {
            'virtual_machine_interface_device_owner':
            ['network:router_interface']
        }
        (ok, result, _) = db_conn.dbe_list('virtual_machine_interface',
                                      back_ref_uuids=[vn_dict['uuid']],
                                      filters=filters,
                                      field_names=['logical_router_back_refs'])
        if not ok:
            return ok, (500, 'Error in dbe_list: %s' % pformat(result))
        vmis = result

        # Read bgpvpn refs of logical routers found
        lr_uuids = [vmi['logical_router_back_refs'][0]['uuid']
                    for vmi in vmis]
        if not lr_uuids:
            return True, ''
        (ok, result, _) = db_conn.dbe_list('logical_router',
                                      obj_uuids=lr_uuids,
                                      field_names=['bgpvpn_refs'])
        if not ok:
            return ok, (500, 'Error in dbe_list: %s' % pformat(result))
        lrs = result
        found_bgpvpns = [(bgpvpn_ref['to'][-1], bgpvpn_ref['uuid'],
                            lr.get('display_name', lr['fq_name'][-1]),
                            lr['uuid'])
                         for lr in lrs
                           for bgpvpn_ref in lr.get('bgpvpn_refs', [])]
        if not found_bgpvpns:
            return True, ''

        vn_name = (vn_dict.get('fq_name') or db_vn_dict['fq_name'])[-1]
        msg = ("Network %s (%s) is linked to a logical router which is "
               "associated to bgpvpn(s):\n" % (vn_name, vn_dict['uuid']))
        for found_bgpvpn in found_bgpvpns:
            msg += ("- bgpvpn %s (%s) associated to router %s (%s)\n" %
                    found_bgpvpn)
        return False, (400, msg[:-1])

    @classmethod
    def check_router_has_bgpvpn_assoc_via_network(
            cls, db_conn, lr_dict, db_lr_dict=None):
        """
        Check if virtual networks attached to the router already have
        a bgpvpn associated to it. If yes, forbid to add bgpvpn to that
        routers.
        """
        if ('bgpvpn_refs' not in lr_dict and
            'virtual_machine_interface_refs' not in lr_dict):
            return True, ''

        bgpvpn_refs = None
        if 'bgpvpn_refs' in lr_dict:
            bgpvpn_refs = lr_dict['bgpvpn_refs']
        elif db_lr_dict:
            bgpvpn_refs = db_lr_dict.get('bgpvpn_refs')
        if not bgpvpn_refs:
            return True, ''

        vmi_refs = []
        if 'virtual_machine_interface_refs' in lr_dict:
            vmi_refs = lr_dict['virtual_machine_interface_refs']
        elif db_lr_dict:
            vmi_refs = db_lr_dict.get('virtual_machine_interface_refs') or []
        vmi_uuids = [vmi_ref['uuid'] for vmi_ref in vmi_refs]
        if not vmi_uuids:
            return True, ''

        # List vmis to obtain their virtual networks
        (ok, result, _) = db_conn.dbe_list('virtual_machine_interface',
                                      obj_uuids=vmi_uuids,
                                      field_names=['virtual_network_refs'])
        if not ok:
            return ok, (500, 'Error in dbe_list: %s' % pformat(result))
        vmis = result
        vn_uuids = [vn_ref['uuid']
                    for vmi in vmis
                    for vn_ref in vmi.get('virtual_network_refs', [])]
        if not vn_uuids:
            return True, ''

        # List bgpvpn refs of virtual networks found
        (ok, result, _) = db_conn.dbe_list('virtual_network',
                                      obj_uuids=vn_uuids,
                                      field_names=['bgpvpn_refs'])
        if not ok:
            return ok, (500, 'Error in dbe_list: %s' % pformat(result))
        vns = result
        found_bgpvpns = [(bgpvpn_ref['to'][-1], bgpvpn_ref['uuid'],
                          vn.get('display_name', vn['fq_name'][-1]),
                          vn['uuid'])
                         for vn in vns
                         for bgpvpn_ref in vn.get('bgpvpn_refs', [])]
        if not found_bgpvpns:
            return True, ''
        lr_name = (lr_dict.get('fq_name') or db_lr_dict['fq_name'])[-1]
        msg = ("Router %s (%s) is linked to virtual network(s) which is/are "
               "associated to bgpvpn(s):\n" % (lr_name, lr_dict['uuid']))
        for found_bgpvpn in found_bgpvpns:
            msg += ("- bgpvpn %s (%s) associated to network %s (%s)\n" %
                    found_bgpvpn)
        return False, (400, msg[:-1])

class BgpRouterServer(Resource, BgpRouter):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        sub_cluster_ref = obj_dict.get('sub_cluster_refs')
        bgp_router_prop = obj_dict.get('bgp_router_parameters')
        if bgp_router_prop:
            asn = bgp_router_prop.get('autonomous_system')
        else:
            asn = None
        if sub_cluster_ref and asn:
            sub_cluster_obj = db_conn.uuid_to_obj_dict(
                         obj_dict['sub_cluster_refs'][0]['uuid'])
            if asn != sub_cluster_obj['prop:sub_cluster_asn']:
               return False, (400, 'Subcluster asn and bgp asn should be same')
        return True, ''

    @classmethod
    def _validate_subcluster_dep(cls, obj_dict, db_conn):
        if 'sub_cluster_refs' in obj_dict:
            if len(obj_dict['sub_cluster_refs']):
                sub_cluster_obj = db_conn.uuid_to_obj_dict(
                         obj_dict['sub_cluster_refs'][0]['uuid'])
                sub_cluster_asn = sub_cluster_obj['prop:sub_cluster_asn']
            else:
                sub_cluster_asn = None
        else:
            bgp_obj = db_conn.uuid_to_obj_dict(obj_dict['uuid'])
            sub_cluster_ref = ([key for key in bgp_obj.keys()
                                     if key.startswith('ref:sub_cluster')])
            if len(sub_cluster_ref):
                sub_cluster_uuid = sub_cluster_ref[0].split(':')[-1]
                sub_cluster_obj = db_conn.uuid_to_obj_dict(sub_cluster_uuid)
                sub_cluster_asn = sub_cluster_obj.get('prop:sub_cluster_asn')
            else:
                sub_cluster_asn = None
        if (sub_cluster_asn):
            if (obj_dict.get('bgp_router_parameters') and
                 obj_dict['bgp_router_parameters'].get('autonomous_system')):
                asn = obj_dict['bgp_router_parameters'].get('autonomous_system')
            else:
                bgp_obj = db_conn.uuid_to_obj_dict(obj_dict['uuid'])
                asn = bgp_obj['prop:bgp_router_parameters'].get('autonomous_system')
            if asn != sub_cluster_asn:
                return (False, (400, 'Subcluster asn and bgp asn should be same'))
        return True, ''

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn,
            prop_collection_updates=None, ref_update=None):
        if (('sub_cluster_refs' in obj_dict) or (obj_dict.get('bgp_router_parameters') and
                 obj_dict['bgp_router_parameters'].get('autonomous_system'))):
            return cls._validate_subcluster_dep(obj_dict, db_conn)
        return True, ''

# end class BgpRouterServer

class SubClusterServer(Resource, SubCluster):
    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn,
            prop_collection_updates=None, ref_update=None):
        if 'sub_cluster_asn' in obj_dict:
            return False (400, 'Sub cluster ASN can not be modified')
        return True, ''
# end class SubClusterServer


# Just decelare here to heritate 'locate' method of Resource class
class PolicyManagementServer(Resource, PolicyManagement):
    pass


class AddressGroupServer(SecurityResourceBase, AddressGroup):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls.check_draft_mode_state(obj_dict)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls.check_draft_mode_state(obj_dict)


class RouteTargetServer(Resource, RouteTarget):
    @staticmethod
    def _parse_route_target_name(name):
        try:
            if isinstance(name, basestring):
                prefix, asn, target = name.split(':')
            elif isinstance(name, list):
                prefix, asn, target = name
            else:
                raise ValueError
            if prefix != 'target':
                raise ValueError
            target = int(target)
            if not asn.isdigit():
                try:
                    IPAddress(asn)
                except netaddr.core.AddrFormatError:
                    raise ValueError
            else:
                asn = int(asn)
        except ValueError:
            msg = ("Route target must be of the format "
                   "'target:<asn>:<number>' or 'target:<ip>:<number>'")
            return False, (400, msg)
        return True, (asn, target)

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls._parse_route_target_name(obj_dict['fq_name'][-1])

    @classmethod
    def is_user_defined(cls, route_target_name, global_asn=None):
        ok, result = cls._parse_route_target_name(route_target_name)
        if not ok:
            return False, result
        asn, target = result
        if not global_asn:
            try:
                global_asn = cls.server.global_autonomous_system
            except cfgm_common.exceptions.VncError as e:
                return False, (400, str(e))

        if asn == global_asn and target >= cfgm_common.BGP_RTGT_MIN_ID:
            return True, False
        return True, True

class RoutingPolicyServer(Resource, RoutingPolicy):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        asn_list = None
        rp_entries = obj_dict.get('routing_policy_entries')
        if rp_entries:
            term = rp_entries.get('term')[0]
            if term:
                action_list = term.get('term_action_list')
                if action_list:
                    action = action_list.get('update')
                    if action:
                        as_path = action.get('as_path')
                        if as_path:
                            expand = as_path.get('expand')
                            if expand:
                                asn_list = expand.get('asn_list')

        try:
            global_asn = cls.server.global_autonomous_system
        except cfgm_common.exceptions.VncError as e:
            return False, (400, str(e))

        if asn_list and global_asn in asn_list:
            msg = ("ASN can't be same as global system config asn")
            return False, (400, msg)
        return True, ""
# end class RoutingPolicyServer

