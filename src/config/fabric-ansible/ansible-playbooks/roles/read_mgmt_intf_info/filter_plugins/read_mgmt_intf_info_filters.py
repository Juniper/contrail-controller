from builtins import object


class FilterModule(object):
    def filters(self):
        return {
            'find_dhcp_assigned_mgmt_interface':
                self.find_dhcp_assigned_mgmt_interface
        }
    # end filters

    def find_dhcp_assigned_mgmt_interface(self, interface_configuration,
                                          mgmt_ip):
        mgmt_interface = {}
        if not interface_configuration or \
                'runtime_interfaces' not in interface_configuration:
            return mgmt_interface

        runtime_interfaces = interface_configuration.get('runtime_interfaces')

        if not runtime_interfaces:
            return mgmt_interface

        found_interface = None
        for interface in runtime_interfaces:
            if interface['logical_interface_address'] == mgmt_ip:
                found_interface = interface
                break

        if not found_interface:
            return mgmt_interface

        mgmt_interface['ip_address'] = mgmt_ip
        mgmt_interface['prefix'] = \
            found_interface['logical_interface_network'].split('/')[-1]
        mgmt_interface['mac_address'] = \
            found_interface['physical_interface_mac_address']
        mgmt_interface['name'] = \
            found_interface['physical_interface_name']
        mgmt_interface['unit'] = \
            found_interface['logical_interface_name'].split('.')[-1]

        return mgmt_interface
    # end find_dhcp_assigned_mgmt_interface
# end FilterModule
