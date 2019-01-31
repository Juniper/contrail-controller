#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common.exceptions import (
    RefsExistError,
    NoIdError
)
from vnc_api.vnc_api import VncApi
from vnc_api.gen.resource_client import VirtualMachine, \
    VirtualMachineInterface, VirtualNetwork, InstanceIp, SecurityGroup

api_server_host = '192.168.3.23'
api_server_port = 8082
api_server_username = 'admin'
api_server_password = 'c0ntrail123'
api_server_tenant = 'admin'

class DMSanity(object):

    def __init__(self):
        self._api = VncApi(
            api_server_host=api_server_host,
            api_server_port=api_server_port,
            username=api_server_username,
            password=api_server_password,
            tenant_name=api_server_tenant)

    def onboard_fabric(self, fabric_info):
        job_template_fq_name = [
            'default-global-system-config', 'existing_fabric_onboard_template']
        job_execution_info = self._api.execute_job(
            job_template_fq_name=job_template_fq_name,
            job_input=fabric_info
        )
        job_execution_id = job_execution_info.get('job_execution_id')
        while True:
            status = self._api.job_status(job_execution_id)
            if status is "SUCCESS" or "FAILURE":
                break

    def create_virtual_network(self):
        try:
            vn_obj = VirtualNetwork(
                display_name="VN1",
                name="VN1",
                parent_type="project",
                network_ipam_refs=[{
                    "to": ["default-domain", "default-project", "default-network-ipam"],
                    "attr": {
                        "ipam_subnets": [{
                            "default_gateway": "172.16.10.1",
                            "enable_dhcp": True,
                            "subnet_name": "95684a26-7cda-446f-b830-5d5f03ea9f75",
                            "addr_from_start": True,
                            "subnet": {
                                "ip_prefix": "172.16.10.0",
                                "ip_prefix_len": 24
                            },
                            "subnet_uuid": "95684a26-7cda-446f-b830-5d5f03ea9f75"
                        }]
                    }
                }],
                fq_name=["default-domain", "admin",
                          "VN1"],
            )
            self.vn_uuid = self._api.virtual_network_create(vn_obj)
        except RefsExistError:
            self.vn_uuid = self._api.fq_name_to_id('virtual-network',
                                                 ["default-domain", "admin", "VN1"])

    def create_sg(self):
        try:
            sg_obj = SecurityGroup(
                name="SG1",
                fq_name=["default-domain", "default-project", "SG1"],
                configured_security_group_id=0,
                security_group_entries={
                "policy_rule": [{
                    "direction": ">",
                    "ethertype": "IPv4",
                    "protocol": "tcp",
                    "dst_addresses": [{
                        "network_policy": None,
                        "security_group": None,
                        "subnet": {
                            "ip_prefix": "0.0.0.0",
                            "ip_prefix_len": "0"
                        },
                        "virtual_network": None
                    }],
                    "dst_ports": [{
                        "end_port": "65535",
                        "start_port": "0"
                    }],
                    "src_addresses": [{
                        "network_policy": None,
                        "security_group": "local",
                        "subnet": None,
                        "virtual_network": None
                    }],
                    "src_ports": [{
                        "end_port": "65535",
                        "start_port": "0"
                    }]
                }, {
                    "direction": ">",
                    "ethertype": "IPv6",
                    "protocol": "tcp",
                    "dst_addresses": [{
                        "network_policy": None,
                        "security_group": None,
                        "subnet": {
                            "ip_prefix": "::",
                            "ip_prefix_len": "0"
                        },
                        "virtual_network": None
                    }],
                    "dst_ports": [{
                        "end_port": "65535",
                        "start_port": "0"
                    }],
                    "src_addresses": [{
                        "network_policy": None,
                        "security_group": "local",
                        "subnet": None,
                        "virtual_network": None
                    }],
                    "src_ports": [{
                        "end_port": "65535",
                        "start_port": "0"
                    }]
                }]
            })
            self.sg_uuid = self._api.security_group_create(sg_obj)
        except RefsExistError:
            self.sg_uuid = self._api.fq_name_to_id('security-group', ["default-domain", "default-project", "SG1"])

    def vmi_create_mh(self):
        try:
            vn = self._api.virtual_network_read(id=self.vn_uuid)
            sg = self._api.security_group_read(id=self.sg_uuid)

            vn_fq_name = self._api.id_to_fq_name(self.vn_uuid)

            vm_data = VirtualMachine(
                name="bms1",
                display_name="bms1",
                fq_name=["bms1"],
                server_type="baremetal-server"
            )
            vm_uuid = self._api.virtual_machine_create(vm_data)

            inst_ip_obj = InstanceIp(
                instance_ip_address=[{
                    "fixedIp": "172.16.10.4",
                    "domain": "default-domain",
                    "project": "admin"
                }],
                instance_ip_family="v4"
            )
            import uuid
            vmi_data1 =  VirtualMachineInterface(
                fq_name=["default-domain", "admin", str(uuid.uuid4())],
                parent_type="project",
                virtual_machine_interface_device_owner="baremetal:none",
                virtual_machine_interface_mac_addresses={
                    "mac_address": ["08:00:27:97:86:68"]
                }
            )
            virtual_machine_interface_bindings1={
                    "key_value_pair": [{
                        "key": "vnic_type",
                        "value": "baremetal"
                    }, {
                        "key": "vif_type",
                        "value": "vrouter"
                    }, {
                        "key": "profile",
                        "value": "{\"local_link_information\":[{\"switch_id\":\"11:11:11:11:11:11\",\"port_id\":\"xe-0/0/2\",\"switch_info\":\"vqfx2\",\"fabric\":\"fab-lag-mh\"},{\"switch_id\":\"11:11:11:11:11:11\",\"port_id\":\"xe-0/0/2\",\"switch_info\":\"vqfx3\",\"fabric\":\"fab-lag-mh\"}]}"
                    }, {
                        "key": "host_id",
                        "value": vm_uuid
                    }]
                }
            virtual_machine_interface_properties1={
                    "sub_interface_vlan_tag": 100
                }
            vmi_data1.set_virtual_machine_interface_bindings(
                virtual_machine_interface_bindings1)
            vmi_data1.set_virtual_machine_interface_properties(
                virtual_machine_interface_properties1)
            vmi_data1.add_virtual_network(vn)
            vmi_data1.add_security_group(sg)
            vmi_uuid = self._api.virtual_machine_interface_create(vmi_data1)
        except Exception as ex:
            print("Test failed due to unexpected error: %s" % str(ex))


    def vmi_create_lag(self):
        try:
            vn = self._api.virtual_network_read(id=self.vn_uuid)
            sg = self._api.security_group_read(id=self.sg_uuid)
            vn_fq_name = self._api.id_to_fq_name(self.vn_uuid)

            vm_data = VirtualMachine(
                name="bms2",
                display_name="bms2",
                fq_name=["bms2"],
                server_type="baremetal-server"
            )
            vm_uuid = self._api.virtual_machine_create(vm_data)

            inst_ip_obj = InstanceIp(
                instance_ip_address=[{
                    "fixedIp": "172.16.10.3",
                    "domain": "default-domain",
                    "project": "admin"
                }],
                instance_ip_family="v4"
            )
            import uuid
            vmi_data1 =  VirtualMachineInterface(
                fq_name=["default-domain", "admin", str(uuid.uuid4())],
                parent_type="project",
                virtual_machine_interface_device_owner="baremetal:none",
                virtual_machine_interface_mac_addresses={
                    "mac_address": ["08:00:27:af:94:01"]
                }
            )
            virtual_machine_interface_bindings1={
                    "key_value_pair": [{
                        "key": "vnic_type",
                        "value": "baremetal"
                    }, {
                        "key": "vif_type",
                        "value": "vrouter"
                    }, {
                        "key": "profile",
                        "value": "{\"local_link_information\":[{\"switch_id\":\"11:11:11:11:11:11\",\"port_id\":\"xe-0/0/1\",\"switch_info\":\"vqfx2\",\"fabric\":\"fab-lag-mh\"},{\"switch_id\":\"11:11:11:11:11:11\",\"port_id\":\"xe-0/0/5\",\"switch_info\":\"vqfx2\",\"fabric\":\"fab-lag-mh\"}]}"
                    }, {
                        "key": "host_id",
                        "value": vm_uuid
                    }]
                }
            virtual_machine_interface_properties1={
                    "sub_interface_vlan_tag": 100
                }
            vmi_data1.set_virtual_machine_interface_bindings(
                virtual_machine_interface_bindings1)
            vmi_data1.set_virtual_machine_interface_properties(
                virtual_machine_interface_properties1)
            vmi_data1.add_virtual_network(vn)
            vmi_data1.add_security_group(sg)
            vmi_uuid = self._api.virtual_machine_interface_create(vmi_data1)
        except Exception as ex:
            print("Test failed due to unexpected error: %s" % str(ex))


    def vmi_create_mh_multi_vlan(self):
        try:
            vn = self._api.virtual_network_read(id=self.vn_uuid)
            sg = self._api.security_group_read(id=self.sg_uuid)
            vn_fq_name = self._api.id_to_fq_name(self.vn_uuid)

            vm_data = VirtualMachine(
                name="bms1",
                display_name="bms1",
                fq_name=["bms1"],
                server_type="baremetal-server"
            )
            vm_uuid = self._api.virtual_machine_create(vm_data)

            inst_ip_obj = InstanceIp(
                instance_ip_address=[{
                    "fixedIp": "172.16.10.4",
                    "domain": "default-domain",
                    "project": "admin"
                }],
                instance_ip_family="v4"
            )
            import uuid
            vmi_data1 =  VirtualMachineInterface(
                fq_name=["default-domain", "admin", str(uuid.uuid4())],
                parent_type="project",
                virtual_machine_interface_device_owner="baremetal:none",
                virtual_machine_interface_mac_addresses={
                    "mac_address": ["08:00:27:97:86:68"]
                }
            )
            virtual_machine_interface_bindings1={
                    "key_value_pair": [{
                        "key": "vnic_type",
                        "value": "baremetal"
                    }, {
                        "key": "vif_type",
                        "value": "vrouter"
                    }, {
                        "key": "profile",
                        "value": "{\"local_link_information\":[{\"switch_id\":\"11:11:11:11:11:11\",\"port_id\":\"xe-0/0/2\",\"switch_info\":\"vqfx2\",\"fabric\":\"fab-lag-mh\"},{\"switch_id\":\"11:11:11:11:11:11\",\"port_id\":\"xe-0/0/2\",\"switch_info\":\"vqfx3\",\"fabric\":\"fab-lag-mh\"}]}"
                    }, {
                        "key": "host_id",
                        "value": vm_uuid
                    }]
                }
            virtual_machine_interface_properties1={
                    "sub_interface_vlan_tag": 100
                }
            vmi_data1.set_virtual_machine_interface_bindings(
                virtual_machine_interface_bindings1)
            vmi_data1.set_virtual_machine_interface_properties(
                virtual_machine_interface_properties1)
            vmi_data1.add_virtual_network(vn)
            vmi_data1.add_security_group(sg)
            vmi_uuid1 = self._api.virtual_machine_interface_create(vmi_data1)

            vmi_obj = self._api.virtual_machine_interface_read(id=vmi_uuid1)
            lag_info = vmi_obj.get_link_aggregation_group_back_refs()
            lag_name = lag_info[0].get('to')[-1]

            vmi_data2 =  VirtualMachineInterface(
                fq_name=["default-domain", "admin", str(uuid.uuid4())],
                parent_type="project",
                virtual_machine_interface_device_owner="baremetal:none",
                virtual_machine_interface_mac_addresses={
                    "mac_address": ["08:00:27:97:86:68"]
                }
            )
            virtual_machine_interface_bindings2={
                    "key_value_pair": [{
                        "key": "vnic_type",
                        "value": "baremetal"
                    }, {
                        "key": "vif_type",
                        "value": "vrouter"
                    }, {
                        "key": "profile",
                        "value": "{\"local_link_information\":[{\"switch_id\":\"11:11:11:11:11:11\",\"port_id\":\"xe-0/0/2\",\"switch_info\":\"vqfx2\",\"fabric\":\"fab-lag-mh\"},{\"switch_id\":\"11:11:11:11:11:11\",\"port_id\":\"xe-0/0/2\",\"switch_info\":\"vqfx3\",\"fabric\":\"fab-lag-mh\"}]}"
                    }, {
                        "key": "host_id",
                        "value": vm_uuid
                    },{
                        "key": "lag",
                        "value": lag_name
                    }]
                }
            virtual_machine_interface_properties2={
                    "sub_interface_vlan_tag": 120
                }
            vmi_data2.set_virtual_machine_interface_bindings(
                virtual_machine_interface_bindings2)
            vmi_data2.set_virtual_machine_interface_properties(
                virtual_machine_interface_properties2)
            vmi_data2.add_virtual_network(vn)
            vmi_data2.add_security_group(sg)
            vmi_uuid2 = self._api.virtual_machine_interface_create(vmi_data2)
        except Exception as ex:
            print("Test failed due to unexpected error: %s" % str(ex))


    def fabric_onboard(self):
        try:
            self.onboard_fabric(
                fabric_info={
                    "fabric_fq_name": ["default-global-system-config",
                                       "fab-lag-mh"],
                    "device_auth": [
                        {
                            "username": "root",
                            "password": "c0ntrail123"
                        }
                    ],
                    "fabric_asn_pool": [
                        {
                            "asn_max": 65000,
                            "asn_min": 64000
                        },
                        {
                            "asn_max": 65100,
                            "asn_min": 65000
                        }
                    ],
                    "management_subnets": [
                        { "cidr": "2.2.2.1/24", "gateway": "2.2.2.1" }
                    ],
                    "node_profiles": [
                        {
                            "node_profile_name": "juniper-qfx5k"
                        },
                        {
                            "node_profile_name": "juniper-qfx10k"
                        },
                        {
                            "node_profile_name": "juniper-qfx5k-lean"
                        },
                        {
                            "node_profile_name": "juniper-mx"
                        }
                    ]
                }
            )
            import time
            time.sleep(300)
        except Exception as ex:
            print("Test failed due to unexpected error: %s" % str(ex))

if __name__ == "__main__":
    dmsanity = DMSanity()
    #dmsanity.fabric_onboard()
    dmsanity.create_virtual_network()
    dmsanity.create_sg()
    #dmsanity.vmi_create_mh()
    dmsanity.vmi_create_lag()
    dmsanity.vmi_create_mh_multi_vlan()


