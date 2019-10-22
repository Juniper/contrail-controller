#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import gevent
import json
from attrdict import AttrDict
from device_manager.device_manager import DeviceManager
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from test_dm_ansible_common import TestAnsibleCommonDM
from test_dm_utils import FakeJobHandler
from vnc_api.vnc_api import *


class TestAnsibleUnderlayIpClosDM(TestAnsibleCommonDM):

    def test_dm_ansible_underlay_ip_clos(self):


        self.create_feature_objects_and_params()
        jt = self.create_job_template('job-template-1')


	tag_obj = Tag(tag_type_name="label",
                          tag_value="fabric-management-ip")
        self._vnc_lib.tag_create(tag_obj)

        name = "test_ipclos"
        fabric_uuid = self.create_fabric(name)  
        fabric = self._vnc_lib.fabric_read(id=fabric_uuid)

	ns_name = unicode("management-subnets", "utf-8")
	ns_subnets = [
            {
                'cidr': "10.204.217.162/32",
                'gateway': "10.204.217.162"
            },
	    {
                'cidr': "10.204.217.38/32",
                'gateway': "10.204.217.38"
            }
        ]
        self.add_cidr_namespace(fabric, ns_name, ns_subnets, 'label=fabric-management-ip')

	fabric = self._vnc_lib.fabric_read(id=fabric_uuid)
        np, rc = self.create_node_profile('node-profile-1',
            device_family='junos-qfx', job_template=jt)

	pr1_mgmtip = '1.1.1.1'
	pr2_mgmtip = '1.1.1.2'	
        bgp_router1, pr1 = self.create_router('router1', pr1_mgmtip,
                                   role='spine', ignore_bgp=False, fabric=fabric,
                                   node_profile=np, physical_router_underlay_managed= True)
	bgp_router2, pr2 = self.create_router('router2', pr2_mgmtip,
                                   role='leaf', ignore_bgp=False, fabric=fabric,
                                   node_profile=np, physical_router_underlay_managed= True)
	
        pr1.set_physical_router_loopback_ip('10.10.0.1')
	pr2.set_physical_router_loopback_ip('10.10.0.2')

	
	## PR1 ##
        pi1 = PhysicalInterface('xe-0/0/0', parent_obj=pr1)
        self._vnc_lib.physical_interface_create(pi1)
        self._vnc_lib.physical_interface_update(pi1)
	# vn
	vn1_name = 'vn-private'
        vn1_obj = VirtualNetwork(vn1_name)
        ipam_obj = NetworkIpam('ipam1')
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("10.0.0.0", 24))]))
        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
	# vmi
	fq_name = ['default-domain', 'default-project', 'vmi1']
        vmi = VirtualMachineInterface(fq_name=fq_name, parent_type = 'project')
        vmi.set_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi)
	#li
        li1 = LogicalInterface('li1', parent_obj = pi1)
        li1.set_virtual_machine_interface(vmi)
        li1_id = self._vnc_lib.logical_interface_create(li1)
	# iip
        vmi = self._vnc_lib.virtual_machine_interface_read(vmi.get_fq_name())
        ip_obj1 = InstanceIp(name='inst-ip-1')
        ip_obj1.set_virtual_machine_interface(vmi)
        ip_obj1.set_virtual_network(vn1_obj)
        ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)
        ip_obj1 = self._vnc_lib.instance_ip_read(id=ip_id1)
        ip_addr1 = ip_obj1.get_instance_ip_address()
	# Update PR with li, pr
	self._vnc_lib.logical_interface_update(li1)
        self._vnc_lib.physical_router_update(pr1)

	## PR2  ## 
	pi2 = PhysicalInterface('xe-0/0/1', parent_obj=pr2)
        self._vnc_lib.physical_interface_create(pi2)
        self._vnc_lib.physical_interface_update(pi2)
	# vn
	vn2_name = 'vn-public'
        vn2_obj = VirtualNetwork(vn2_name)
        ipam_obj = NetworkIpam('ipam2')
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn2_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("20.0.0.0", 24))]))
        vn2_uuid = self._vnc_lib.virtual_network_create(vn2_obj)
	# vmi
        fq_name = ['default-domain', 'default-project', 'vmi2']
        vmi = VirtualMachineInterface(fq_name=fq_name, parent_type = 'project')
        vmi.set_virtual_network(vn2_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi)
	# li
        li2 = LogicalInterface('li2', parent_obj = pi2)
        li2.set_virtual_machine_interface(vmi)
        li2_id = self._vnc_lib.logical_interface_create(li2)
	# iip
        vmi = self._vnc_lib.virtual_machine_interface_read(vmi.get_fq_name())
        ip_obj2 = InstanceIp(name='inst-ip-2')
        ip_obj2.set_virtual_machine_interface(vmi)
        ip_obj2.set_virtual_network(vn2_obj)
        ip_id2 = self._vnc_lib.instance_ip_create(ip_obj2)
        ip_obj2 = self._vnc_lib.instance_ip_read(id=ip_id2)
        ip_addr2 = ip_obj2.get_instance_ip_address()
	# Update PR with li, pr
	self._vnc_lib.logical_interface_update(li2)
        self._vnc_lib.physical_router_update(pr2)

	#bgp sessions
	families = AddressFamilies(['route-target', 'inet-vpn', 'e-vpn'])
        bgp_sess_attrs = [BgpSessionAttributes(address_families=families)]
        bgp_sessions = [BgpSession(attributes=bgp_sess_attrs)]
        bgp_peering_attrs = BgpPeeringAttributes(session=bgp_sessions)
        bgp_router1.add_bgp_router(bgp_router2, bgp_peering_attrs)
        self._vnc_lib.bgp_router_update(bgp_router1)

        abstract_config = self.check_dm_ansible_config_push()
	device_abstract_config = abstract_config.get(
            'device_abstract_config')
	print ("this is abstract_config {}".format(device_abstract_config))
	#import pdb; pdb.set_trace()

	## VALIDATION ##
	self.assertEqual(abstract_config['manage_underlay'], True)
	self.assertEqual(abstract_config['device_management_ip'], pr2_mgmtip)
	system = abstract_config['device_abstract_config'].get('system')
	self.assertEqual(system.get('physical_role'), 'leaf')
	self.assertEqual(system.get('loopback_ip_list'), ['10.10.0.2'])
	self.assertEqual(system.get('name'), 'router2')
	self.assertEqual(system.get('tunnel_ip'), '10.10.0.2')
	# bgp validation
	bgp = abstract_config['device_abstract_config'].get('bgp')
	bgp = bgp[0]
	self.assertEqual(bgp.get('autonomous_system'), 64512)
	self.assertEqual(bgp.get('families'), ['route-target', 'inet-vpn', 'evpn', 'inet6-vpn'])
	#peer info
	bgp_peer = bgp.get('peers')
	bgp_peer = bgp_peer[0]
        self.assertEqual(bgp_peer.get('autonomous_system'), 64512)
	self.assertEqual(bgp_peer.get('ip_address'), '1.1.1.1')
	




    def create_feature_objects_and_params(self):
        self.create_features(['underlay-ip-clos'])
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles(['crb-gateway', 'crb-access'])
        self.create_role_definitions([
            AttrDict({
                'name': 'underlay_ip_clos-spine',
                'physical_role': 'spine',
                'overlay_role': 'crb-gateway',
                'features': ['underlay-ip-clos'],
                'feature_configs': None
            }),
	   AttrDict({
                'name': 'underlay_ip_clos-leaf',
                'physical_role': 'leaf',
                'overlay_role': 'crb-access',
                'features': ['underlay-ip-clos'],
                'feature_configs': None
            })
        ])

    def create_fabric(self, name):
        fab = Fabric(
            name=name,
            manage_underlay=True,
            fabric_credentials={
                'device_credential': [{
                    'credential': {
                        'username': 'root', 'password': '123'
                    },
                    'vendor': 'Juniper',
                    'device_family': None
                }]
            }
        )
        fab_uuid = self._vnc_lib.fabric_create(fab)
        return fab_uuid

