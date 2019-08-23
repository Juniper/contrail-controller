#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from functools import wraps

from cfgm_common.exceptions import HttpError
from cfgm_common.exceptions import NoIdError
from sandesh_common.vns.constants import\
    POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT
from sandesh_common.vns.constants import SECURITY_OBJECT_TYPES
from vnc_api.gen.resource_common import GlobalSystemConfig
from vnc_api.gen.resource_common import PolicyManagement
from vnc_api.gen.resource_common import Project

from vnc_cfg_api_server.context import is_internal_request
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


def _draft_mode_sanity_checks(func):
    @wraps(func)
    def wrapper(cls, obj_dict, *args, **kwargs):
        if cls.object_type not in SECURITY_OBJECT_TYPES:
            return True, ''

        if is_internal_request():
            # Ignore all internal calls to not pending pending actions
            return True, ''

        ok, result = cls._get_dedicated_draft_policy_management(obj_dict)
        if not ok:
            return False, result
        # if nothing returned without error, that means security draft mode
        # is  NOT enabled
        if result == '':
            return True, ''
        draft_pm = result

        # if a commit or discard is on going in that scope, forbidden any
        # security resource modification in that scope
        scope_type = draft_pm.get('parent_type',
                                  GlobalSystemConfig.object_type)
        scope_uuid = draft_pm.get('parent_uuid', 'unknown UUID')
        scope_fq_name = draft_pm['fq_name'][:-1]
        if not scope_fq_name:
            scope_fq_name = [GlobalSystemConfig().name]
        scope_read_lock = cls.vnc_zk_client._zk_client.read_lock(
            '%s/%s/%s' % (
                cls.server.security_lock_prefix,
                scope_type,
                ':'.join(scope_fq_name),
            ),
        )
        if not scope_read_lock.acquire(blocking=False):
            contenders = scope_read_lock.contenders()
            for contender in contenders:
                _, action_in_progress = contender.split()
                if action_in_progress:
                    break
            else:
                action_in_progress = '<unknown action>'
            msg = ("Action '%s' on %s '%s' (%s) scope is under progress. "
                   "Cannot modify %s security resource." %
                   (action_in_progress, scope_type.replace('_', ' ').title(),
                    ':'.join(scope_fq_name), scope_uuid,
                    cls.object_type.replace('_', ' ').title()))
            return False, (400, msg)
        try:
            return func(cls, draft_pm, obj_dict, *args, **kwargs)
        finally:
            scope_read_lock.release()
    return wrapper


