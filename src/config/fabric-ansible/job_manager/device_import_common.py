from vnc_utils import VncUtils

class DeviceImportBasePlugin(object):
    _device_info_parsers = {}

    def register_parser_method(self, vendor, family, method):
        self._device_info_parsers[vendor+"_"+family] = method

    def _get_parser_method(self, vendor, family):
        return self._device_info_parsers[vendor+"_"+family]

    def device_import(self, auth_token, prouter_name, prouter_vendor_name,
                      prouter_family_name, device_data, regex_str):
        try:
            device_import_log = "Starting Device Import ...\n"
            parser_method = self._get_parser_method(prouter_vendor_name, prouter_family_name)
            device_import_log += "Obtained the parser method ... \n"
            interfaces_payload = parser_method(device_data, prouter_name, regex_str)
            device_import_log += "Completed Parsing ... \n"
            self._create_interfaces_and_update_dataplane_ip(auth_token, interfaces_payload)
            device_import_log += "Created interfaces ... \n"
            return {
                    'status': 'success',
                    'device_import_log': device_import_log,
                    'interfaces_payload': interfaces_payload
                   }
        except Exception as ex:
            # TODO: print stacktrace in debug logs
            return {'status': 'failure',
                    'error_msg': str(ex),
                    'device_import_log': device_import_log}
            

    # group vnc functions
    def _create_interfaces_and_update_dataplane_ip(self, auth_token, interfaces_payload):
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

    def _create_physical_interfaces(self, vnc_lib, physical_interfaces_payload):
        object_type = "physical_interface"
        success_uuids = []
        failed_uuids = []

        for phy_interface_dict in physical_interfaces_payload:
            try:
                cls = VncUtils._get_vnc_cls(object_type)
                phy_interface_dict['uuid'] = None
                phy_interface_obj = cls.from_dict(**phy_interface_dict)
                phy_intf_uuid = vnc_lib.physical_interface_create(phy_interface_obj)
                success_uuids.append(phy_intf_uuid)
            except Exception as ex:
                failed_uuids.append(str(ex))
                # TODO: print stacktrace in debug logs
            return success_uuids, failed_uuids

    def _create_logical_interfaces(self, vnc_lib, logical_interfaces_payload):
        return

    def _update_dataplane_ip(self, vnc_lib, dataplane_ip):
        return
