#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common import proto_dict
from cfgm_common.exceptions import NoIdError
from sandesh_common.vns.constants import DEFAULT_MATCH_TAG_TYPE
from vnc_api.gen.resource_common import AddressGroup
from vnc_api.gen.resource_common import FirewallRule
from vnc_api.gen.resource_common import PolicyManagement
from vnc_api.gen.resource_common import Project
from vnc_api.gen.resource_common import ServiceGroup
from vnc_api.gen.resource_common import VirtualNetwork

from vnc_cfg_api_server.context import is_internal_request
from vnc_cfg_api_server.resources._security_base import SecurityResourceBase


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
                    return False, (400, "Invalid protocol: %s" % protocol)
            elif protocol not in proto_dict:
                return False, (400, "Invalid protocol: %s" % protocol)
            else:
                protocol_id = proto_dict[protocol]
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

                ok, result = cls.server.get_resource_class(
                    'tag_type').get_tag_type_id(tag_type)
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
                    except NoIdError:
                        msg = ('No Address Group object found for %s' %
                               ag_fq_name_str)
                        return False, (404, msg)
                    ag_refs.append({'to': ag_fq_name, 'uuid': ag_uuid})

        if {r['uuid'] for r in ag_refs} != {r['uuid'] for r in db_ag_refs}:
            obj_dict['address_group_refs'] = ag_refs

        return True, ''

    @classmethod
    def _frs_fix_endpoint_tag(cls, obj_dict, db_obj_dict=None):
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
            elif obj_parent_type == PolicyManagement.resource_type:
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
                for tag_name in set(ep.get('tags', []) or []):
                    ok, result = _get_tag_fq_name(tag_name)
                    if not ok:
                        return False, result
                    tag_fq_name = result
                    ok, result = cls.server.get_resource_class('tag').locate(
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
    def _check_host_based_service_action(cls, req_dict, db_dict=None):
        if not db_dict:
            db_dict = {}

        if ('action_list' not in req_dict or
                not req_dict['action_list'].get('host_based_service', False)):
            return True, ''

        parent_type = req_dict.get('parent_type', db_dict.get('parent_type'))
        if parent_type != 'project':
            msg = ("host-based-service rule action can only be use in a "
                   "project scope")
            return False, (400, msg)

        fq_name = req_dict.get('fq_name', db_dict.get('fq_name'))
        parent_uuid = req_dict.get('parent_uuid', db_dict.get('parent_uuid'))
        if not parent_uuid:
            try:
                parent_uuid = cls.db_conn.fq_name_to_uuid(
                    'project', fq_name[:-1])
            except NoIdError:
                msg = "Project %s parent not found " % fq_name
                return False, (404, msg)

        ok, result = cls.server.get_resource_class(
            'host_based_service').host_based_service_enabled(parent_uuid)
        if not ok:
            return False, result
        hbs_enabled, msg = result

        if not hbs_enabled:
            msg = ("Cannot use Host Based Service firewall rule action: %s" %
                   msg)
            return False, (400, msg)

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
            ServiceGroup,
        )
        if not ok:
            return False, result

        ok, result = cls.check_associated_firewall_resource_in_same_scope(
            obj_dict['uuid'],
            obj_dict['fq_name'],
            obj_dict,
            VirtualNetwork,
        )
        if not ok:
            return False, result

        ok, result = cls._check_host_based_service_action(obj_dict)
        if not ok:
            return False, result

        ok, result = cls._frs_fix_service(
            obj_dict.get('service'), obj_dict.get('service_group_refs'))
        if not ok:
            return False, result

        # create default match tag if use doesn't specifiy any explicitly
        if 'match_tags' not in obj_dict:
            obj_dict['match_tag_types'] = {
                'tag_type': DEFAULT_MATCH_TAG_TYPE
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
            id, fq_name, obj_dict, ServiceGroup)
        if not ok:
            return False, result

        ok, result = cls.check_associated_firewall_resource_in_same_scope(
            id, fq_name, obj_dict, VirtualNetwork)
        if not ok:
            return False, result

        ok, read_result = cls.dbe_read(db_conn, 'firewall_rule', id)
        if not ok:
            return ok, read_result
        db_obj_dict = read_result

        ok, result = cls._check_host_based_service_action(
            obj_dict, db_obj_dict)
        if not ok:
            return False, result

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
