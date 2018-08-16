#!/usr/bin/python

from cfgm_common.exceptions import RefsExistError

import traceback

import sys
sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
sys.path.append("/opt/contrail/fabric_ansible_playbooks/device_parsers")

from vnc_utils import VncUtils
from filter_utils import FilterLog, _task_log, _task_done,\
    _task_error_log, validate_payload

class FilterModule(object):

    def filters(self):
        return {
            'device_import': self.device_import,
        }

    def _instantiate_filter_log_instance(self, device_name):
        FilterLog.instance("DeviceImportFilter", device_name)

    def device_import(self, auth_token, prouter_name, prouter_vendor_name,
                      prouter_family_name, device_data, regex_str=".*"):

        self._instantiate_filter_log_instance(prouter_name)
        _task_log("Starting Device Import")
        device_import_resp = {}

        try:
            _task_log("Obtaining the parser method and parsing the data")
            file_module = __import__(prouter_vendor_name + "_device_import")
            class_name = getattr(file_module, "ParserModule")
            class_instance = class_name()
            prouter_vendor_name = prouter_vendor_name.replace("-", "_")
            prouter_family_name = prouter_family_name.replace("-", "_")
            interfaces_payload = getattr(class_instance, 'parse_%s_%s' %(
                                    prouter_vendor_name,
                                    prouter_family_name))(device_data,
                                                          regex_str)
            _task_done()

            _task_log("Creating interfaces")
            device_import_resp = self._create_interfaces_and_update_dataplane_ip(
                                                                                 auth_token,
                                                                                 interfaces_payload,
                                                                                 prouter_name,
                                                                                 self.validate_interfaces_payload
                                                                                )
            _task_done()

            if device_import_resp.get('phy_intf_failed_info') or \
                device_import_resp.get('log_intf_failed_info'):
                raise Exception("Create or Update physical or logical interfaces failed")

            return {
                    'status': 'success',
                    'device_import_log': FilterLog.instance().dump(),
                    'device_import_resp': device_import_resp
                   }
        except Exception as ex:
            _task_error_log(str(ex))
            _task_error_log(traceback.format_exc())
            return {'status': 'failure',
                    'error_msg': str(ex),
                    'device_import_log': FilterLog.instance().dump(),
                    'device_import_resp': device_import_resp
                   }

    def validate_interfaces_payload(self, payload):
        if 'physical_interfaces_list' not in payload:
            raise AttributeError("KeyError: 'physical_interfaces_list' key must be in parsed output")
        if 'logical_interfaces_list' not in payload:
            raise AttributeError("KeyError: 'logical_interfaces_list' key must be in parsed output")

    def get_create_interfaces_payload(self, device_name, physical_interfaces_list, logical_interfaces_list):

        phy_intfs_payloads = []
        log_intfs_payloads = []

        for phy_intfs_obj in physical_interfaces_list:
            phy_interface_name = phy_intfs_obj['physical_interface_name'].replace(':', '_')

            phy_intfs_payload = {
                                  "parent_type": "physical-router",
                                  "fq_name": [
                                              "default-global-system-config",
                                              device_name,
                                              phy_interface_name],
                                  "display_name": phy_intfs_obj['physical_interface_name']
                                }
            if 'physical_interface_mac_address' in phy_intfs_obj:
                phy_intfs_payload['physical_interface_mac_addresses']\
                        = {"mac_address": [phy_intfs_obj['physical_interface_mac_address']]}

            if 'physical_interface_port_id' in phy_intfs_obj:
                phy_intfs_payload['physical_interface_port_id'] = phy_intfs_obj['physical_interface_port_id']

            phy_intfs_payloads.append(phy_intfs_payload)


        for log_intfs_obj in logical_interfaces_list:
            phy_interface_name = log_intfs_obj['physical_interface_name']
            log_interface_name = log_intfs_obj['logical_interface_name']

            if phy_interface_name not in log_interface_name:
                # implies log_interface_name is actually a unit no.
                log_interface_name = phy_interface_name + "." + log_interface_name

            log_intfs_payload = {
                                  "parent_type": "physical-interface",
                                  "fq_name": ["default-global-system-config",
                                              device_name,
                                              phy_interface_name.replace(':', '_'),
                                              log_interface_name.replace(':', '_')
                                             ],
                                  "display_name": log_interface_name
                                }

            if 'logical_interface_vlan_tag' in log_intfs_obj:
                log_intfs_payload['logical_interface_vlan_tag'] = log_intfs_obj['logical_interface_vlan_tag']

            if 'logical_interface_type' in log_intfs_obj:
                log_intfs_payload['logical_interface_type'] = log_intfs_obj['logical_interface_type']

            log_intfs_payloads.append(log_intfs_payload)

        return phy_intfs_payloads, log_intfs_payloads

    # group vnc functions
    @validate_payload
    def _create_interfaces_and_update_dataplane_ip(self, auth_token, interfaces_payload, prouter_name, validator_method):
        # create or update the interfaces
        # args: interfaces_payload is of the fmt
        #{
        #  "physical_interfaces_list": [<payload>, <payload>],
        #  "logical_interfaces_list": [<payload>, <payload>],
        #  "dataplane_ip": <dataplane ip if present else "">
        #}

        vnc_lib = VncUtils._init_vnc_api(auth_token)
        physical_interfaces_list = interfaces_payload.get('physical_interfaces_list')
        logical_interfaces_list = interfaces_payload.get('logical_interfaces_list')
        dataplane_ip = interfaces_payload.get('dataplane_ip', "")

        vnc_physical_interfaces_list, vnc_logical_interfaces_list = self.get_create_interfaces_payload(
                                                                         prouter_name,
                                                                         physical_interfaces_list,
                                                                         logical_interfaces_list
                                                                        )

        phy_intfs_success_names, phy_intf_failed_info = self._create_physical_interfaces(vnc_lib, vnc_physical_interfaces_list)
        log_intfs_success_names, log_intf_failed_info = self._create_logical_interfaces(vnc_lib, vnc_logical_interfaces_list)
        dataplane_ip, dataplane_ip_upd_resp, warning_info = self._update_dataplane_ip(vnc_lib, dataplane_ip, prouter_name)

        return {
                 "phy_intfs_success_names": list(set(phy_intfs_success_names)),
                 "log_intfs_success_names": list(set(log_intfs_success_names)),
                 "phy_intf_failed_info": phy_intf_failed_info,
                 "log_intf_failed_info": log_intf_failed_info,
                 "dataplane_ip": dataplane_ip,
                 "dataplane_ip_upd_resp": dataplane_ip_upd_resp,
                 "warning_info": warning_info
        }

    def _create_physical_interfaces(self, vnc_lib, physical_interfaces_payload):
        object_type = "physical_interface"
        success_intfs_names = []
        phy_intf_failed_info = []

        for phy_interface_dict in physical_interfaces_payload:
            try:
                try:
                    cls = VncUtils._get_vnc_cls(object_type)
                    phy_interface_dict['uuid'] = None
                    phy_interface_obj = cls.from_dict(**phy_interface_dict)
                    phy_intf_uuid = vnc_lib.physical_interface_create(phy_interface_obj)
                    # _task_log(phy_intf_uuid)
                    success_intfs_names.append(phy_interface_dict['fq_name'][-1])
                except RefsExistError as exc:
                    vnc_lib.physical_interface_update(phy_interface_obj)
                    success_intfs_names.append(phy_interface_dict['fq_name'][-1])
            except Exception as exc:
                _task_error_log(str(exc))
                _task_error_log(traceback.format_exc())
                phy_intf_failed_info.append({
                                             "phy_interface_name": phy_interface_dict['fq_name'][-1],
                                             "failure_msg": str(exc) 
                                            })
        return success_intfs_names, phy_intf_failed_info

    def _create_logical_interfaces(self, vnc_lib, logical_interfaces_payload):
        object_type = "logical_interface"
        success_intfs_names = []
        log_intf_failed_info = []

        for log_interface_dict in logical_interfaces_payload:
            try:
                try:
                    cls = VncUtils._get_vnc_cls(object_type)
                    log_interface_dict['uuid'] = None
                    log_interface_obj = cls.from_dict(**log_interface_dict)
                    log_intf_uuid = vnc_lib.logical_interface_create(log_interface_obj)
                    # _task_log(log_intf_uuid)
                    success_intfs_names.append(log_interface_dict['fq_name'][-1])
                except RefsExistError as exc:
                    vnc_lib.logical_interface_update(log_interface_obj)
                    success_intfs_names.append(log_interface_dict['fq_name'][-1])
            except Exception as exc:
                _task_error_log(str(exc))
                _task_error_log(traceback.format_exc())
                log_intf_failed_info.append({
                                             "log_interface_name": log_interface_dict['fq_name'][-1],
                                             "failure_msg": str(exc)
                                            })
        return success_intfs_names, log_intf_failed_info

    def _update_dataplane_ip(self, vnc_lib, dataplane_ip, prouter_name):
        warning_info = {}
        object_type = "physical_router"
        if dataplane_ip == "":
            return "", "", warning_info
        else:
            try:
                obj_dict = {
                             "uuid": None,
                             "fq_name": ["default-global-system-config", prouter_name],
                             "physical_router_dataplane_ip": dataplane_ip,
                             "physical_router_loopback_ip": dataplane_ip
                           }
                cls = VncUtils._get_vnc_cls(object_type)
                physical_router_obj = cls.from_dict(**obj_dict)
                vnc_lib.physical_router_update(physical_router_obj)
                upd_resp = "\nUpdated device with dataplane ip: "
            except Exception as ex:
                _task_error_log(str(ex))
                _task_error_log(traceback.format_exc())
                upd_resp = "There was a problem while updating the device with dataplane ip: "
                warning_info = {
                               "device_name": prouter_name,
                               "dataplane_ip": dataplane_ip,
                               "warning_message": str(ex)
                             }

        return dataplane_ip, upd_resp, warning_info


