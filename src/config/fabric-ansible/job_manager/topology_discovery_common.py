from vnc_api.vnc_api import VncApi

class TopologyDiscoveryBasePlugin(object):
    _device_info_parsers = {}

    def register_parser_method(self, vendor, family, method):
        self._device_info_parsers[vendor+"_"+family] = method

    def _get_parser_method(self, vendor, family):
        return self._device_info_parsers[vendor+"_"+family]

    def topology_discovery(self, auth_token, prouter_fqname, prouter_vendor_name, prouter_family_name, device_data):
        parser_method = self._get_parser_method(prouter_vendor_name, prouter_family_name)
        phy_intfs_payload = parser_method(device_data, prouter_fqname)
        self._create_interfaces_refs(auth_token, phy_intfs_payload)

    # group vnc functions
    def _create_interfaces_refs(self, auth_token, phy_intfs_payload):
        # create or update refs between physical interfaces
        # on the local device to the remote device
        return

