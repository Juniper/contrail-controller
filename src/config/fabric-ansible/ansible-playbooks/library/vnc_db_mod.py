#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
# This file contains implementation of wrapper module for VNC operations
# such as add, delete, update, read, list, ref-update, ref-delete,
# fqname_to_id, id_to_fqname
#

import ast
import sys
import time

from inflection import camelize
from job_manager.job_utils import JobVncApi
import vnc_api

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
sys.path.append('../fabric-ansible/ansible-playbooks/module_utils') # unit test
from fabric_utils import FabricAnsibleModule # noqa


DOCUMENTATION = '''
---
Performs CRUD operation for the objects in database.
Following operations are supported - ADD, DELETE, UPDATE,
READ, LIST, REF-UPDATE, REF-DELETE, FQNAME_TO_ID,
ID_TO_FQNAME

For CREATE operation, if 'object_obj_if_present' flag is
set to True (default value), module tries to update the
existing object. If this flag is set to False, module
will return SUCCESS with existing uuid

LIST operation is supported with filters/fields/back_ref_id
and detail clause
'''

EXAMPLES = '''
CREATE operation:
    vnc_db_mod:
        job_ctx: "{{ job_ctx }}"
        object_type: "physical_router"
        object_op: "create"
        object_dict: |
          {
              "parent_type": "global-system-config",
              "fq_name": ["default-global-system-config", "mx-240"],
              "physical_router_management_ip": "172.10.68.1",
              "physical_router_vendor_name": "Juniper",
              "physical_router_product_name": "MX",
              "physical_router_device_family": "junos"
          }

BULK CREATE operation:
    vnc_db_mod:
        job_ctx: "{{ job_ctx }}"
        object_type: "physical_router"
        object_op: "bulk_create"
        object_list: [
          {
              "parent_type": "global-system-config",
              "fq_name": ["default-global-system-config", "mx-240"],
              "physical_router_management_ip": "172.10.68.1",
              "physical_router_vendor_name": "Juniper",
              "physical_router_product_name": "MX",
              "physical_router_device_family": "junos"
          },
          {
              "parent_type": "global-system-config",
              "fq_name": ["default-global-system-config", "mx-240-2"],
              "physical_router_management_ip": "172.10.68.2",
              "physical_router_vendor_name": "Juniper",
              "physical_router_product_name": "MX",
              "physical_router_device_family": "junos"
          }
        ]

UPDATE operation:
    vnc_db_mod:
        job_ctx: "{{ job_ctx }}"
        object_type: "physical_router"
        object_op: "update"
        object_dict: |
          {
              "uuid": "{{ item[0].obj['physical-routers'][0].uuid }}",
              "physical_router_user_credentials": {
                  "username": "{{ item.1.username }}",
                  "password": "{{ item.1.password }}"
              }
          }

BULK UPDATE operation:
    vnc_db_mod:
        job_ctx: "{{ job_ctx }}"
        object_type: "physical_router"
        object_op: "bulk_update"
        object_list: [
          {
              "uuid": "{{ item[0].obj['physical-routers'][0].uuid }}",
              "physical_router_user_credentials": {
                  "username": "{{ item.1.username }}",
                  "password": "{{ item.1.password }}"
              }
          }
        ]

READ opreration:
    vnc_db_mod:
        job_ctx: "{{ job_ctx }}"
        object_type: "fabric"
        object_op: "read"
        object_dict: |
          {
              "fq_name": "{{ fabric_fq_name }}"
          }

REF-UPDATE opreation:
    vnc_db_mod:
        job_ctx: "{{ job_ctx }}"
        object_type: "fabric"
        object_op: "ref_update"
        object_dict: |
          {
              "obj_uuid": "{{ fabric_uuid }}",
              "ref_type": "physical_router",
              "ref_uuid": "{{ item.uuid }}"
          }

BULK REF-UPDATE operation:
    vnc_db_mod:
        job_ctx: "{{job_ctx}}"
        object_type: "physical_interface"
        object_dict: {
                      "ref_type": "physical_interface",
                      "ignore_unknown_id_err": True
                     }
        object_op: "bulk_ref_update"
        object_list: [[obj_fqname, ref_fqname], [obj_fqname, ref_fqname]]

ID_TO_FQNAME operation:
    vnc_db_mod:
        job_ctx: "{{ job_ctx }}"
        object_type: "fabric"
        object_op: "id_to_fq_name"
        object_dict: |
          {
              "uuid": "{{ fabric_uuid }}"
          }

FQNAME_TO_ID operation:
    vnc_db_mod:
        job_ctx: "{{ job_ctx }}"
        object_type: "tag"
        object_op: "fq_name_to_id"
        object_dict: |
          {
              "fq_name": ["{{ tag_fq_name }}"]
          }

LIST with filters and detail operation:
    vnc_db_mod:
        job_ctx: "{{ job_ctx }}"
        object_type: "physical_router"
        object_op: "list"
        object_dict: |
          {
              "filters":
               {"physical_router_management_ip":"{{ item.hostname }}"},
              "detail": "True",
          }
BULK QUERY operation:
    vnc_db_mod:
        job_ctx: "{{job_ctx}}"
        object_type: "physical_interface"
        object_op: "bulk_query"
        object_dict: {"fields": ['physical_interface_port_id']}
        object_list: [prouter_fqname, prouter_fqname, <parent_fqname>]
'''


