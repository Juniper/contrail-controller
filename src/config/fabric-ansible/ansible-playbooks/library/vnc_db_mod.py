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

BULK CREATE operation:
    vnc_db_mod:
        object_type: "physical_router"
        object_op: "bulk_create"
        object_list:
            [   {
                  "parent_type": "global-system-config",
                  "fq_name": ["default-global-system-config", "mx-240"],
                  "physical_router_management_ip": "172.10.68.1",
                  "physical_router_vendor_name": "Juniper",
                  "physical_router_product_name": "MX",
                  "physical_router_device_family": "juniper-mx"
               },
               {
                  "parent_type": "global-system-config",
                  "fq_name": ["default-global-system-config", "mx-240-2"],
                  "physical_router_management_ip": "172.10.68.2",
                  "physical_router_vendor_name": "Juniper",
                  "physical_router_product_name": "MX",
                  "physical_router_device_family": "juniper-mx"
               }
           ]
        auth_token: "820ac4ea583c47959c6b3d42e7b829b3"
        api_server_host: "localhost"

UPDATE operation:
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

BULK UPDATE operation:
    vnc_db_mod:
         object_type: "physical_router"
         object_op: "bulk_update"
         object_list: |
          [ {
                  "uuid": "{{ item[0].obj['physical-routers'][0].uuid }}",
                  "physical_router_user_credentials": {
                      "username": "{{ item.1.username }}",
                      "password": "{{ item.1.password }}"
                      }
            }
          ]
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
        api_server_host: "localhost"

ID_TO_FQNAME operation:
    vnc_db_mod:
         object_type: "fabric"
         object_op: "id_to_fq_name"
         object_dict: |
           {
              "uuid": "{{ fabric_uuid }}"
           }
        auth_token: "820ac4ea583c47959c6b3d42e7b829b3"
        api_server_host: "localhost"

FQNAME_TO_ID operation:
    vnc_db_mod:
         object_type: "tag"
         object_op: "fq_name_to_id"
         object_dict: |
           {
              "fq_name": ["{{ tag_fq_name }}"]
           }
        auth_token: "820ac4ea583c47959c6b3d42e7b829b3"
        api_server_host: "localhost"

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
        api_server_host: "localhost"
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
from ansible.module_utils.fabric_utils import FabricAnsibleModule
import logging


