#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from builtins import str
import logging


import cfgm_common
from cfgm_common.exceptions import NoIdError
import gevent
from testtools import ExpectedException
from vnc_api.vnc_api import Fabric
from vnc_api.vnc_api import FabricNamespace
from vnc_api.vnc_api import NamespaceValue
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import RoutedProperties
from vnc_api.vnc_api import SubnetListType
from vnc_api.vnc_api import SubnetType
from vnc_api.vnc_api import VirtualNetworkRoutedPropertiesType

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class TestFabricNamespaceBase(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestFabricNamespaceBase, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestFabricNamespaceBase, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib


class TestFabricNamespace(TestFabricNamespaceBase):

    def basic_setup(self):
        proj_obj = Project('project')
        self.api.project_create(proj_obj)

        fabric_obj = Fabric('fabric')
        self.api.fabric_create(fabric_obj)

    def _get_loopback_vn_fq_name(self, fabric_name):
        return ["default-domain",
                "default-project",
                fabric_name + "-overlay-loopback-network"
                ]

    def _get_loopback_ipam_fq_name(self, fabric_name):
        return ["default-domain",
                "default-project",
                fabric_name + "-overlay-loopback-network-ipam"
                ]

    def _validate_vn_and_ipam_obj(self, vn, ipam):
        self.assertEqual(vn.get_fq_name(),
                         self._get_loopback_vn_fq_name('fabric'))
        self.assertEqual(vn.get_virtual_network_category(),
                         "routed")
        self.assertEqual(vn.get_address_allocation_mode(),
                         "flat-subnet-only")
        self.assertEqual(vn.get_parent_fq_name(),
                         ['default-domain', 'default-project'])
        self.assertEqual(ipam.get_ipam_subnet_method(),
                         "flat-subnet")
        self.assertEqual(ipam.get_fq_name(),
                         self._get_loopback_ipam_fq_name('fabric'))

    def _check_subnet_VN_IPAM(self, fn_subnets):
        fn_subnet_list = []
        for subnet in fn_subnets:
            fn_subnet_list.append(subnet.get('prefix') + '/' +
                                  str(subnet.get('len')))
        # Check overlay loopback VN got created
        vn = None
        vn = self.api.virtual_network_read(
            fq_name=self._get_loopback_vn_fq_name('fabric'))
        self.assertIsNotNone(vn)
        network_ipam_ref = vn.get_network_ipam_refs()
        self.assertIsNotNone(network_ipam_ref)
        ipam_uuid = network_ipam_ref[-1].get('uuid', None)
        self.assertIsNotNone(ipam_uuid)
        ipam_obj = self.api.network_ipam_read(id=ipam_uuid)
        self.assertIsNotNone(ipam_obj)
        self._validate_vn_and_ipam_obj(vn, ipam_obj)
        subnets = ipam_obj.ipam_subnets
        self.assertIsNotNone(subnets)
        for subnet in subnets.get_subnets():
            ip_prefix = subnet.subnet.ip_prefix
            ip_pre_len = subnet.subnet.ip_prefix_len
            cidr = ip_prefix + "/" + str(ip_pre_len)
            if cidr not in fn_subnet_list:
                self.assertTrue(False,
                                'Subnet in loopback IPAM does not match')

    def _create_and_check_fn(self, fn_subnets):
        subnets = []
        for subnet in fn_subnets or []:
            subnets.append(SubnetType(subnet.get('prefix'),
                                      subnet.get('len')))
        namespace = FabricNamespace(
            name="overlay-loopback-subnets",
            fq_name=["default-global-system-config",
                     "fabric", "overlay-loopback-subnets"],
            parent_type='fabric',
            fabric_namespace_type='IPV4-CIDR',
            fabric_namespace_value=NamespaceValue(ipv4_cidr=SubnetListType(
                                                  subnet=subnets)))
        try:
            fn_uuid = self.api.fabric_namespace_create(namespace)
        except NoIdError:
            return None
        # Test 1 validate is create of FN is successful
        self.assertIsNotNone(fn_uuid, "Unable to create fabric namespace")
        self._check_subnet_VN_IPAM(fn_subnets)
        return fn_uuid

    def _update_existing_fabric_namespace(self, fn_subnets, fn_id):
        subnets = []
        for subnet in fn_subnets or []:
            subnets.append(SubnetType(subnet.get('prefix'),
                                      subnet.get('len')))
        namespace = FabricNamespace(
            name="overlay-loopback-subnets",
            fq_name=["default-global-system-config",
                     "fabric", "overlay-loopback-subnets"],
            parent_type='fabric',
            fabric_namespace_type='IPV4-CIDR',
            fabric_namespace_value=NamespaceValue(ipv4_cidr=SubnetListType(
                                                  subnet=subnets)))
        try:
            self.api.fabric_namespace_update(namespace)
        except NoIdError:
            self.assertTrue(False, 'Could not find fabric namespace')
        # Test 1 validate is create of FN is successful
        self._check_subnet_VN_IPAM(fn_subnets)

    def _delete_fabric_namespace(self, fn_uuid):
        fabric_fq_name = ["default-global-system-config", "fabric"]
        self.api.fabric_namespace_delete(id=fn_uuid)
        try:
            vn = self.api.virtual_network_read(
                fq_name=self._get_loopback_vn_fq_name('fabric'))
            iip_refs = vn.get_instance_ip_back_refs() or []
            self.assertIsNotNone(iip_refs)
            ip_ref_list = []
            for iip_ref in iip_refs:
                ip_ref_list.append(iip_ref.get('uuid', None))
            for iip_uuid in ip_ref_list or []:
                self.api.instance_ip_delete(id=iip_uuid)

            fab = self.api.fabric_read(fq_name=fabric_fq_name)
            fab.set_virtual_network_list([])
            self.api.fabric_update(fab)
            self.api.virtual_network_delete(
                fq_name=self._get_loopback_vn_fq_name('fabric'))
            self.api.network_ipam_delete(
                fq_name=self._get_loopback_ipam_fq_name('fabric'))
        except Exception as e:
            self.assertIsNone(e)

    def test_fabric_namespace_creation_with_one_subnet(self):
        loopback_subnet = [{'prefix': '1.1.1.0',
                            'len': 24}]
        self.basic_setup()
        fn_uuid = self._create_and_check_fn(loopback_subnet)
        # delete Fabric Namespace
        self._delete_fabric_namespace(fn_uuid)

    def test_faric_routed_vn_instance_with_two_subnet(self):
        loopback_subnet = [{'prefix': '1.1.1.0',
                            'len': 24},
                           {'prefix': '2.2.2.0',
                            'len': 24}]
        fn_uuid = self._create_and_check_fn(loopback_subnet)
        # delete Fabric Namespace
        self._delete_fabric_namespace(fn_uuid)

    def test_faric_routed_vn_upgrade_one_with_two_subnet(self):
        loopback_subnet = [{'prefix': '1.1.1.0',
                            'len': 24}]
        fn_uuid = self._create_and_check_fn(loopback_subnet)
        loopback_subnet = [{'prefix': '1.1.1.0',
                            'len': 24},
                           {'prefix': '2.2.2.0',
                            'len': 24}]
        self._update_existing_fabric_namespace(loopback_subnet, fn_uuid)
        # delete Fabric Namespace
        self._delete_fabric_namespace(fn_uuid)

    def test_loopback_routed_vn_instance_ip_alloc_dealloc(self):
        loopback_subnet = [{'prefix': '1.1.1.0',
                            'len': 24}]
        fn_uuid = self._create_and_check_fn(loopback_subnet)
        # Read loopback VN
        vn = self.api.virtual_network_read(
            fq_name=self._get_loopback_vn_fq_name('fabric'))
        self.assertIsNotNone(vn)
        vn_routed_props = VirtualNetworkRoutedPropertiesType()
        pr_uuid = '6fbefc85-25fe-4dcc-a69a-dd5158b5a32f'
        lr_uuid = 'b89e131b-4b7e-4eff-9706-4c4a58a814a8'
        routed_props = RoutedProperties(
            physical_router_uuid=pr_uuid,
            logical_router_uuid=lr_uuid,
            routed_interface_ip_address='1.1.1.1',
            loopback_ip_address='1.1.1.5',
            routing_protocol=None,
            bgp_params=None,
            bfd_params=None,
            static_route_params=None,
            routing_policy_params=None)
        vn.set_virtual_network_category('routed')
        vn_routed_props.add_routed_properties(routed_props)
        vn.set_virtual_network_routed_properties(vn_routed_props)
        self.api.virtual_network_update(vn)
        gevent.sleep(3)

        # again read the VN
        vn = self.api.virtual_network_read(
            fq_name=self._get_loopback_vn_fq_name('fabric'))
        self.assertIsNotNone(vn)

        # Verify instance IP is create with ip 1.1.1.5
        iip_refs = vn.get_instance_ip_back_refs()
        self.assertIsNotNone(iip_refs)
        ip_ref_list = []
        for iip_ref in iip_refs or []:
            ip_ref_list.append(iip_ref.get('uuid', None))
        ip_list = []
        for iip_uuid in ip_ref_list or []:
            iip = self.api.instance_ip_read(id=iip_uuid)
            self.assertIsNotNone(iip)
            ip_list.append(iip.get_instance_ip_address())
        if '1.1.1.5' not in ip_list:
            self.assertTrue(False)
        # add routed parameters loopback ip  1.1.1.3
        routed_props = RoutedProperties(
            physical_router_uuid=pr_uuid,
            logical_router_uuid=lr_uuid,
            routed_interface_ip_address='1.1.1.1',
            loopback_ip_address='1.1.1.3',
            routing_protocol=None,
            bgp_params=None,
            bfd_params=None,
            static_route_params=None,
            routing_policy_params=None)
        vn.set_virtual_network_category('routed')
        vn_routed_props.add_routed_properties(routed_props)
        vn.set_virtual_network_routed_properties(vn_routed_props)
        self.api.virtual_network_update(vn)
        gevent.sleep(3)

        # add routed parameters loopback ip  None
        routed_props = RoutedProperties(
            physical_router_uuid=pr_uuid,
            logical_router_uuid=lr_uuid,
            routed_interface_ip_address='1.1.1.1',
            loopback_ip_address=None,
            routing_protocol=None,
            bgp_params=None,
            bfd_params=None,
            static_route_params=None,
            routing_policy_params=None)
        vn.set_virtual_network_category('routed')
        vn_routed_props.add_routed_properties(routed_props)
        vn.set_virtual_network_routed_properties(vn_routed_props)
        self.api.virtual_network_update(vn)

        # again read the VN
        vn = self.api.virtual_network_read(
            fq_name=self._get_loopback_vn_fq_name('fabric'))
        self.assertIsNotNone(vn)

        # Verify instance IP is create with ip 1.1.1.3
        iip_refs = vn.get_instance_ip_back_refs()
        self.assertIsNotNone(iip_refs)
        ip_ref_list = []
        for iip_ref in iip_refs or []:
            ip_ref_list.append(iip_ref.get('uuid', None))
        ip_list = []
        for iip_uuid in ip_ref_list or []:
            iip = self.api.instance_ip_read(id=iip_uuid)
            self.assertIsNotNone(iip)
            ip_list.append(iip.get_instance_ip_address())
        if '1.1.1.3' not in ip_list:
            self.assertTrue(False,
                            'Instance IP for 1.1.1.3 not allocated')
        # Verify instance IP ip 1.1.1.5 is deleted
        if '1.1.1.5' not in ip_list:
            self.assertTrue(False,
                            'Instance IP for 1.1.1.5 not allocated')
        # verify instance IP for loopbackNone is created
        if len(ip_list) != 3:
            self.assertTrue(False,
                            "Instance IP for loopback None is not allocated")
        # delete Fabric Namespace
        self._delete_fabric_namespace(fn_uuid)

    def test_fabric_namespace_creation_with_no_subnet(self):
        loopback_subnet = []
        fn_uuid = self._create_and_check_fn(loopback_subnet)
        self.assertIsNone(fn_uuid)

    def test_loopback_routed_vn_instance_ip_alloc_notfrom_subnet(self):
        loopback_subnet = [{'prefix': '1.1.1.0',
                            'len': 24}]
        fn_uuid = self._create_and_check_fn(loopback_subnet)
        # Read loopback VN
        vn = self.api.virtual_network_read(
            fq_name=self._get_loopback_vn_fq_name('fabric'))
        self.assertIsNotNone(vn)
        vn_routed_props = VirtualNetworkRoutedPropertiesType()
        pr_uuid = '6fbefc85-25fe-4dcc-a69a-dd5158b5a32f'
        lr_uuid = 'b89e131b-4b7e-4eff-9706-4c4a58a814a8'
        routed_props = RoutedProperties(
            physical_router_uuid=pr_uuid,
            logical_router_uuid=lr_uuid,
            routed_interface_ip_address='1.1.1.1',
            loopback_ip_address='2.3.3.5',
            routing_protocol=None,
            bgp_params=None,
            bfd_params=None,
            static_route_params=None,
            routing_policy_params=None)
        vn.set_virtual_network_category('routed')
        vn_routed_props.add_routed_properties(routed_props)
        vn.set_virtual_network_routed_properties(vn_routed_props)
        with ExpectedException(cfgm_common.exceptions.HttpError):
            self.api.virtual_network_update(vn)
            gevent.sleep(3)
        # again read the VN
        vn = self.api.virtual_network_read(
            fq_name=self._get_loopback_vn_fq_name('fabric'))
        self.assertIsNotNone(vn)

        # Verify no instance IP is allocated
        iip_refs = vn.get_instance_ip_back_refs()
        self.assertIsNone(iip_refs)
        # Test PR and LR uuid None
        vn_routed_props = VirtualNetworkRoutedPropertiesType()
        routed_props = RoutedProperties(
            routed_interface_ip_address='1.1.1.1',
            routing_protocol=None,
            bgp_params=None,
            bfd_params=None,
            static_route_params=None,
            routing_policy_params=None)
        vn.set_virtual_network_category('routed')
        vn_routed_props.add_routed_properties(routed_props)
        vn.set_virtual_network_routed_properties(vn_routed_props)
        try:
            self.api.virtual_network_update(vn)
            gevent.sleep(3)
        except Exception as e:
            self.assertEqual(str(e),
                             "Physical router Id not present for routed VN")
        # delete Fabric Namespace
        self._delete_fabric_namespace(fn_uuid)
