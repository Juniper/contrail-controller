#!/usr/bin/python

import traceback
import sys

from job_manager.job_utils import JobVncApi
from vnc_api.exceptions import NoIdError

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")

from filter_utils import FilterLog, _task_log, _task_done, \
    _task_error_log


class FilterModule(object):

    def filters(self):
        return {
            'import_chassis_info': self.import_chassis_info,
        }

    def _instantiate_filter_log_instance(self, device_name):
        FilterLog.instance("Import_chassis_info_Filter", device_name)

    def import_chassis_info(self, job_ctx, prouter_name, chassis_payload):
        """
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
        :param chassis_payload: Dictionary
            example:
            {
              "device_chassis_id_info": [
                {
                  "device_chassis_id": "00:11:22:33:44:55",
                  "chassis_id_type": "private"
                }
              ]
            }


        :return: Dictionary
        if success, returns
            {
              'status': 'success',
              'chassis_import_log': <String: chassis_import_log>,
              'chassis_import_resp': <Dictionary: chassis_import_resp>
            }
        if failure, returns
            {
              'status': 'failure',
              'error_msg': <String: exception message>,
              'chassis_import_log': <String: chassis_import_log>
            }
        :param chassis_import_resp: Dictionary
            example:
            {
              "chassis_mac_ids":
                  <List: <object: device_chassis_id_info> >,
              "upd_resp": <String: upd_resp>
            }
        """
        self._instantiate_filter_log_instance(prouter_name)
        _task_log("Starting Chassis Info Import")
        chassis_import_resp = {}

        try:
            upd_resp, chassis_mac_ids = self._import_chassis_info(
                job_ctx,
                chassis_payload,
                prouter_name)

            chassis_import_resp['upd_resp'] = upd_resp
            chassis_import_resp['chassis_mac_ids'] = chassis_mac_ids

            _task_done()

            return {
                'status': 'success',
                'chassis_import_log': FilterLog.instance().dump(),
                'chassis_import_resp': chassis_import_resp
            }
        except Exception as ex:
            _task_error_log(str(ex))
            _task_error_log(traceback.format_exc())
            return {'status': 'failure',
                    'error_msg': str(ex),
                    'chassis_import_log': FilterLog.instance().dump()}

    # group vnc functions
    def _import_chassis_info(self, job_ctx, chassis_payload,
                             prouter_name):

        object_type = "physical_router"
        vnc_lib = JobVncApi.vnc_init(job_ctx)
        chassis_mac_ids = []
        try:
            obj_dict = {
                "fq_name": ["default-global-system-config", prouter_name],
                "physical_router_chassis_ids": chassis_payload
            }
            cls = JobVncApi.get_vnc_cls(object_type)
            physical_router_obj = cls.from_dict(**obj_dict)
            vnc_lib.physical_router_update(physical_router_obj)
            upd_resp = "\nImported chassis information for the " \
                       "device successfully."
            chassis_mac_ids = chassis_payload.get('device_chassis_id_info')
        except Exception as ex:
            _task_error_log(str(ex))
            _task_error_log(traceback.format_exc())
            upd_resp = "There was a problem while importing " \
                       "the chassis information for the device: " \
                       + str(ex)

        return upd_resp, chassis_mac_ids