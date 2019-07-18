#!/usr/bin/python

import sys
import traceback

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
from filter_utils import _task_done, _task_error_log, _task_log, FilterLog
from vnc_api.exceptions import NoIdError

from job_manager.job_utils import JobVncApi


class FilterModule(object):

    def filters(self):
        return {
            'import_chassis_info': self.import_chassis_info,
        }
    # end filters

    def _instantiate_filter_log_instance(self, device_name):
        FilterLog.instance("Import_chassis_info_Filter", device_name)
    # end  _instantiate_filter_log_instance

    def import_chassis_info(self, job_ctx, prouter_name,
                            chassis_payload, prouter_vendor):
        """Import chassis Mac.

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
        :param prouter_vendor: String
            example: "juniper"
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
                  <List: <String: chassis_mac_id> >,
              "warning_info": <List: warn_info>
            }
        """
        self._instantiate_filter_log_instance(prouter_name)
        _task_log("Starting Chassis Info Import")

        try:
            chassis_vnc_payloads = self.get_vnc_chassis_payloads(
                chassis_payload.get('device_chassis_id_info'),
                prouter_vendor
            )
            chassis_import_resp = self._import_chassis_info(
                job_ctx,
                chassis_vnc_payloads,
                prouter_name,
                prouter_vendor
            )

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
    # end import_chassis_info

    def get_vnc_chassis_payloads(self, chassis_payload, prouter_vendor):
        vnc_chassis_payloads = []

        for chassis_obj in chassis_payload or []:
            chassis_dict = dict()
            chassis_dict['fq_name'] = [
                prouter_vendor + '_' + chassis_obj.get(
                    'device_chassis_id').replace(':', '_')
            ]
            chassis_type = chassis_obj.get('chassis_id_type')
            if chassis_type:
                chassis_dict['device_chassis_type'] = chassis_type
            vnc_chassis_payloads.append(chassis_dict)

        return vnc_chassis_payloads
    # end get_vnc_chassis_payloads

    # group vnc functions
    def _import_chassis_info(self, job_ctx, chassis_payload,
                             prouter_name, prouter_vendor):

        warning_info = []
        chassis_mac_ids = []
        chassis_mac_fqnames = []
        object_type = "device_chassis"

        vnc_lib = JobVncApi.vnc_init(job_ctx)

        # try to create device_chassis objects from chassis_payload
        for chassis_info in chassis_payload:
            chassis_mac_fqname = chassis_info.get('fq_name')
            chassis_mac_id = chassis_mac_fqname[-1].split(
                prouter_vendor + '_')[1].replace('_', ':')
            try:
                try:
                    cls = JobVncApi.get_vnc_cls(object_type)
                    chassis_obj = cls.from_dict(**chassis_info)
                    existing_obj = vnc_lib.device_chassis_read(
                        fq_name=chassis_mac_fqname)
                    existing_obj_dict = vnc_lib.obj_to_dict(existing_obj)
                    for key in chassis_info:
                        to_be_upd_value = chassis_info[key]
                        existing_value = existing_obj_dict.get(key)
                        if to_be_upd_value != existing_value:
                            vnc_lib.device_chassis_update(
                                chassis_obj)
                            break
                    chassis_mac_fqnames.append(
                        chassis_mac_fqname)
                    chassis_mac_ids.append(
                        chassis_mac_id
                    )
                except NoIdError as ex:
                    vnc_lib.device_chassis_create(chassis_obj)
                    chassis_mac_fqnames.append(
                        chassis_mac_fqname)
                    chassis_mac_ids.append(
                        chassis_mac_id
                    )
            except Exception as exc:
                _task_error_log(str(exc))
                _task_error_log(traceback.format_exc())
                warn_info = dict()
                warn_info['failed_operation'] = 'Device Chassis Creation'
                warn_info['failure_message'] = 'Error while creating' \
                                               'chassis_id(' + \
                                               chassis_mac_id + \
                                               ') for ' + prouter_vendor + \
                                               ' device: ' + str(exc)
                warning_info.append(warn_info)

        # Now try to link all the created chassis objects as references
        # to this prouter

        object_type = "physical_router"
        device_chassis_ref_list = []
        for chassis_fqname in chassis_mac_fqnames:
            device_chassis_ref_list.append(
                {'to': chassis_fqname}
            )

        try:
            pr_dict = {
                'fq_name': ['default-global-system-config', prouter_name]
            }
            cls = JobVncApi.get_vnc_cls(object_type)
            physical_router_obj = cls.from_dict(**pr_dict)
            physical_router_obj.set_device_chassis_list(
                device_chassis_ref_list)
            vnc_lib.physical_router_update(physical_router_obj)
        except Exception as ex:
            _task_error_log(str(ex))
            _task_error_log(traceback.format_exc())
            warn_info = dict()
            warn_info['failed_operation'] = 'Physical Router Updation'
            warn_info['failure_message'] = 'Error while updating' \
                                           'chassis_ids refs for ' + \
                                           prouter_vendor + ' device ' + \
                                           prouter_name + ': ' + str(ex)
            warning_info.append(warn_info)

        return \
            {
                'chassis_mac_ids': chassis_mac_ids,
                'warning_info': warning_info
            }
    # end  _import_chassis_info