class VncMod(object):
    def __init__(self, module):
        self.vnc_lib = None
        retry = 10

        # Fetch module params
        auth_token = module.params['auth_token']
        api_server_host = module.params['api_server_host']
        self.object_type = module.params['object_type']
        self.object_op = module.params['object_op']
        self.object_dict = module.params['object_dict']
        self.object_list = module.params['object_list']
        self.update_obj = module.params['update_obj_if_present']

        # Instantiate the VNC library
        # Retry for sometime, till API server is up
        connected = False
        while retry and not connected:
            try:
                retry -= 1
                self.vnc_lib = VncApi(auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
                                      auth_token=auth_token)
                connected = True
            except Exception as ex:
                time.sleep(10)

    def do_oper(self):
        results = dict()
        results['failed'] = False
        if self.vnc_lib is None:
            results['msg'] = "Failed to instantiate vnc api"
            results['failed'] = True
            return results

        # Get the class name from object type
        cls_name = camelize(self.object_type)

        # Get the class object
        self.cls = self.str_to_class(cls_name)

        if self.cls is None:
            results['msg'] = "Failed to get class for %s", cls_name
            results['failed'] = True
            return results

        # Get the vnc method name based on the object type
        if self.object_op == 'create' or self.object_op == 'delete' or \
                        self.object_op == 'update' or self.object_op == 'read':
            method_name = self.object_type + '_' + self.object_op
        elif self.object_op == 'bulk_create':
            method_name = self.object_type + '_create'
        elif self.object_op == 'bulk_update':
            method_name = self.object_type + '_update'
        elif self.object_op == 'list':
            method_name = self.object_type + 's_' + self.object_op
        else:
            method_name = self.object_op

        self.method = self.str_to_vnc_method(method_name)
        if self.method is None:
            results['msg'] = "Failed to get class method for %s", \
                                  method_name
            results['failed'] = True
            return results

        # This creates an object, if not existing, else update the object
        if self.object_op == 'create':
            results = self.create_oper()

        elif self.object_op == 'update':
            results = self.update_oper()

        elif self.object_op == 'bulk_create':
            results = self.bulk_create_oper()

        elif self.object_op == 'bulk_update':
            results = self.bulk_update_oper()

        elif self.object_op == 'delete':
            results = self.delete_oper()

        elif self.object_op == 'read':
            results = self.read_oper()

        elif self.object_op == 'list':
            results = self.list_oper()

        elif self.object_op == 'ref_update' or self.object_op == 'ref_delete':
            results = self.ref_update_delete_oper()

        elif self.object_op == 'fq_name_to_id':
            results = self.fq_name_to_id_oper()

        elif self.object_op == 'id_to_fq_name':
            results = self.id_to_fq_name_oper()

        return results

    def _create_single(self, obj_dict):
        results = dict()
        try:
            if obj_dict.get('uuid') is None:
                obj_dict['uuid'] = None
            instance_obj = self.cls.from_dict(**obj_dict)
            obj_uuid = self.method(instance_obj)
            results['uuid'] = obj_uuid
        except RefsExistError as ex:
            if self.update_obj:
                # Try to update the object, if already exist and
                # 'update_obj_if_present' flag is present
                method_name = self.object_type + '_update'
                self.method = getattr(self.vnc_lib, method_name)
                results = self._update_single(obj_dict)
            else:
                # This is the case where caller does not want to update the
                # object. Set failed to True to let caller decide
                results['msg'] = str(ex)
                results['failed'] = True
        except Exception as ex:
            results['uuid'] = None
            results['msg'] = str(ex)
            results['failed'] = True
        return results

    def _update_single(self, ob_dict):
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
            obj = self.method(instance_obj)
            obj_name = self.object_type.replace('_', '-')
            results['uuid'] = ast.literal_eval(obj).get(obj_name).get(
                'uuid')
        except Exception as ex:
            results['uuid'] = None
            results['msg'] = str(ex)
            results['failed'] = True
        return results

    def create_oper(self):
        results = dict()
        self.object_list = [self.object_dict]
        res = self.bulk_create_oper()
        results['uuid'] = res['list_uuids'][0] if len(res['list_uuids']) else None
        return results

    def bulk_create_oper(self):
        results = dict()
        results['list_uuids'] = []
        for ob_dict in self.object_list:
            res = self._create_single(ob_dict)
            if res.get('failed'):
                results['msg'] = res.get('msg')
                break
            else:
                results['list_uuids'].append(res['uuid'])
        return results

    def update_oper(self):
        results = dict()
        self.object_list = [self.object_dict]
        res = self.bulk_update_oper()
        results['uuid'] = res['list_uuids'][0] if len(res['list_uuids']) else None
        return results

    def bulk_update_oper(self):
        results = dict()
        results['list_uuids'] = []
        for ob_dict in self.object_list:
            res = self._update_single(ob_dict)
            if res.get('failed'):
                results['msg'] = res.get('msg')
                break
            else:
                results['list_uuids'].append(res['uuid'])
        return results

    def delete_oper(self):
        results = dict()
        obj_uuid = self.object_dict.get('uuid')
        obj_fq_name = self.object_dict.get('fq_name')

        try:
            if obj_uuid:
                self.method(id=obj_uuid)
            elif obj_fq_name:
                self.method(fq_name=obj_fq_name)
            else:
                results['msg'] = "Either uuid or fq_name should be " \
                                      "present for delete"
                results['failure'] = True
        except Exception as ex:
            results['msg'] = str(ex)
            results['failed'] = True
        return results

    def read_oper(self):
        results = dict()
        obj_uuid = self.object_dict.get('uuid')
        obj_fq_name = self.object_dict.get('fq_name')

        try:
            if obj_uuid:
                obj = self.method(id=obj_uuid)
            elif obj_fq_name:
                if type(obj_fq_name) is list:
                    obj = self.method(fq_name=obj_fq_name)
                else:
                    # convert str object to list
                    obj = self.method(fq_name=ast.literal_eval(obj_fq_name))
            else:
                results['msg'] = "Either uuid or fq_name should be " \
                                      "present for read"
                results['failed'] = True
                return results

            results['obj'] = self.vnc_lib.obj_to_dict(obj)
        except Exception as ex:
            results['obj'] = None
            results['msg'] = str(ex)
            results['failed'] = True
        return results

    def list_oper(self):
        results = dict()
        filters = self.object_dict.get('filters')
        fields = self.object_dict.get('fields')
        back_ref_id = self.object_dict.get('back_ref_id')
        detail = self.object_dict.get('detail')
        try:
            if detail == 'True':
                obj = self.method(back_ref_id=back_ref_id,
                                  filters=filters,
                                  fields=fields,
                                  detail=True)
                results['obj'] = []
                for object in obj:
                    results['obj'].append(self.vnc_lib.obj_to_dict(object))
            else:
                obj = self.method(back_ref_id=back_ref_id,
                                  filters=filters,
                                  fields=fields)
                results['obj'] = obj
        except Exception as ex:
            results['obj'] = None
            results['msg'] = str(ex)
            results['failed'] = True
        return results

    def ref_update_delete_oper(self):
        results = dict()
        # Get the input params from the object dict
        obj_type = self.object_type.replace('_', '-')
        obj_uuid = self.object_dict.get('obj_uuid')
        ref_type = self.object_dict.get('ref_type')
        ref_uuid = self.object_dict.get('ref_uuid')
        ref_fqname = self.object_dict.get('ref_fqname')

        try:
            if self.object_op == 'ref_update':
                obj_uuid = self.method(obj_type, obj_uuid, ref_type,
                                       ref_uuid, ref_fqname, 'ADD')
            else:
                obj_uuid = self.method(obj_type, obj_uuid, ref_type,
                                       ref_uuid, ref_fqname, 'DELETE')
                results['uuid'] = obj_uuid
        except Exception as ex:
            results['msg'] = str(ex)
            results['failed'] = True
        return results

    def fq_name_to_id_oper(self):
        results = dict()
        try:
            obj_type = self.object_type.replace('_', '-')
            if type(self.object_dict.get('fq_name')) is list:
                obj_fq_name = self.object_dict.get('fq_name')
            else:
                # convert str object to list
                obj_fq_name = ast.literal_eval(self.object_dict.get('fq_name'))
            obj_uuid = self.method(obj_type, obj_fq_name)
            results['uuid'] = obj_uuid
        except Exception as ex:
            results['uuid'] = None
            results['msg'] = str(ex)
            results['failed'] = True
        return results

    def id_to_fq_name_oper(self):
        results = dict()
        try:
            obj_uuid = self.object_dict.get('uuid')
            obj_fq_name = self.method(obj_uuid)
            results['fq_name'] = obj_fq_name
        except Exception as ex:
            results['msg'] = str(ex)
            results['failed'] = True
        return results

    def str_to_class(self, input):
        return getattr(vnc_api.gen.resource_client, input, None)

    # end str_to_class

    def str_to_vnc_method(self, method_name):
        return getattr(self.vnc_lib, method_name, None)
        # end str_to_vnc_method


def main():
    # Create the module instance
    module = FabricAnsibleModule(
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
                    'bulk_create',
                    'bulk_update',
                    'ref_update',
                    'ref_delete',
                    'fq_name_to_id',
                    'id_to_fq_name']),
            object_dict=dict(
                required=False,
                type='dict'),
            object_list=dict(required=False, type='list'),
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
    results = VncMod(module).do_oper()

    # Return response
    module.exit_json(**results)


if __name__ == '__main__':
    main()

