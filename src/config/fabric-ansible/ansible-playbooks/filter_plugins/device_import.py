#!/usr/bin/python

from job_manager.device_import_common import DeviceImportBasePlugin

class FilterModule(DeviceImportBasePlugin):

    def filters(self):
        return {
            'device_import': self.device_import,
        }
    def __init__(self):
        self.register_parser_method("juniper", "junos-mx", self.parse_juniper_data)
        self.register_parser_method("juniper", "junos-qfx", self.parse_juniper_data)
        self.register_parser_method("cisco", "ios-fmly", self.parse_cisco_ios_data)
        self.register_parser_method("arista", "eos-fmly", self.parse_arista_eos_data)


    def parse_juniper_data(self, device_data, prouter_name, regex_str):
        return {
            "physical_interfaces_list": [],
            "logical_interfaces_list": [],
            "dataplane_ip": "junos-qfx"
        }
        

    def parse_cisco_ios_data(self, device_data):
        print "parsed cisco ios with: "+device_data

    def parse_arista_eos_data(self, device_data):
        print "parsed arista eos with: "+device_data