class VncMod(object):
    JOB_IN_PROGRESS = "IN_PROGRESS"

    def __init__(self, module):
        """Initialising module parameters."""
        self.cls = None

        # Fetch module params
        self.job_ctx = module.params['job_ctx']
        self.object_type = module.params['object_type']
        self.object_op = module.params['object_op']
        self.object_dict = module.params['object_dict']
        self.object_list = module.params['object_list']
        self.update_obj = module.params['update_obj_if_present']
        self.enable_job_ctx = module.params['enable_job_ctx']

        # additional validation on top of argument_spec
        self._validate_params()

        # initialize vnc_lib
        self._init_vnc_lib()
    # end __init__

    def _validate_params(self):
        if self.enable_job_ctx:
            required_keys = [
                'auth_token', 'job_template_fqname', 'job_execution_id',
                'config_args', 'job_input']
        else:
            required_keys = ['auth_token']
        for key in required_keys:
            if key not in self.job_ctx or self.job_ctx.get(key) is None:
                raise ValueError("Missing job context param: %s" % key)
    # end _validate_params

    def _init_vnc_lib(self):
        # Instantiate the VNC library
        # Retry for sometime, till API server is up
        errmsg = None
        for i in range(0, 10):
            try:
                self.vnc_lib = JobVncApi.vnc_init(self.job_ctx)
                break
            except Exception as ex:
                time.sleep(10)
                errmsg = "Failed to connect to API server due to error: %s"\
                    % str(ex)

        if self.vnc_lib is None:
            raise RuntimeError(errmsg)
    # end __init_vnc_lib

    def do_oper(self):
        """Vnc db crud operations."""
        # Get the class name from object type
        cls_name = camelize(self.object_type)

        # Get the class object
        self.cls = self._str_to_class(cls_name)

        if self.cls is None:
            raise ValueError("Invalid object_type: %s" % self.object_type)

        # This creates an object, if not existing, else update the object
        results = None
        if self.object_op == 'create':
            results = self._create_oper()

        elif self.object_op == 'update':
            results = self._update_oper()

        elif self.object_op == 'bulk_create':
            results = self._bulk_create_oper()

        elif self.object_op == 'bulk_update':
            results = self._bulk_update_oper()

        elif self.object_op == 'bulk_query':
            results = self._bulk_query_oper()

        elif self.object_op == 'delete':
            results = self._delete_oper()

        elif self.object_op == 'read':
            results = self._read_oper()

        elif self.object_op == 'list':
            results = self._list_oper()

        elif self.object_op == 'ref_delete':
            results = self._ref_delete_oper()

        elif self.object_op == 'ref_update':
            results = self._ref_update_oper()

        elif self.object_op == 'bulk_ref_update':
            results = self._bulk_ref_update_oper()

        elif self.object_op == 'fq_name_to_id':
            results = self._fq_name_to_id_oper()

        elif self.object_op == 'id_to_fq_name':
            results = self._id_to_fq_name_oper()
        else:
            raise ValueError(
                "Unsupported operation '%s' for object type '%s'",
                self.object_op, self.object_type)

        return results
    # end do_oper

    def _obtain_vnc_method(self, operation, prepend_object_type=True):
        method_name = self.object_type + operation \
            if prepend_object_type else operation
        method = self._str_to_vnc_method(method_name)
        if method is None:
            raise ValueError(
                "Operation '%s' is not supported on '%s'"
                % (self.object_op, self.object_type))

        return method
    # end _obtain_vnc_method

    def _create_single(self, obj_dict):
        self.object_dict = obj_dict
        method = self._obtain_vnc_method('_create')
        results = dict()
        try:
            if obj_dict.get('uuid') is None:
                obj_dict['uuid'] = None

            # try to read the object, if not present already, then create
            # read is less expensive than trying to create

            read_obj = self._read_oper()

            if read_obj.get('failed'):
                # object is not already created; create now
                instance_obj = self.cls.from_dict(**obj_dict)
                obj_uuid = method(instance_obj)
                results['uuid'] = obj_uuid
            else:
                # This step implies object already present
                if self.update_obj:
                    # Try to update the object, if it already exists and
                    # 'update_obj_if_present' flag is present and if there are
                    # some changes in the object properties
                    results = read_obj.get('obj')

                    # Now compare for every property in object dict,
                    # if the property already exists and the value is the same

                    for key in obj_dict:
                        if key != 'uuid':
                            to_be_upd_value = obj_dict[key]
                            existing_value = results.get(key)
                            if to_be_upd_value != existing_value:
                                results = self._update_single(obj_dict)
                                break
                else:
                    # This is the case where caller does not want to update the
                    # object. Set failed to True to let caller decide
                    results['failed'] = True
                    results['msg'] = \
                        "Failed to create object in database as object "\
                        "exists with same uuid '%s' or fqname '%s'" % \
                        (obj_dict.get('uuid'), obj_dict.get('fq_name'))

        except Exception as ex:
            results['failed'] = True
            results['msg'] = \
                "Failed to create object (uuid='%s', fq_name='%s') in the "\
                "database due to error: %s" % \
                (obj_dict.get('uuid'), obj_dict.get('fq_name'), str(ex))
        return results
    # end _create_single

    def _update_single(self, ob_dict):
        method = self._obtain_vnc_method('_update')
        results = dict()
        try:
            uuid = ob_dict.get('uuid')
            fq_name = ob_dict.get('fq_name')

            if uuid and not fq_name:
                # Get the fq_name from uuid
                ob_dict['fq_name'] = self.vnc_lib.id_to_fq_name(uuid)
            elif fq_name and not uuid:
                ob_dict['uuid'] = None

            instance_obj = self.cls.from_dict(**ob_dict)
            obj = method(instance_obj)
            obj_name = self.object_type.replace('_', '-')
            results['uuid'] = \
                ast.literal_eval(obj).get(obj_name).get('uuid')
        except Exception as ex:
            results['failed'] = True
            results['msg'] = \
                "Failed to update object (uuid='%s', fq_name='%s') in the "\
                "database due to error: %s" % (uuid, fq_name, str(ex))
        return results
    # end _update_single

    def _create_oper(self):
        results = dict()
        self.object_list = [self.object_dict]
        res = self._bulk_create_oper()
        results['uuid'] = res['list_uuids'][0] if res['list_uuids'] else None
        return results
    # end _create_oper

    def _bulk_create_oper(self):
        results = dict()
        results['list_uuids'] = []
        for ob_dict in self.object_list:
            res = self._create_single(ob_dict)
            if res.get('failed'):
                results['failed'] = True
                results['msg'] = res.get('msg')
                break
            else:
                results['list_uuids'].append(res['uuid'])
        return results
    # end _bulk_create_oper

    def _update_oper(self):
        results = dict()
        self.object_list = [self.object_dict]
        res = self._bulk_update_oper()
        results['uuid'] = res['list_uuids'][0] if res['list_uuids'] else None
        return results
    # end _update_oper

    def _bulk_update_oper(self):
        results = dict()
        results['list_uuids'] = []
        for ob_dict in self.object_list:
            res = self._update_single(ob_dict)
            if res.get('failed'):
                results['failed'] = True
                results['msg'] = res.get('msg')
                break
            else:
                results['list_uuids'].append(res['uuid'])
        return results
    # end _bulk_update_oper

    def _delete_oper(self):
        method = self._obtain_vnc_method('_delete')
        results = dict()
        obj_uuid = self.object_dict.get('uuid')
        obj_fq_name = self.object_dict.get('fq_name')

        try:
            if obj_uuid:
                method(id=obj_uuid)
            elif obj_fq_name:
                method(fq_name=obj_fq_name)
            else:
                results['failed'] = True
                results['msg'] = \
                    "Either uuid or fq_name should be present for delete"
        except Exception as ex:
            results['failed'] = True
            results['msg'] = \
                "Failed to delete object (uuid='%s', fq_name='%s') from the "\
                "database due to error: %s" % (obj_uuid, obj_fq_name, str(ex))
        return results
    # end _delete_oper

    def _read_oper(self):
        method = self._obtain_vnc_method('_read')
        results = dict()
        obj_uuid = self.object_dict.get('uuid')
        obj_fq_name = self.object_dict.get('fq_name')
        fields = self.object_dict.get('fields')
        try:
            if obj_uuid:
                obj = method(id=obj_uuid, fields=fields)
            elif obj_fq_name:
                if isinstance(obj_fq_name, list):
                    obj = method(fq_name=obj_fq_name, fields=fields)
                else:
                    # convert str object to list
                    obj = method(fq_name=ast.literal_eval(obj_fq_name),
                                 fields=fields)
            else:
                results['failed'] = True
                results['msg'] = \
                    "Either uuid or fq_name should be present for read"
                return results

            if fields is not None:
                complete_dict_obj = obj.__dict__
                result_obj = self.vnc_lib.obj_to_dict(obj)
                for item in fields:
                    append_value = complete_dict_obj[item]
                    result_obj[item] = append_value
                results['obj'] = result_obj

            else:
                results['obj'] = self.vnc_lib.obj_to_dict(obj)

        except Exception as ex:
            results['failed'] = True
            results['msg'] = \
                "Failed to read object (uuid='%s', fq_name='%s') from the "\
                "database due to error: %s" % (obj_uuid, obj_fq_name, str(ex))
        return results
    # end _read_oper

    def _bulk_query_oper(self):
        results = dict()
        results['list_objects'] = []
        for parent_fqname in self.object_list:
            self.object_dict['parent_fq_name'] = parent_fqname
            res = self._list_oper()
            if res.get('failed'):
                results['failed'] = True
                results['msg'] = res.get('msg')
                break
            else:
                results['list_objects'].append(res)
        return results
    # end _bulk_query_oper

    def _list_oper(self):
        method = self._obtain_vnc_method('s_list')
        results = dict()
        parent_fqname = self.object_dict.get('parent_fq_name')
        filters = self.object_dict.get('filters')
        fields = self.object_dict.get('fields')
        back_ref_id = self.object_dict.get('back_ref_id')
        detail = self.object_dict.get('detail')
        try:
            if detail == 'True':
                objs = method(back_ref_id=back_ref_id,
                              filters=filters,
                              fields=fields,
                              detail=True,
                              parent_fq_name=parent_fqname)
                results['obj'] = []
                for obj in objs:
                    results['obj'].append(self.vnc_lib.obj_to_dict(obj))
            else:
                obj = method(back_ref_id=back_ref_id,
                             filters=filters,
                             fields=fields,
                             parent_fq_name=parent_fqname)
                results['obj'] = obj
        except Exception as ex:
            results['failed'] = True
            results['msg'] = \
                "Failed to list objects due to error: %s" % str(ex)
        return results
    # end _list_oper

    def _bulk_ref_update_oper(self):
        results = dict()
        results['refs_upd_resp'] = []
        results['failed_uuids'] = []
        for obj_ref_pair in self.object_list:
            self.object_dict['fq_name'] = obj_ref_pair[0]
            local_uuid = self._fq_name_to_id_oper()
            self.object_dict['obj_uuid'] = local_uuid.get('uuid')
            self.object_dict['ref_fqname'] = obj_ref_pair[1]
            res = self._ref_update_oper()
            if res.get('failed'):
                results['failed'] = False
                results['failed_uuids'].append(res.get('msg'))
            else:
                results['refs_upd_resp'].append(res)
        return results
    # end _bulk_ref_update_oper

    def _ref_update_oper(self):
        results = dict()
        method = self._obtain_vnc_method('ref_update', False)

        # Get the input params from the object dict
        obj_type = self.object_type.replace('_', '-')
        obj_uuid = self.object_dict.get('obj_uuid')
        ref_type = self.object_dict.get('ref_type')
        ref_uuid = self.object_dict.get('ref_uuid')
        ref_fqname = self.object_dict.get('ref_fqname')

        try:
            obj_uuid = method(obj_type, obj_uuid, ref_type,
                              ref_uuid, ref_fqname, 'ADD')
            results['uuid'] = obj_uuid
        except Exception as ex:
            results['failed'] = True
            results['msg'] = \
                "Failed to update ref (%s, %s) -> (%s, %s, %s) " \
                "due to error: %s"\
                % (obj_type, obj_uuid, ref_type, ref_uuid,
                   ref_fqname, str(ex))
        return results
    # _ref_update_oper

    def _ref_delete_oper(self):
        results = dict()
        method = self._obtain_vnc_method('ref_delete', False)

        # Get the input params from the object dict
        obj_type = self.object_type.replace('_', '-')
        obj_uuid = self.object_dict.get('obj_uuid')
        ref_type = self.object_dict.get('ref_type')
        ref_uuid = self.object_dict.get('ref_uuid')
        ref_fqname = self.object_dict.get('ref_fqname')

        try:

            obj_uuid = method(obj_type, obj_uuid, ref_type,
                              ref_uuid, ref_fqname, 'DELETE')
            results['uuid'] = obj_uuid
        except Exception as ex:
            results['failed'] = True
            results['msg'] = \
                "Failed to delete ref (%s, %s) -> (%s, %s, %s) " \
                "due to error: %s"\
                % (obj_type, obj_uuid, ref_type, ref_uuid,
                   ref_fqname, str(ex))
        return results
    # _ref_delete_oper

    def _fq_name_to_id_oper(self):
        method = self._obtain_vnc_method('fq_name_to_id', False)
        results = dict()
        try:
            obj_type = self.object_type.replace('_', '-')
            if isinstance(self.object_dict.get('fq_name'), list):
                obj_fq_name = self.object_dict.get('fq_name')
            else:
                # convert str object to list
                obj_fq_name = ast.literal_eval(self.object_dict.get('fq_name'))
            obj_uuid = method(obj_type, obj_fq_name)
            results['uuid'] = obj_uuid
        except Exception as ex:
            results['failed'] = True
            results['msg'] = \
                "Failed to retrieve uuid for (%s, %s) due to error: %s"\
                % (obj_type, obj_fq_name, str(ex))
        return results
    # _fq_name_to_id_oper

    def _id_to_fq_name_oper(self):
        method = self._obtain_vnc_method(self.object_op, False)
        results = dict()
        try:
            obj_uuid = self.object_dict.get('uuid')
            obj_fq_name = method(obj_uuid)
            results['fq_name'] = obj_fq_name
        except Exception as ex:
            results['failed'] = True
            results['msg'] = \
                "Failed to retrive fq_name by uuid '%s' due to error: %s"\
                % (obj_uuid, str(ex))
        return results
    # end _id_to_fq_name_oper

    def _str_to_class(self, cls_name):
        return getattr(vnc_api.gen.resource_client, cls_name, None)
    # end _str_to_class

    def _str_to_vnc_method(self, method_name):
        return getattr(self.vnc_lib, method_name, None)
    # end _str_to_vnc_method
