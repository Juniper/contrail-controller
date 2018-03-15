#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of wrapper module for VNC operations
such as add, delete, update, read, list, ref-update, ref-delete,
fqname_to_id, id_to_fqname
"""

DOCUMENTATION = '''
---
Performs CRUD operation for the objects in database. Following operations are
supported - ADD, DELETE, UPDATE, READ, LIST, REF-UPDATE, REF-DELETE,
FQNAME_TO_ID, ID_TO_FQNAME

For CREATE operation, if 'object_obj_if_present' flag is set to True (default value),
module tries to update the existing object. If this flag is set to False, module
will return SUCCESS with existing uuid

LIST operation is supported with filters/fields/back_ref_id and detail clause
'''

EXAMPLES = '''
CREATE operation:
    vnc_db_mod:
        object_type: "physical_router"
        object_op: "create"
        object_dict: |
          {
              "parent_type": "global-system-config",
              "fq_name": ["default-global-system-config", "mx-240"],
              "physical_router_management_ip": "172.10.68.1",
              "physical_router_vendor_name": "Juniper",
              "physical_router_product_name": "MX",
              "physical_router_device_family": "juniper-mx"
           }
        auth_token: "820ac4ea583c47959c6b3d42e7b829b3"
        api_server_host: "localhost"

UPDATE opreation:
    vnc_db_mod:
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
        auth_token: "820ac4ea583c47959c6b3d42e7b829b3"
        api_server_host: "localhost"

READ opreration:
    vnc_db_mod:
         object_type: "fabric"
         object_op: "read"
         object_dict: |
         {
              "fq_name": "{{ fabric_fq_name }}"
         }
         auth_token: "820ac4ea583c47959c6b3d42e7b829b3"
         api_server_host: "localhost"

REF-UPDATE opreation:
    vnc_db_mod:
        object_type: "fabric"
        object_op: "ref_update"
        object_dict: |
          {
              "obj_uuid": "{{ fabric_uuid }}",
              "ref_type": "physical_router",
              "ref_uuid": "{{ item.uuid }}"
          }
        auth_token: "820ac4ea583c47959c6b3d42e7b829b3"
        api_server_host: "locahost"

ID_TO_FQNAME operation:
    vnc_db_mod:
         object_type: "fabric"
         object_op: "id_to_fq_name"
         object_dict: |
           {
              "uuid": "{{ fabric_uuid }}"
           }
        auth_token: "820ac4ea583c47959c6b3d42e7b829b3"
        api_server_host: "locahost"

FQNAME_TO_ID operation:
    vnc_db_mod:
         object_type: "tag"
         object_op: "fq_name_to_id"
         object_dict: |
           {
              "fq_name": ["{{ tag_fq_name }}"]
           }
        auth_token: "820ac4ea583c47959c6b3d42e7b829b3"
        api_server_host: "locahost"

LIST with filters and detail operation:
    vnc_db_mod:
        object_type: "physical_router"
        object_op: "list"
        object_dict: |
        {
            "filters": {"physical_router_management_ip":"{{ item.hostname }}"},
            "detail": "True",
        }
        auth_token: "820ac4ea583c47959c6b3d42e7b829b3"
        api_server_host: "locahost"
'''

import sys
import requests
import time
import ast
from inflection import camelize
from vnc_api.vnc_api import *
from vnc_api.gen.resource_client import *
from cfgm_common.exceptions import (
    TimeOutError,
    RefsExistError,
    NoIdError,
    ServiceUnavailableError,
    AuthFailed,
    ResourceExhaustionError)
from ansible.module_utils.basic import AnsibleModule
import logging


def vnc_crud(module):

    results = {}
    results['failed'] = False
    vnc_lib = None
    retry = 10

    # Fetch module params
    object_type = module.params['object_type']
    object_op = module.params['object_op']
    object_dict = module.params['object_dict']
    auth_token = module.params['auth_token']
    update_obj = module.params['update_obj_if_present']
    api_server_host = module.params['api_server_host']
 
    # Instantiate the VNC library
    # Retry for sometime, till API server is up
    connected = False
    while retry and not connected:
        try:
            retry -= 1
            vnc_lib = VncApi(auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY, auth_token=auth_token)
            connected = True
        except Exception as ex:
            time.sleep(10)

    if vnc_lib is None:
        results['msg'] = "Failed to instantiate vnc api"
        results['failed'] = True
        return results

    # Get the class name from object type
    cls_name = camelize(object_type)

    # Get the class object
    cls = str_to_class(cls_name)
    if cls is None:
        results['msg'] = "Failed to get class for %s", cls_name
        results['failed'] = True
        return results

    # Get the vnc method name based on the object type
    if object_op == 'create' or object_op == 'delete' or \
       object_op == 'update' or object_op == 'read':
        method_name = object_type + '_' + object_op
    elif object_op == 'list':
        method_name = object_type + 's_' + object_op
    else:
        method_name = object_op

    method = str_to_vnc_method(vnc_lib, method_name)
    if method is None:
        results['msg'] = "Failed to get class method for %s", method_name
        results['failed'] = True
        return results

    # This creates an object, if not existing, else update the object
    if object_op == 'create':
        try:
            if object_dict.get('uuid') is None:
                object_dict['uuid'] = None
            instance_obj = cls.from_dict(**object_dict)
            obj_uuid = method(instance_obj)
            results['uuid'] = obj_uuid
        except RefsExistError as ex:
            if update_obj:
                # Try to update the object, if already exist and
                # 'update_obj_if_present' flag is present
                uuid = object_dict.get('uuid')
                fq_name = object_dict.get('fq_name')

                if uuid and not fq_name:
                    # Get the fq_name from uuid
                    object_dict['fq_name'] = vnc_lib.id_to_fq_name(uuid)
                elif fq_name and not uuid:
                    object_dict['uuid'] = None

                instance_obj = cls.from_dict(**object_dict)
                method_name = object_type + '_update'
                method = getattr(vnc_lib, method_name)
                obj = method(instance_obj)
                obj_name = object_type.replace('_', '-')
                # update method return unicode object, needs to convert it into
                # a dictionary to get uuid
                results['uuid'] = ast.literal_eval(
                    obj).get(obj_name).get('uuid')
            else:
                # This is the case where caller does not want to update the
                # object. Set failed to True to let caller decide
                results['msg'] = str(ex)
                results['failed'] = True
        except Exception as ex:
            results['uuid'] = None
            results['msg'] = str(ex)
            results['failed'] = True

    elif object_op == 'update':
        try:
            uuid = object_dict.get('uuid')
            fq_name = object_dict.get('fq_name')

            if uuid and not fq_name:
                # Get the fq_name from uuid
                object_dict['fq_name'] = vnc_lib.id_to_fq_name(uuid)
            elif fq_name and not uuid:
                object_dict['uuid'] = None

            instance_obj = cls.from_dict(**object_dict)
            obj = method(instance_obj)
            obj_name = object_type.replace('_', '-')
            results['uuid'] = ast.literal_eval(obj).get(obj_name).get('uuid')
        except Exception as ex:
            results['uuid'] = None
            results['msg'] = str(ex)
            results['failed'] = True

    elif object_op == 'delete':
        obj_uuid = object_dict.get('uuid')
        obj_fq_name = object_dict.get('fq_name')

        try:
            if obj_uuid:
                method(id=obj_uuid)
            elif obj_fq_name:
                method(fq_name=obj_fq_name)
            else:
                results['msg'] = "Either uuid or fq_name should be present \
                                 for delete"
                results['failure'] = True
        except Exception as ex:
            results['msg'] = str(ex)
            results['failed'] = True

    elif object_op == 'read':
        obj_uuid = object_dict.get('uuid')
        obj_fq_name = object_dict.get('fq_name')

        try:
            if obj_uuid:
                obj = method(id=obj_uuid)
            elif obj_fq_name:
                if type(obj_fq_name) is list:
                    obj = method(fq_name=obj_fq_name)
                else:
                    # convert str object to list
                    obj = method(fq_name=ast.literal_eval(obj_fq_name))
            else:
                results['msg'] = "Either uuid or fq_name should be present \
                                 for read"
                results['failed'] = True
                return results

            results['obj'] = vnc_lib.obj_to_dict(obj)
        except Exception as ex:
            results['obj'] = None
            results['msg'] = str(ex)
            results['failed'] = True

    elif object_op == 'list':
        filters = object_dict.get('filters')
        fields = object_dict.get('fields')
        back_ref_id = object_dict.get('back_ref_id')
        detail = object_dict.get('detail')
        try:
            if detail == 'True':
                obj = method(back_ref_id=back_ref_id,
                             filters=filters,
                             fields=fields,
                             detail=True)
                results['obj'] = []
                for object in obj:
                    results['obj'].append(vnc_lib.obj_to_dict(object))
            else:
                obj = method(back_ref_id=back_ref_id,
                             filters=filters,
                             fields=fields)
                results['obj'] = obj
        except Exception as ex:
            results['obj'] = None
            results['msg'] = str(ex)
            results['failed'] = True

    elif object_op == 'ref_update' or object_op == 'ref_delete':
        # Get the input params from the object dict
        obj_type = object_type.replace('_', '-')
        obj_uuid = object_dict.get('obj_uuid')
        ref_type = object_dict.get('ref_type')
        ref_uuid = object_dict.get('ref_uuid')
        ref_fqname = object_dict.get('ref_fqname')

        try:
            if object_op == 'ref_update':
                obj_uuid = method(obj_type, obj_uuid, ref_type,
                                  ref_uuid, ref_fqname, 'ADD')
            else:
                obj_uuid = method(obj_type, obj_uuid, ref_type,
                                  ref_uuid, ref_fqname, 'DELETE')
            results['uuid'] = obj_uuid
        except Exception as ex:
            results['msg'] = str(ex)
            results['failed'] = True

    elif object_op == 'fq_name_to_id':
        try:
            obj_type = object_type.replace('_', '-')
            if type(object_dict.get('fq_name')) is list:
                obj_fq_name = object_dict.get('fq_name')
            else:
                # convert str object to list
                obj_fq_name = ast.literal_eval(object_dict.get('fq_name'))
            obj_uuid = method(obj_type, obj_fq_name)
            results['uuid'] = obj_uuid
        except Exception as ex:
            results['uuid'] = None
            results['msg'] = str(ex)
            results['failed'] = True

    elif object_op == 'id_to_fq_name':
        try:
            obj_uuid = object_dict.get('uuid')
            obj_fq_name = method(obj_uuid)
            results['fq_name'] = obj_fq_name
        except Exception as ex:
            results['msg'] = str(ex)
            results['failed'] = True

    return results
# end vnc_crud

def str_to_class(input):
    return getattr(vnc_api.gen.resource_client, input, None)
# end str_to_class

def str_to_vnc_method(vnc_lib, method_name):
    return getattr(vnc_lib, method_name, None)
# end str_to_vnc_method

def main():
    # Create the module instance
    module = AnsibleModule(
        argument_spec=dict(
            object_type=dict(
                required=True,
                type='str'),
            object_op=dict(
                required=True,
                type='str',
                choices=[
                    'create',
                    'update',
                    'delete',
                    'read',
                    'list',
                    'ref_update',
                    'ref_delete',
                    'fq_name_to_id',
                    'id_to_fq_name']),
            object_dict=dict(
                required=False,
                type='dict'),
            auth_token=dict(
                required=True),
            update_obj_if_present=dict(
                required=False,
                type='bool',
                default=True),
            api_server_host=dict(
                required=False,
                default="localhost")),
        supports_check_mode=True,
    )

    logging.basicConfig(filename="/tmp/ans.log", level=logging.DEBUG)
    results = vnc_crud(module)

    # Return response
    module.exit_json(**results)

if __name__ == '__main__':
    main()