class SecurityResourceBase(ResourceMixin):
    @classmethod
    def check_associated_firewall_resource_in_same_scope(cls, uuid, fq_name,
                                                         obj_dict, ref_class):
        # NOTE(ethuleau): this method is simply based on the fact global
        # firewall resource (draft or not) have a FQ name length equal to two
        # and scoped one (draft or not) have a FQ name longer than 2 to
        # distinguish a scoped firewall resource to a global one. If that
        # assumption disappear, all that method will need to be re-worked

        ref_name = '%s_refs' % ref_class.object_type
        if ref_name not in obj_dict:
            return True, ''

        scoped_resource = True
        if len(fq_name) == 2:
            scoped_resource = False

        # Scoped firewall resource can reference global or scoped resources
        if scoped_resource:
            return True, ''

        # In case of global firewall resource check if it reference global
        # firewall resource only
        for ref in obj_dict.get(ref_name, []):
            ref_scoped_resource = True
            if len(ref['to']) == 2:
                ref_scoped_resource = False
            # Global firewall resource can reference global firewall resources
            if not ref_scoped_resource:
                continue
            # Global firewall resource cannot reference scoped resources
            msg = ("Global %s %s (%s) cannot reference a scoped %s %s (%s)" %
                   (cls.object_type.replace('_', ' ').title(),
                    ':'.join(fq_name), uuid,
                    ref_class.object_type.replace('_', ' ').title(),
                    ':'.join(ref['to']), ref['uuid']))
            return False, (400, msg)

        return True, ''

    @staticmethod
    def check_draft_mode_state(obj_dict):
        if is_internal_request():
            # system only property
            return True, ''

        if 'draft_mode_state' in obj_dict:
            msg = ("Security resource property 'draft_mode_state' is only "
                   "readable")
            return False, (400, msg)

        return True, ''

    @classmethod
    def set_policy_management_for_security_draft(cls, scope, obj_dict,
                                                 draft_mode_enabled=False):
        if 'enable_security_policy_draft' not in obj_dict:
            return True, ''

        pm_class = cls.server.get_resource_class(PolicyManagement.object_type)
        draft_mode_set = obj_dict.get('enable_security_policy_draft', False)
        draft_pm_name = POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT
        if obj_dict['fq_name'][-1:] == draft_pm_name:
            # This is the policy management dedicated for draft security
            # resources, should already exist we can return it
            return pm_class.locate(obj_dict['fq_name'], create_it=False)
        if scope == GlobalSystemConfig.resource_type:
            parent_type = None
            parent_uuid = None
            draft_pm_fq_name = [draft_pm_name]
        else:
            parent_type = scope
            parent_uuid = obj_dict['uuid']
            draft_pm_fq_name = obj_dict['fq_name'] + [draft_pm_name]

        # Disabling draft mode
        if not draft_mode_set and draft_mode_enabled:
            try:
                draft_pm_uuid = cls.db_conn.fq_name_to_uuid(
                    PolicyManagement.object_type, draft_pm_fq_name)
            except NoIdError:
                return True, ''
            try:
                # If pending security modification, it fails to delete the
                # draft PM
                cls.server.internal_request_delete(
                    PolicyManagement.resource_type, draft_pm_uuid)
            except HttpError as e:
                if e.status_code == 404:
                    pass
                elif e.status_code == 409:
                    ok, result = pm_class.locate(uuid=draft_pm_uuid,
                                                 create_it=False)
                    if not ok:
                        return False, result
                    draft_pm = result
                    if scope == GlobalSystemConfig.resource_type:
                        msg = ("Cannot disable security draft mode on global "
                               "scope as some pending security resource(s) "
                               "need to be reviewed:\n")
                    else:
                        msg = ("Cannot disable security draft mode on scope "
                               "%s (%s) as some pending security resource(s) "
                               "need to be reviewed:\n" % (
                                   ':'.join(obj_dict['fq_name']),
                                   parent_uuid))
                    children_left = []
                    for child_type in ['%ss' % t for t in
                                       SECURITY_OBJECT_TYPES]:
                        children = ["%s (%s)" % (':'.join(c['to']), c['uuid'])
                                    for c in draft_pm.get(child_type, [])]
                        if children:
                            children_left.append(
                                "\t- %s: %s" % (
                                    child_type.replace('_', ' ').title(),
                                    ', '.join(children),
                                ),
                            )
                    if children_left:
                        msg += '\n'.join(children_left)
                        return False, (409, msg)
                    else:
                        return False, (e.status_code, e.content)
                else:
                    return False, (e.status_code, e.content)
        # Enabling draft mode
        elif draft_mode_set and not draft_mode_enabled:
            attrs = {
                'parent_type': parent_type,
                'parent_uuid': parent_uuid,
                'name': draft_pm_name,
                'enable_security_policy_draft': True,
            }
            return pm_class.locate(draft_pm_fq_name, **attrs)

        return True, ''

    @classmethod
    def _get_dedicated_draft_policy_management(cls, obj_dict):
        parent_type = obj_dict.get('parent_type')
        if parent_type == PolicyManagement.resource_type:
            if (obj_dict['fq_name'][-2] ==
                    POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT):
                # Could be already the dedicated draft policy management, in
                # that case it is updating an already pending resource. Return
                # it directly
                return cls.server.get_resource_class(
                    PolicyManagement.object_type).locate(
                        fq_name=obj_dict.get('fq_name')[:-1],
                        uuid=obj_dict.get('parent_uuid'),
                        create_it=False)
            # if not a dedicated draft policy management, it is a global
            # security resources owned by the global policy management
            elif obj_dict['fq_name'][:-1] == PolicyManagement().fq_name:
                parent_type = GlobalSystemConfig.object_type
                parent_fq_name = GlobalSystemConfig().fq_name
                parent_uuid = None
            else:
                return True, ''
        elif parent_type == Project.resource_type:
            parent_type = Project.object_type
            parent_fq_name = obj_dict['fq_name'][:-1]
            parent_uuid = obj_dict.get('parent_uuid')
        else:
            return True, ''

        # check from parent if security draft mode is enabled
        parent_class = cls.server.get_resource_class(parent_type)
        ok, result = parent_class.locate(
            fq_name=parent_fq_name,
            uuid=parent_uuid,
            create_it=False,
            fields=['enable_security_policy_draft']
        )
        if not ok:
            return False, result
        parent_dict = result

        # get scoped policy management dedicated to own pending security
        # resource
        return cls.set_policy_management_for_security_draft(
            parent_class.resource_type, parent_dict)

    @classmethod
    @_draft_mode_sanity_checks
    def pending_dbe_create(cls, draft_pm, obj_dict):
        return cls._pending_update(draft_pm, obj_dict, 'created')

    @classmethod
    @_draft_mode_sanity_checks
    def pending_dbe_update(cls, draft_pm, obj_dict, delta_obj_dict=None,
                           prop_collection_updates=None):
        return cls._pending_update(draft_pm, obj_dict, 'updated',
                                   delta_obj_dict, prop_collection_updates)

    @classmethod
    @_draft_mode_sanity_checks
    def pending_dbe_delete(cls, draft_pm, obj_dict):
        uuid = obj_dict['uuid']
        exist_refs = set()
        relaxed_refs = set(cls.db_conn.dbe_get_relaxed_refs(uuid))

        for backref_field in cls.backref_fields:
            ref_type, _, is_derived = cls.backref_field_types[backref_field]
            if is_derived:
                continue
            exist_refs = {(ref_type, backref['uuid'])
                          for backref in obj_dict.get(backref_field, [])
                          if backref['uuid'] not in relaxed_refs}

        ref_exits_error = set()
        for ref_type, ref_uuid in exist_refs:
            ref_class = cls.server.get_resource_class(ref_type)
            ref_field = '%s_refs' % cls.object_type
            ok, result = ref_class.locate(
                uuid=ref_uuid,
                create_it=False,
                fields=['fq_name', 'parent_type', 'draft_mode_state',
                        ref_field],
            )
            if not ok:
                return False, result
            ref = result
            if (ref['fq_name'][-2] !=
                    POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT):
                ok, result = ref_class.get_pending_resource(
                    ref, fields=['draft_mode_state', ref_field])
                if ok and result == '':
                    # no draft version of the back-referenced resource,
                    # draft mode not enabled, abandon and let classic code
                    # operate
                    return True, ''
                elif not ok and isinstance(result, tuple) and result[0] == 404:
                    # draft mode enabled, but no draft version of the
                    # back-referenced resource, resource still referenced
                    # cannot delete it
                    ref_exits_error.add((ref_type, ref_uuid))
                    continue
                elif not ok:
                    return False, result
                draft_ref = result
            else:
                draft_ref = ref
            if draft_ref['draft_mode_state'] in ['create', 'updated']:
                if uuid in {r['uuid'] for r in draft_ref.get(ref_field, [])}:
                    # draft version of the back-referenced resource still
                    # referencing resource, resource cannot delete it
                    ref_exits_error.add((ref_type, ref_uuid))
                    continue
            # draft version of the back-referenced resource is in pending
            # delete, resource could be deleted

        if ref_exits_error:
            return False, (409, ref_exits_error)

        return cls._pending_update(draft_pm, obj_dict, 'deleted')

    @classmethod
    def _pending_update(cls, draft_pm, obj_dict, draft_mode_state,
                        delta_obj_dict=None, prop_collection_updates=None):
        if not delta_obj_dict:
            delta_obj_dict = {}
        delta_obj_dict.pop('uuid', None)

        # if the parent is already the draft policy management, its updating
        # the draft version of the resource. We can ignore it and let classic
        # update code operated
        if (obj_dict['fq_name'][-2] ==
                POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT):
            if (obj_dict['draft_mode_state'] == 'deleted' and
               draft_mode_state != 'deleted'):
                # Cannot update a pending deleted resource
                msg = ("%s %s is in pending delete, cannot be updated" %
                       (cls.object_type.replace('_', ' ').title(),
                        ':'.join(obj_dict['fq_name'])))
                return False, (400, msg)
            return True, ''

        # Locate draft version of the resource, if does not exits, create it
        draft_fq_name = draft_pm['fq_name'] + obj_dict['fq_name'][-1:]
        # Check if it's trying to create a resource already in pending create
        if draft_mode_state == 'created':
            try:
                cls.db_conn.fq_name_to_uuid(cls.object_type, draft_fq_name)
                msg = ("%s named %s was already created and it is pending to "
                       "be committed or discarded. You cannot create it again"
                       % (cls.object_type.replace('_', ' ').title(),
                          ':'.join(obj_dict['fq_name'])))
                return False, (400, msg)
            except NoIdError:
                pass

        obj_dict['parent_type'] = PolicyManagement.resource_type
        obj_dict['parent_uuid'] = draft_pm['uuid']
        obj_dict.pop('fq_name', None)
        obj_dict.pop('uuid', None)
        if 'id_perms' in obj_dict:
            obj_dict['id_perms'].pop('uuid', None)
        # Set draft mode to created, if draft resource already exists, property
        # will be read from the DB, if not state need to be set to 'created'
        obj_dict['draft_mode_state'] = draft_mode_state
        ok, result = cls.locate(fq_name=draft_fq_name, **obj_dict)
        if not ok:
            return False, result
        draft_obj_dict = result

        # Allow to update pending created or updated security resources
        if draft_mode_state == 'updated':
            if draft_obj_dict['draft_mode_state'] == 'deleted':
                # Disallow to update a pending deleted security resource
                msg = ("%s %s is in pending delete, cannot be updated" %
                       (cls.object_type.replace('_', ' ').title(),
                        ':'.join(obj_dict['fq_name'])))
                return False, (400, msg)
            elif delta_obj_dict:
                try:
                    cls.server.internal_request_update(
                        cls.resource_type,
                        draft_obj_dict['uuid'],
                        delta_obj_dict,
                    )
                except HttpError as e:
                    return False, (e.status_code, e.content)
            elif prop_collection_updates:
                try:
                    cls.server.internal_request_prop_collection(
                        draft_obj_dict['uuid'], prop_collection_updates)
                except HttpError as e:
                    return False, (e.status_code, e.content)
        elif (draft_mode_state == 'deleted' and
                draft_obj_dict['draft_mode_state'] == 'updated'):
            cls.server.internal_request_update(
                cls.resource_type,
                draft_obj_dict['uuid'],
                {'draft_mode_state': 'deleted'},
            )

        draft_obj_dict.update(delta_obj_dict)
        return True, (202, draft_obj_dict)

    @classmethod
    def get_pending_resource(cls, obj_dict, fields=None):
        ok, result = cls._get_dedicated_draft_policy_management(obj_dict)
        if not ok:
            return False, result
        # if nothing returned without error, that means security draft mode is
        # NOT enabled or this is an internal pending security resource update
        if result == '':
            return True, ''
        draft_pm_dict = result
        pending_fq_name = draft_pm_dict['fq_name'] + obj_dict['fq_name'][-1:]

        # return pending resource if already exists
        return cls.locate(pending_fq_name, create_it=False, fields=fields)
