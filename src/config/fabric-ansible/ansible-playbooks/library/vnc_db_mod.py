#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of wrapper module for VNC operations
such as add, delete, update, read, list, ref-update, ref-delete,
fqname_to_id, id_to_fqname
"""

import uuid
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
    results['failure'] = False
    vnc_lib = None
    retry = 10

    # Fetch module params
    object_type = module.params['object_type']
    object_op = module.params['object_op']
    object_dict = module.params['object_dict']
    auth_token = module.params['auth_token']
    update_obj = module.params['update_obj_if_present']

    # Instantiate the VNC library
    # Retry for sometime, till API server is up
    connected = False
    while retry and not connected:
        try:
            retry -= 1
            vnc_lib = VncApi(auth_token=auth_token)
            connected = True
        except Exception as ex:
            time.sleep(10)

    if vnc_lib is None:
        results['msg'] = "Failed to instantiate vnc api"
        results['failure'] = True
        return results

    # Get the class name from object type
    cls_name = camelize(object_type)

    # Get the class object
    cls = str_to_class(cls_name)
    if cls is None:
        results['msg'] = "Failed to get class for %s", cls_name
        results['failure'] = True
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
        results['failure'] = True
        return results

    # This creates an object, if not existing, else update the object
    if object_op == 'create':
        try:
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
                    object_dict['fq_name'] = "None"
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
                results['msg'] = str(ex)
                results['failure'] = True
        except Exception as ex:
            results['uuid'] = None
            results['msg'] = str(ex)
            results['failure'] = True

    elif object_op == 'update':
        try:
            uuid = object_dict.get('uuid')
            fq_name = object_dict.get('fq_name')

            if uuid and not fq_name:
                object_dict['fq_name'] = "None"
            elif fq_name and not uuid:
                object_dict['uuid'] = None

            instance_obj = cls.from_dict(**object_dict)
            obj = method(instance_obj)
            obj_name = object_type.replace('_', '-')
            results['uuid'] = ast.literal_eval(obj).get(obj_name).get('uuid')
        except Exception as ex:
            results['uuid'] = None
            results['msg'] = str(ex)
            results['failure'] = True

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
            results['failure'] = True

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
                results['failure'] = True
                return results

            results['obj'] = vnc_lib.obj_to_dict(obj)
        except Exception as ex:
            results['obj'] = None
            results['msg'] = str(ex)
            results['failure'] = True

    elif object_op == 'list':
        filters = object_dict.get('filters')
        fields = object_dict.get('fields')
        try:
            if filters and fields:
                obj = method(filters=filters, fields=fields)
            elif fields:
                obj = method(fields=fields)
            elif filters:
                obj = method(filters=filters)
            else:
                obj = method()
            results['obj'] = obj
        except Exception as ex:
            results['obj'] = None
            results['msg'] = str(ex)
            results['failure'] = True

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
            results['failure'] = True

    elif object_op == 'fq_name_to_id':
        try:
            obj_type = object_type.replace('_', '-')
            obj_fq_name = ast.literal_eval(object_dict.get('fq_name'))
            obj_uuid = method(obj_type, obj_fq_name)
            results['uuid'] = obj_uuid
        except Exception as ex:
            results['uuid'] = None
            results['msg'] = str(ex)
            results['failure'] = True

    elif object_op == 'id_to_fq_name':
        try:
            obj_uuid = object_dict.get('uuid')
            obj_fq_name = method(obj_uuid)
            results['fq_name'] = obj_fq_name
        except Exception as ex:
            results['msg'] = str(ex)
            results['failure'] = True

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
                default=True)),
        supports_check_mode=True,
    )

    logging.basicConfig(filename="/tmp/ans.log", level=logging.DEBUG)
    results = vnc_crud(module)

    # Return response
    module.exit_json(**results)


if __name__ == '__main__':
    main()