# end class VncMod


def module_process(module, args):
    module.send_job_object_log(
        args[0], VncMod.JOB_IN_PROGRESS, None)


def main():
    """Module main."""
    # Create the module instance
    module = FabricAnsibleModule(
        argument_spec=dict(
            job_ctx=dict(type='dict', required=True),
            object_type=dict(type='str', required=True),
            object_op=dict(
                type='str', required=True,
                choices=[
                    'create', 'update', 'delete', 'read', 'list',
                    'bulk_create', 'bulk_update', 'bulk_query',
                    'ref_update', 'ref_delete', 'bulk_ref_update',
                    'fq_name_to_id', 'id_to_fq_name'
                ]
            ),
            object_dict=dict(type='dict', required=False),
            object_list=dict(type='list', required=False),
            update_obj_if_present=dict(
                type='bool', required=False, default=True
            ),
            enable_job_ctx=dict(type='bool', required=False, default=True)
        ),
        supports_check_mode=True
    )

    try:
        # vnc operations
        vnc_mod = VncMod(module)
        results = vnc_mod.do_oper()
    except Exception as ex:
        results = dict(failed=True, msg=str(ex))

    if results.get('failed'):
        module.logger.error(results.get('msg'))

        enable_job_ctx = module.params['enable_job_ctx']

        # log to sandesh only when enable_job_ctx is set to true
        if enable_job_ctx:
            module.execute(module_process, results.get('msg'))

    # Return response
    module.exit_json(**results)


if __name__ == '__main__':
    main()
