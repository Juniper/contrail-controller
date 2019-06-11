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
            'import_interfaces_info': self.import_interfaces_info,
        }
    # end filters

    def _instantiate_filter_log_instance(self, device_name):
        FilterLog.instance("Import_interfaces_infoFilter", device_name)
    # end _instantiate_filter_log_instance

    def import_interfaces_info(self, job_ctx, prouter_name,
                               interfaces_payload):
        """Import Interfaces.

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
        :param interfaces_payload: Dictionary
            example:
            {
              "physical_interfaces_list": [
                {
                  "physical_interface_port_id": "526",
                  "physical_interface_mac_address": "00:11:22:33:44:55",
                  "physical_interface_name": "xe-0/0/1:0"
                }
              ],
              "logical_interfaces_list": [
                {
                  "logical_interface_name": "xe-0/0/1:0.236",
                  "logical_interface_type": "l3",
                  "logical_interface_vlan_tag": "1213"
                }
              ]
            }


        :return: Dictionary
        if success, returns
            {
              'status': 'success',
              'device_import_log': <String: device_import_log>,
              'device_import_resp': <Dictionary: device_import_resp>
            }
        if failure, returns
            {
              'status': 'failure',
              'error_msg': <String: exception message>,
              'device_import_log': <String: device_import_log>,
              'device_import_resp': <Dictionary: device_import_resp>


            }
        :param device_import_resp: Dictionary
            example:
            {
              "phy_intfs_success_names":
                  <List: <String: phy_intfs_success_name> >,
              "log_intfs_success_names":
                  <List: <String: log_intfs_success_name> >,
              "phy_intf_failed_info":
                  <List: <Dictionary: phy_intf_failed_object> >,
              "log_intf_failed_info":
                  <List: <Dictionary: log_intf_failed_object> >
            }
        """
        self._instantiate_filter_log_instance(prouter_name)
        _task_log("Starting Device Import")
        device_import_resp = {}

        try:
            _task_log("Creating interfaces")
            device_import_resp = \
                self._create_interfaces(
                    job_ctx,
                    interfaces_payload,
                    prouter_name
                )
            _task_done()

            if device_import_resp.get('phy_intf_failed_info') or \
                    device_import_resp.get('log_intf_failed_info'):
                raise Exception(
                    "Create or Update physical or logical interfaces failed")

            return {
                'status': 'success',
                'device_import_log': FilterLog.instance().dump(),
                'device_import_resp': device_import_resp
            }
        except Exception as ex:
            _task_error_log(str(ex))
            _task_error_log(traceback.format_exc())
            return {
                'status': 'failure',
                'error_msg': str(ex),
                'device_import_log': FilterLog.instance().dump(),
                'device_import_resp': device_import_resp
            }
    # end import_interfaces_info

    def get_create_interfaces_payload(self, device_name,
                                      physical_interfaces_list,
                                      logical_interfaces_list):

        phy_intfs_payloads = []
        log_intfs_payloads = []

        for phy_intfs_obj in physical_interfaces_list:
            phy_interface_name = \
                phy_intfs_obj['physical_interface_name'].replace(':', '_')

            phy_intfs_payload = {
                "parent_type": "physical-router",
                "fq_name": [
                    "default-global-system-config",
                    device_name,
                    phy_interface_name],
                "display_name": phy_intfs_obj['physical_interface_name']
            }
            if 'physical_interface_mac_address' in phy_intfs_obj:
                phy_intfs_payload['physical_interface_mac_addresses'] \
                    = {"mac_address": [phy_intfs_obj
                                       ['physical_interface_mac_address']]}

            if 'physical_interface_port_id' in phy_intfs_obj:
                phy_intfs_payload['physical_interface_port_id'] = \
                    phy_intfs_obj['physical_interface_port_id']

            phy_intfs_payloads.append(phy_intfs_payload)

        for log_intfs_obj in logical_interfaces_list:
            phy_interface_name = log_intfs_obj['physical_interface_name']
            log_interface_name = log_intfs_obj['logical_interface_name']

            if phy_interface_name not in log_interface_name:
                # implies log_interface_name is actually a unit no.
                log_interface_name = phy_interface_name + "." + \
                    log_interface_name

            log_intfs_payload = {
                "parent_type": "physical-interface",
                "fq_name": [
                    "default-global-system-config",
                    device_name,
                    phy_interface_name.replace(':', '_'),
                    log_interface_name.replace(':', '_')
                ],
                "display_name": log_interface_name
            }

            if 'logical_interface_vlan_tag' in log_intfs_obj:
                log_intfs_payload['logical_interface_vlan_tag'] = \
                    log_intfs_obj['logical_interface_vlan_tag']

            if 'logical_interface_type' in log_intfs_obj:
                log_intfs_payload['logical_interface_type'] = \
                    log_intfs_obj['logical_interface_type']

            log_intfs_payloads.append(log_intfs_payload)

        return phy_intfs_payloads, log_intfs_payloads
    # end get_create_interfaces_payload

    # group vnc functions
    def _create_interfaces(self,
                           job_ctx,
                           interfaces_payload,
                           prouter_name):

        vnc_lib = JobVncApi.vnc_init(job_ctx)
        physical_interfaces_list = interfaces_payload.get(
            'physical_interfaces_list')
        logical_interfaces_list = interfaces_payload.get(
            'logical_interfaces_list')

        vnc_physical_interfaces_list, vnc_logical_interfaces_list = \
            self.get_create_interfaces_payload(
                prouter_name,
                physical_interfaces_list,
                logical_interfaces_list)

        phy_intfs_success_names, phy_intf_failed_info =\
            self._create_physical_interfaces(
                vnc_lib, vnc_physical_interfaces_list)
        log_intfs_success_names, log_intf_failed_info =\
            self._create_logical_interfaces(
                vnc_lib, vnc_logical_interfaces_list)

        return {
            "phy_intfs_success_names": list(set(phy_intfs_success_names)),
            "log_intfs_success_names": list(set(log_intfs_success_names)),
            "phy_intf_failed_info": phy_intf_failed_info,
            "log_intf_failed_info": log_intf_failed_info
        }
    # end _create_interfaces

    def _create_physical_interfaces(self, vnc_lib,
                                    physical_interfaces_payload):
        object_type = "physical_interface"
        success_intfs_names = []
        phy_intf_failed_info = []

        for phy_interface_dict in physical_interfaces_payload:
            try:
                try:
                    cls = JobVncApi.get_vnc_cls(object_type)
                    phy_interface_obj = cls.from_dict(**phy_interface_dict)
                    existing_obj = vnc_lib.physical_interface_read(
                        fq_name=phy_interface_dict.get('fq_name'))
                    existing_obj_dict = vnc_lib.obj_to_dict(existing_obj)
                    for key in phy_interface_dict:
                        to_be_upd_value = phy_interface_dict[key]
                        existing_value = existing_obj_dict.get(key)
                        if to_be_upd_value != existing_value:
                            vnc_lib.physical_interface_update(
                                phy_interface_obj)
                            break
                    success_intfs_names.append(
                        phy_interface_dict['fq_name'][-1])
                except NoIdError as exc:
                    vnc_lib.physical_interface_create(phy_interface_obj)
                    success_intfs_names.append(
                        phy_interface_dict['fq_name'][-1])
            except Exception as exc:
                _task_error_log(str(exc))
                _task_error_log(traceback.format_exc())
                phy_intf_failed_info.append({
                    "phy_interface_name": phy_interface_dict['fq_name'][-1],
                    "failure_msg": str(exc)
                })
        return success_intfs_names, phy_intf_failed_info
    # end _create_physical_interfaces

    def _create_logical_interfaces(self, vnc_lib,
                                   logical_interfaces_payload):
        object_type = "logical_interface"
        success_intfs_names = []
        log_intf_failed_info = []

        for log_interface_dict in logical_interfaces_payload:
            try:
                try:
                    cls = JobVncApi.get_vnc_cls(object_type)
                    log_interface_obj = cls.from_dict(**log_interface_dict)
                    existing_obj = vnc_lib.logical_interface_read(
                        fq_name=log_interface_dict.get('fq_name'))
                    existing_obj_dict = vnc_lib.obj_to_dict(existing_obj)
                    for key in log_interface_dict:
                        to_be_upd_value = log_interface_dict[key]
                        existing_value = existing_obj_dict.get(key)
                        if to_be_upd_value != existing_value:
                            vnc_lib.logical_interface_update(log_interface_obj)
                            break
                    success_intfs_names.append(
                        log_interface_dict['fq_name'][-1])
                except NoIdError as exc:
                    vnc_lib.logical_interface_create(log_interface_obj)
                    success_intfs_names.append(
                        log_interface_dict['fq_name'][-1])
            except Exception as exc:
                _task_error_log(str(exc))
                _task_error_log(traceback.format_exc())
                log_intf_failed_info.append({
                    "log_interface_name": log_interface_dict['fq_name'][-1],
                    "failure_msg": str(exc)
                })
        return success_intfs_names, log_intf_failed_info
    # end _create_logical_interfaces
