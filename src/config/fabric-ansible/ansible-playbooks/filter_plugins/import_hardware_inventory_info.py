#!/usr/bin/python

from builtins import str
from builtins import object
import json
import sys
import traceback

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
from filter_utils import _task_done, _task_error_log, _task_log, FilterLog
from vnc_api.exceptions import NoIdError

from job_manager.job_utils import JobVncApi


class FilterModule(object):

    def filters(self):
        return {
            'import_hardware_inventory_info':
                self.import_hardware_inventory_info,
        }
    # end filters

    def _instantiate_filter_log_instance(self, device_name):
        FilterLog.instance("Import_hardware_inventory_info_Filter",
                           device_name)
    # end _instantiate_filter_log_instance

    def import_hardware_inventory_info(self, job_ctx, prouter_name,
                                       pr_product_name, prouter_vendor,
                                       hardware_inventory_payload):
        r"""Import Hardware Inventory.

        :param job_ctx: Dictionary
            example:
            {
                "auth_token": "EB9ABC546F98",
                "job_input": {
                    "fabric_fq_name": [
                        "default-global-system-config",
                        "fab01"
                    ],
                    "device_auth": [{
                        "username": "root",
                        "password": "Embe1mpls"
                    }],
                    "management_subnets": [
                        {
                            "cidr": "10.87.69.0/25",
                            "gateway": "10.87.69.1"
                        }
                    ],
                    "overlay_ibgp_asn": 64512,
                    "node_profiles": [
                        {
                            "node_profile_name": "juniper-qfx5k"
                        }
                    ]
                }
            }
        :param prouter_name: String
            example: "5c3-qfx8"
        :param pr_product_name: String
            example: "qfx5110-48s-4c"
        :param prouter_vendor: String
            example: "Juniper"
        :param hardware_inventory_payload: Dictionary
            example:
            {
        "inventory_info": "\n[{'description': u'QFX5110-48S-4C', 'parent': '',
         'model_number': None, 'module': u'Chassis',
        'version': None, 'serial_number': u'WS3718350232', 'model': None},
         {'description': None, 'parent': u'Chassis', 'model_number': None,
        'module': u'Pseudo CB 0', 'version': None, 'serial_number': None,
        'model': None}, {'description': u'RE-QFX5110-48S-4C',
         'parent': u'Chassis',
        'model_number': u'QFX5110-48S-AFI', 'module': u'Routing Engine 0',
         'version': None, 'serial_number': u'BUILTIN', 'model': None},
        {'description': u'QFX5110-48S-4C', 'parent': u'Chassis',
        'model_number': u'QFX5110-48S-AFI', 'module': u'FPC 0',
         'version': u'REV 26', '
        serial_number': u'WS3718350232', 'model': None},
        {'description': u'FPC CPU', 'parent': u'FPC 0', 'model_number':
         None, 'module': u'CPU',
        'version': None, 'serial_number': u'BUILTIN', 'model': None},
         {'description': u'48x10G-4x100G', 'parent': u'FPC 0',
        'model_number': u'QFX5110-48S-AFI', 'module': u'PIC 0',
         'version': None, 'serial_number': u'BUILTIN', 'model': None},
        {'description': u'SFP+-10G-CU2M', 'parent': u'PIC 0',
         'model_number': None, 'module': u'Xcvr 0', 'version': None,
        'serial_number': u'APF164800484VC', 'model': None},
         {'description': u'SFP+-10G-CU2M', 'parent': u'PIC 0',
        'model_number': None, 'module': u'Xcvr 45', 'version': None,
         'serial_number': u'APF164900493E7', 'model': None},
        {'description': u'SFP+-10G-CU2M', 'parent': u'PIC 0',
         'model_number': None, 'module': u'Xcvr 46', 'version': None,
        'serial_number': u'APF18220040FVH', 'model': None}
            }
        }
        :return: Dictionary
        if success, returns
            {
              'status': 'success',
              'hardware_inventory_import_log':
               <String: hardware_inventory_import_log>,
              'hardware_inventory_import_resp': <Dictionary:
               hardware_inventory_import_resp>
            }
        if failure, returns
            {
              'status': 'failure',
              'error_msg': <String: exception message>,
              'hardware_inventory_import_log':
              <String: hardware_inventory_import_log>,
              'hardware_inventory_import_resp':
              <Dictionary: hardware_inventory_import_resp>
            }
        """
        self._instantiate_filter_log_instance(prouter_name)
        _task_log("Starting Hardware Inventory Import")

        try:
            _task_log("Creating hardware inventory object")
            hardware_inventory_import_resp = \
                self._create_hardware_inventory(
                    job_ctx,
                    hardware_inventory_payload,
                    prouter_name, pr_product_name, prouter_vendor
                )
            _task_done()

            return {
                'status': 'success',
                'hardware_inventory_import_log': FilterLog.instance().dump(),
                'hardware_inventory_import_resp':
                    hardware_inventory_import_resp
            }
        except Exception as ex:
            _task_error_log(str(ex))
            _task_error_log(traceback.format_exc())
            return {
                'status': 'failure',
                'error_msg': str(ex),
                'hardware_inventory_import_log': FilterLog.instance().dump()}

    # end import_hardware_inventory_info

    def _create_hardware_inventory(self, job_ctx, hardware_inventory_payload,
                                   prouter_name, pr_product_name,
                                   prouter_vendor):
        object_type = "hardware_inventory"
        hardware_iven_obj = prouter_vendor + "__" + pr_product_name
        json_data =\
            hardware_inventory_payload['hardware_inventory_inventory_info']
        data_str = json.dumps(json_data)

        hardware_inventory_payload_final = {
            "parent_type": "physical-router",
            "fq_name": [
                "default-global-system-config",
                prouter_name,
                hardware_iven_obj],
            "hardware_inventory_inventory_info": data_str
        }
        hardware_obj_key = "hardware_inventory_inventory_info"
        vnc_lib = JobVncApi.vnc_init(job_ctx)

    # try to update the existing hardware existing object else create new one.
        try:
            try:
                cls = JobVncApi.get_vnc_cls(object_type)
                inventory_obj = cls.from_dict(
                    **hardware_inventory_payload_final)
                existing_obj = vnc_lib.hardware_inventory_read(
                    fq_name=hardware_inventory_payload_final.get('fq_name'))
                existing_obj_dict = vnc_lib.obj_to_dict(existing_obj)
                to_be_upd_value =\
                    hardware_inventory_payload_final[hardware_obj_key]
                existing_value = existing_obj_dict.get(hardware_obj_key)
                if to_be_upd_value != existing_value:
                    vnc_lib.hardware_inventory_update(
                        inventory_obj)
            except NoIdError:
                vnc_lib.hardware_inventory_create(inventory_obj)

        except Exception as exc:
            raise Exception("Inventory object creation failed"
                            " with exception: %s", str(exc))

    # end _create_hardware_inventory
