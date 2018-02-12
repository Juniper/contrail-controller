#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of wrapper module for VNC operations such as
ADD, DELETE, READ, UPDATE, REF-UPDATE, REF-DELETE
"""

import uuid
import sys
import requests
import time
import ast
from inflection import camelize
from vnc_api.vnc_api import *
from vnc_api.gen.resource_client import *
from cfgm_common.exceptions import (TimeOutError, RefsExistError, NoIdError,
                 ServiceUnavailableError, AuthFailed, ResourceExhaustionError)
from ansible.module_utils.basic import AnsibleModule
import logging

def vnc_crud(module):

     results = {}
     results['failed'] = False

     # Fetch module params
     object_type = module.params['object_type']
     object_op = module.params['object_op']
     object_dict = module.params['object_dict']
     auth_token = module.params['auth_token']

     # Instantiate the VNC library
     # Retry till API server is up
     connected = False
     while not connected:
        try:
           vnc_lib = VncApi(auth_token=auth_token)
           connected = True
        except requests.exceptions.ConnectionError as e:
           time.sleep(3)
        except ResourceExhaustionError:  # haproxy throws 503
           time.sleep(3)

     # Get the class name from object type
     class_name = camelize(object_type)

     # Get the class object
     instance = str_to_class(class_name)
     if instance is None:
         results['msg'] = "Failed to get class instance for %s", class_name
         results['failed'] = True
         return results

     # Get the vnc method name based on the object type
     if object_op == 'ref_update' or object_op == 'ref_delete':
         method_name = object_op
     elif object_op == 'list':
         method_name = object_type + 's_' + object_op
     else:
         method_name = object_type + '_' + object_op

     method = str_to_vnc_method(vnc_lib, method_name)
     if method is None:
         results['msg'] = "Failed to get class method for %s", method_name
         results['failed'] = True
         return results

     # This creates an object, if not existing, else update the object
     if object_op == 'create':
         try:
            # Try to see if object is already present
            read_method_name = object_type + '_read'
            method = getattr(vnc_lib,  read_method_name)
            uuid = object_dict.get('uuid')
            fq_name = object_dict.get('fq_name')

            if uuid is None and fq_name is None:
                obj = method(id="None")
            else:
                if uuid and not fq_name:
                    obj = method(id=uuid)
                    object_dict['fq_name'] = "None"
                elif fq_name and not uuid:
                    obj = method(fq_name=fq_name)
                    object_dict['uuid'] = None
            # Try to update the object
            instance_obj = instance.from_dict(**object_dict)
            method_name = object_type + '_update'
            method = getattr(vnc_lib,  method_name)
            obj = method(instance_obj)
            obj_name = object_type.replace('_', '-')
            results['uuid'] = ast.literal_eval(obj).get(obj_name).get('uuid')
         except NoIdError:
            # Try to create the object
            try:
               object_dict['uuid'] = None
               instance_obj = instance.from_dict(**object_dict)
               method = getattr(vnc_lib,  method_name)
               obj_uuid = method(instance_obj)
               results['uuid'] = obj_uuid
            except RefsExistError as ex:
               results['msg'] = str(ex)
               results['failed'] = False
         except Exception as ex:
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
                results['failed'] = False
         except NoIdError as ex:
            results['msg'] = str(ex)
            results['failed'] = False
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
                obj = method(fq_name=obj_fq_name)
            else:
                results['msg'] = "Either uuid or fq_name should be present \
                                  for read"
                results['failed'] = False
                return results

            results['obj'] = vnc_lib.obj_to_dict(obj)
         except NoIdError as ex:
            results['msg'] = str(ex)
            results['failed'] = False
         except Exception as ex:
            results['msg'] = str(ex)
            results['failed'] = True

     elif object_op == 'list':
         filter = object_dict.get('filters')
         try:
            if filter:
                obj = method(filters=filter)
            else:
                obj = method()
            results['obj'] = obj
         except Exception as ex:
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

     return results
# end vnc_crud

def str_to_class(input):
    try:
        instance = getattr(vnc_api.gen.resource_client, input)
    except AttributeError:
        instance = None
    return instance
# end str_to_class

def str_to_vnc_method(vnc_lib, method_name):
    try:
        method = getattr(vnc_lib, method_name)
    except AttributeError:
        method = None
    return method
# end str_to_vnc_method

def main():
    # Create the module instance
    module = AnsibleModule(
        argument_spec = dict(
            object_type = dict(required=True, type='str'),
            object_op = dict(required=True, type='str',
                             choices=['create', 'delete', 'read', 'list',
                                      'ref_update', 'ref_delete']),
            object_dict = dict(required=False, type='dict'),
            auth_token = dict(required=True)
        ),
        supports_check_mode=True,
    )

    logging.basicConfig(filename="/tmp/ans.log", level=logging.DEBUG)
    results = vnc_crud(module)

    # Return response
    module.exit_json(**results)

if __name__ == '__main__':
    main()
