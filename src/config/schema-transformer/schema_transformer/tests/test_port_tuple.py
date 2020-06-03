#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import gevent
import logging

from vnc_api.vnc_api import PortTuple
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import RoutingPolicyType, SequenceType
from vnc_api.vnc_api import ServiceInstance
from vnc_api.vnc_api import ServiceTemplate
from vnc_api.vnc_api import VirtualNetwork, VirtualNetworkPolicyType
from vnc_api.vnc_api import VirtualMachineInterface
from vnc_api.vnc_api import VirtualMachineInterfacePropertiesType
from vnc_api.vnc_api import KeyValuePair
from vnc_api.vnc_api import KeyValuePairs
from vnc_api.vnc_api import ServiceInstanceType
from vnc_api.vnc_api import ServiceTemplateInterfaceType
from vnc_api.vnc_api import ServiceTemplateType

from schema_transformer.resources.service_chain import ServiceChain

from .test_case import retries, STTestCase

logger = logging.getLogger(__name__)


class TestPortTupleBase(STTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestPortTupleBase, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestPortTupleBase, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib


class TestVsrxPortTuple(TestPortTupleBase):
    def setUp(self):
        super(TestVsrxPortTuple, self).setUp()
        logger.debug("setUp called")
        proj_obj = self.api.project_read(Project().fq_name)

        # Create Virtual Networks
        vn1_name = self.id() + 'vn-left'
        self.vn_left_obj = self.create_virtual_network(vn1_name,
                                                       ['10.0.0.0/24'])
        self.vn_left_uuid = self.vn_left_obj.get_uuid()

        vn2_name = self.id() + 'vn-right'
        self.vn_right_obj = self.create_virtual_network(vn2_name,
                                                        ['20.0.0.0/24'])
        self.vn_right_uuid = self.vn_right_obj.get_uuid()

        vn3_name = self.id() + 'vn-mgt'
        self.vn_mgt_obj = self.create_virtual_network(vn3_name,
                                                      ['30.0.0.0/24'])
        self.vn_mgt_uuid = self.vn_mgt_obj.get_uuid()

        # Create Virtual Machine Interfaces
        vmi_left = VirtualMachineInterface('%s-vmi-left' % self.id(),
                                           parent_obj=proj_obj)
        vmi_left_props = VirtualMachineInterfacePropertiesType(
            service_interface_type='left')
        vmi_left.set_virtual_machine_interface_properties(vmi_left_props)
        vmi_left.add_virtual_network(self.vn_left_obj)
        self.vmi_left_uuid = self.api.virtual_machine_interface_create(
            vmi_left)

        vmi_right = VirtualMachineInterface('%s-vmi-right' % self.id(),
                                            parent_obj=proj_obj)
        vmi_right_props = VirtualMachineInterfacePropertiesType(
            service_interface_type='right')
        vmi_right.set_virtual_machine_interface_properties(vmi_right_props)
        vmi_right.add_virtual_network(self.vn_right_obj)
        self.vmi_right_uuid = self.api.virtual_machine_interface_create(
            vmi_right)

        vmi_mgt = VirtualMachineInterface('%s-vmi-mgt' % self.id(),
                                          parent_obj=proj_obj)
        vmi_mgt_props = VirtualMachineInterfacePropertiesType(
            service_interface_type='management')
        vmi_mgt.set_virtual_machine_interface_properties(vmi_mgt_props)
        vmi_mgt.add_virtual_network(self.vn_mgt_obj)
        self.vmi_mgt_uuid = self.api.virtual_machine_interface_create(vmi_mgt)

        # Create Virtual Machine Interfaces
        vmi_left_2 = VirtualMachineInterface('%s-vmi2-left' % self.id(),
                                             parent_obj=proj_obj)
        vmi_left_2_props = VirtualMachineInterfacePropertiesType(
            service_interface_type='left')
        vmi_left_2.set_virtual_machine_interface_properties(vmi_left_2_props)
        vmi_left_2.add_virtual_network(self.vn_left_obj)
        self.vmi_left_2_uuid = self.api.virtual_machine_interface_create(
            vmi_left_2)

        vmi_right_2 = VirtualMachineInterface('%s-vmi2-right' % self.id(),
                                              parent_obj=proj_obj)
        vmi_right_2_props = VirtualMachineInterfacePropertiesType(
            service_interface_type='right')
        vmi_right_2.set_virtual_machine_interface_properties(vmi_right_2_props)
        vmi_right_2.add_virtual_network(self.vn_right_obj)
        self.vmi_right_2_uuid = self.api.virtual_machine_interface_create(
            vmi_right_2)

        vmi_mgt_2 = VirtualMachineInterface('%s-vmi2-mgt' % self.id(),
                                            parent_obj=proj_obj)
        vmi_mgt_2_props = VirtualMachineInterfacePropertiesType(
            service_interface_type='management')
        vmi_mgt_2.set_virtual_machine_interface_properties(vmi_mgt_props)
        vmi_mgt_2.add_virtual_network(self.vn_mgt_obj)
        self.vmi_mgt_2_uuid = self.api.virtual_machine_interface_create(
            vmi_mgt_2)

        # Create Service template
        st_obj = ServiceTemplate(name='st-' + self.id())
        svc_properties = ServiceTemplateType()
        svc_properties.set_service_virtualization_type('virtual-machine')
        if_type = ServiceTemplateInterfaceType()
        if_type.set_service_interface_type('left')
        svc_properties.add_interface_type(if_type)
        if_type = ServiceTemplateInterfaceType()
        if_type.set_service_interface_type('right')
        svc_properties.add_interface_type(if_type)
        if_type = ServiceTemplateInterfaceType()
        if_type.set_service_interface_type('management')
        svc_properties.add_interface_type(if_type)
        st_obj.set_service_template_properties(svc_properties)
        self.st_uuid = self.api.service_template_create(st_obj)

        # Create Service Instance object
        si_fqn = ['default-domain', 'default-project', 'si-' + self.id()]
        si_obj = ServiceInstance(fq_name=si_fqn)
        si_obj.fq_name = si_fqn
        si_obj.add_service_template(st_obj)
        kvp_array = []
        kvp = KeyValuePair("management", self.vn_mgt_uuid)
        kvp_array.append(kvp)
        kvp = KeyValuePair("left", self.vn_left_uuid)
        kvp_array.append(kvp)
        kvp = KeyValuePair("right", self.vn_right_uuid)
        kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)
        si_obj.set_annotations(kvps)

        props = ServiceInstanceType()
        props.set_service_virtualization_type('virtual-machine')
        si_obj.set_service_instance_properties(props)
        self.si_uuid = self.api.service_instance_create(si_obj)

    def tearDown(self):
        super(TestVsrxPortTuple, self).tearDown()
        logger.debug("TearDown called")

        self.api.service_instance_delete(id=self.si_uuid)
        self.api.service_template_delete(id=self.st_uuid)
        self.api.virtual_machine_interface_delete(id=self.vmi_mgt_uuid)
        self.api.virtual_machine_interface_delete(id=self.vmi_right_uuid)
        self.api.virtual_machine_interface_delete(id=self.vmi_left_uuid)
        # self.api.virtual_network_delete(id=self.vn_mgt_uuid)
        # self.api.virtual_network_delete(id=self.vn_right_uuid)
        # self.api.virtual_network_delete(id=self.vn_left_uuid)

    def get_ri_name(self, vn, ri_name=None):
        return vn.get_fq_name() + [ri_name or vn.name]

    @retries(5)
    def check_ri_ref_present(self, fq_name, to_fq_name):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        for ri_ref in ri.get_routing_instance_refs() or []:
            if ri_ref['to'] == to_fq_name:
                return
        raise Exception('ri_ref not found from %s to %s' % (fq_name,
                                                            to_fq_name))

    @retries(5)
    def wait_to_get_sc(self, left_vn=None, right_vn=None, si_name=None,
                       check_create=False):
        for sc in list(ServiceChain.values()):
            if (left_vn in (None, sc.left_vn) and
                    right_vn in (None, sc.right_vn) and
                    si_name in (sc.service_list[0], None)):
                if check_create and not sc.created:
                    raise Exception('Service chain not created')
                return sc.name
        raise Exception('Service chain not found')

    def test_pt_changing_its_si(self):

        service_name = self.id() + 's1'
        np = self.create_network_policy(self.vn_left_obj, self.vn_right_obj,
                                        [service_name])
        si_obj = self.api.service_instance_read(id=self.si_uuid)
        gevent.sleep(2)

        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)

        self.vn_left_obj.set_network_policy(np, vnp)
        self.vn_right_obj.set_network_policy(np, vnp)

        self._vnc_lib.virtual_network_update(self.vn_left_obj)
        self._vnc_lib.virtual_network_update(self.vn_right_obj)
        gevent.sleep(2)

        # Construct PT object
        pt_obj = PortTuple('pt-' + self.id(), parent_obj=si_obj)
        self.api.port_tuple_create(pt_obj)

        left_vmi = self.api.virtual_machine_interface_read(
            id=self.vmi_left_uuid)
        right_vmi = self.api.virtual_machine_interface_read(
            id=self.vmi_right_uuid)
        mgt_vmi = self.api.virtual_machine_interface_read(id=self.vmi_mgt_uuid)
        right_vmi.add_port_tuple(pt_obj)
        self._vnc_lib.virtual_machine_interface_update(right_vmi)
        left_vmi.add_port_tuple(pt_obj)
        self._vnc_lib.virtual_machine_interface_update(left_vmi)
        mgt_vmi.add_port_tuple(pt_obj)
        self._vnc_lib.virtual_machine_interface_update(mgt_vmi)

        # Construct PT object
        pt_obj_2 = PortTuple('pt2-' + self.id(), parent_obj=si_obj)
        self.api.port_tuple_create(pt_obj_2)

        left_vmi_2 = self.api.virtual_machine_interface_read(
            id=self.vmi_left_2_uuid)
        right_vmi_2 = self.api.virtual_machine_interface_read(
            id=self.vmi_right_2_uuid)
        mgt_vmi_2 = self.api.virtual_machine_interface_read(
            id=self.vmi_mgt_2_uuid)
        right_vmi_2.add_port_tuple(pt_obj_2)
        self._vnc_lib.virtual_machine_interface_update(right_vmi_2)
        left_vmi_2.add_port_tuple(pt_obj_2)
        self._vnc_lib.virtual_machine_interface_update(left_vmi_2)
        mgt_vmi_2.add_port_tuple(pt_obj_2)
        self._vnc_lib.virtual_machine_interface_update(mgt_vmi_2)

        sc_id = self.wait_to_get_sc(
            left_vn=self.vn_left_obj.get_fq_name_str(),
            right_vn=self.vn_right_obj.get_fq_name_str(),
            check_create=True)
        sc_ri_name = 'service-' + sc_id + '-default-domain_default-project_' \
                     + service_name
        self.check_ri_ref_present(self.get_ri_name(self.vn_right_obj),
                                  self.get_ri_name(self.vn_right_obj,
                                                   sc_ri_name))
        gevent.sleep(1)

        self.assertEqual(1, len(right_vmi.get_port_tuple_refs()))
        self.assertEqual(1, len(left_vmi.get_port_tuple_refs()))
        self.assertEqual(1, len(mgt_vmi.get_port_tuple_refs()))

        self.assertEqual(1, len(mgt_vmi.get_routing_instance_refs()))
        gevent.sleep(1)
        # They should have 2 refs, but have only one -
        # ref with service chain's attr is not appearing
        self.assertEqual(1, len(left_vmi.get_routing_instance_refs()))
        self.assertEqual(1, len(right_vmi.get_routing_instance_refs()))

        # Cleanup
        left_vmi.del_port_tuple(pt_obj)
        self._vnc_lib.virtual_machine_interface_update(left_vmi)
        right_vmi.del_port_tuple(pt_obj)
        self._vnc_lib.virtual_machine_interface_update(right_vmi)
        mgt_vmi.del_port_tuple(pt_obj)
        self._vnc_lib.virtual_machine_interface_update(mgt_vmi)

        self.api.port_tuple_delete(id=pt_obj.uuid)

        self.vn_left_obj.del_network_policy(np)
        self.vn_right_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(self.vn_left_obj)
        self._vnc_lib.virtual_network_update(self.vn_right_obj)
        self.api.network_policy_delete(fq_name=np.get_fq_name())
