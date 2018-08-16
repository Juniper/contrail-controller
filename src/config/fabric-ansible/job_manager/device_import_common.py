from vnc_utils import VncUtils
from cfgm_common.exceptions import RefsExistError
from filter_utils import FilterLog, _task_log, _task_done,\
    _task_error_log, validate_payload
import sys
import traceback

class DeviceImportBasePlugin(object):
    _device_info_parsers = {}

    def _instantiate_filter_log_instance(self):
        FilterLog.instance("DeviceImportFilter")

    def register_parser_method(self, vendor, family, method):
        self._device_info_parsers[vendor+"_"+family] = method

    def _get_parser_method(self, vendor, family):
        return self._device_info_parsers[vendor+"_"+family]

    def device_import(self, auth_token, prouter_name, prouter_vendor_name,
                      prouter_family_name, device_data, regex_str=".*"):

        self._instantiate_filter_log_instance()
        _task_log("Starting Device Import")
        try:
            _task_log("Obtaining the parser method")
            parser_method = self._get_parser_method(prouter_vendor_name, prouter_family_name)
            _task_done()

            _task_log("Starting to parse device data")
            interfaces_payload = parser_method(device_data, prouter_name, regex_str)
            _task_done()

            _task_log("Creating interfaces")
            device_import_resp = self._create_interfaces_and_update_dataplane_ip(
                                                                                 auth_token,
                                                                                 interfaces_payload,
                                                                                 prouter_name,
                                                                                 self.validate_interfaces_payload
                                                                                )
            _task_done()

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
                    'device_import_log': FilterLog.instance().dump()}

    def validate_interfaces_payload(self, payload):
        if 'physical_interfaces_list' not in payload:
            raise AttributeError("KeyError: 'physical_interfaces_list' key must be in parsed output")
        if 'logical_interfaces_list' not in payload:
            raise AttributeError("KeyError: 'logical_interfaces_list' key must be in parsed output")

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

        phy_intfs_success_names, phy_intfs_failed_names = self._create_physical_interfaces(vnc_lib, physical_interfaces_list)
        log_intfs_success_names, log_intfs_failed_names = self._create_logical_interfaces(vnc_lib, logical_interfaces_list)
        dataplane_ip, dataplane_ip_upd_resp = self._update_dataplane_ip(vnc_lib, dataplane_ip, prouter_name)

        return {
                 "phy_intfs_success_names": list(set(phy_intfs_success_names)),
                 "phy_intfs_failed_names": list(set(phy_intfs_failed_names)),
                 "log_intfs_success_names": list(set(log_intfs_success_names)),
                 "log_intfs_failed_names": list(set(log_intfs_failed_names)),
                 "dataplane_ip": dataplane_ip,
                 "dataplane_ip_upd_resp": dataplane_ip_upd_resp
        }

    def _create_physical_interfaces(self, vnc_lib, physical_interfaces_payload):
        object_type = "physical_interface"
        success_intfs_names = []
        failed_intfs_names = []

        for phy_interface_dict in physical_interfaces_payload:
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
            except Exception as ex:
                _task_error_log(str(ex))
                _task_error_log(traceback.format_exc())
                if 'fq_name' in phy_interface_dict:
                    failed_intfs_names.append(phy_interface_dict['fq_name'][-1])
        return success_intfs_names, failed_intfs_names

    def _create_logical_interfaces(self, vnc_lib, logical_interfaces_payload):
        object_type = "logical_interface"
        success_intfs_names = []
        failed_intfs_names = []

        for log_interface_dict in logical_interfaces_payload:
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
            except Exception as ex:
                _task_error_log(str(ex))
                _task_error_log(traceback.format_exc())
                if 'fq_name' in log_interface_dict:
                    failed_intfs_names.append(log_interface_dict['fq_name'][-1])
        return success_intfs_names, failed_intfs_names

    def _update_dataplane_ip(self, vnc_lib, dataplane_ip, prouter_name):
        object_type = "physical_router"
        if dataplane_ip == "":
            return "", ""
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

        return dataplane_ip, upd_resp

