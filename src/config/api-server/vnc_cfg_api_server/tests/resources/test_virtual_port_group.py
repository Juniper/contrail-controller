#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from builtins import int
from builtins import range
from builtins import str
from collections import defaultdict
import json
import logging
import os
import re
from unittest import skip

from cfgm_common.exceptions import BadRequest
from cfgm_common.exceptions import NoIdError
from cfgm_common.tests import test_common
from cfgm_common.zkclient import ZookeeperLock
import gevent
import mock
from testtools import ExpectedException
from vnc_api.exceptions import HttpError as vnc_api_HttpError
from vnc_api.gen.resource_xsd import KeyValuePair
from vnc_api.gen.resource_xsd import KeyValuePairs
from vnc_api.vnc_api import Fabric
from vnc_api.vnc_api import PhysicalInterface
from vnc_api.vnc_api import PhysicalRouter
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import VirtualMachineInterface
from vnc_api.vnc_api import VirtualMachineInterfacePropertiesType
from vnc_api.vnc_api import VirtualNetwork
from vnc_api.vnc_api import VirtualPortGroup

from vnc_cfg_api_server.tests import test_case
from vnc_cfg_api_server.vnc_db import VncDbClient

logger = logging.getLogger(__name__)


class TestVirtualPortGroupBase(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestVirtualPortGroupBase, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestVirtualPortGroupBase, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib


class TestVirtualPortGroup(TestVirtualPortGroupBase):

    VMI_NUM = 2

    def test_virtual_port_group_name_with_internal_negative(self):
        proj_obj = Project('%s-project' % (self.id()))
        self.api.project_create(proj_obj)

        fabric_obj = Fabric('%s-fabric' % (self.id()))
        self.api.fabric_create(fabric_obj)

        vn = VirtualNetwork('vn-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn)

        vpg_name_err = "vpg-internal-" + self.id()
        vpg_obj_err = VirtualPortGroup(vpg_name_err, parent_obj=fabric_obj)

        # Make sure that api server throws an error if
        # VPG is created externally with prefix vpg-internal in the name.
        with ExpectedException(BadRequest):
            self.api.virtual_port_group_create(vpg_obj_err)

        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_virtual_port_group_delete(self):
        proj_obj = Project('%s-project' % (self.id()))
        self.api.project_create(proj_obj)

        fabric_obj = Fabric('%s-fabric' % (self.id()))
        self.api.fabric_create(fabric_obj)

        vn = VirtualNetwork('vn-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn)

        vpg_name = "vpg-" + self.id()
        vpg_obj = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        self.api.virtual_port_group_create(vpg_obj)

        vmi_id_list = []
        for i in range(self.VMI_NUM):
            vmi_obj = VirtualMachineInterface(self.id() + str(i),
                                              parent_obj=proj_obj)
            vmi_obj.set_virtual_network(vn)
            vmi_id_list.append(
                self.api.virtual_machine_interface_create(vmi_obj))
            vpg_obj.add_virtual_machine_interface(vmi_obj)
            self.api.virtual_port_group_update(vpg_obj)
            self.api.ref_relax_for_delete(vpg_obj.uuid, vmi_id_list[i])

        # Make sure when VPG doesn't get deleted, since associated VMIs
        # still refers it.
        with ExpectedException(BadRequest):
            self.api.virtual_port_group_delete(id=vpg_obj.uuid)

        # Cleanup
        for i in range(self.VMI_NUM):
            self.api.virtual_machine_interface_delete(id=vmi_id_list[i])

        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    # end test_virtual_port_group_delete

    def _create_prerequisites(
        self, enterprise_style_flag=True, create_second_pr=False,
            disable_vlan_vn_uniqueness_check=False):
        # Create project first
        proj_obj = Project('%s-project' % (self.id()))
        self.api.project_create(proj_obj)

        # Create Fabric with enterprise style flag set to false
        fabric_obj = Fabric('%s-fabric' % (self.id()))
        fabric_obj.set_fabric_enterprise_style(enterprise_style_flag)
        fabric_obj.set_disable_vlan_vn_uniqueness_check(
            disable_vlan_vn_uniqueness_check)
        fabric_uuid = self.api.fabric_create(fabric_obj)
        fabric_obj = self.api.fabric_read(id=fabric_uuid)

        # Create physical router
        pr_name = self.id() + '_physical_router'
        pr = PhysicalRouter(pr_name)
        pr_uuid = self._vnc_lib.physical_router_create(pr)
        pr_obj = self._vnc_lib.physical_router_read(id=pr_uuid)

        if create_second_pr:
            pr_name_2 = self.id() + '2_physical_router'
            pr = PhysicalRouter(pr_name_2)
            pr_uuid_2 = self._vnc_lib.physical_router_create(pr)
            pr_obj_2 = self._vnc_lib.physical_router_read(id=pr_uuid_2)
            return proj_obj, fabric_obj, [pr_obj, pr_obj_2]

        return proj_obj, fabric_obj, pr_obj

    def _create_kv_pairs(self, pi_fq_name, fabric_name, vpg_name,
                         tor_port_vlan_id=0):
        # Populate binding profile to be used in VMI create
        binding_profile = {'local_link_information': []}
        if isinstance(pi_fq_name[0], type([])):
            for pi_name in pi_fq_name:
                binding_profile['local_link_information'].append(
                    {'port_id': pi_name[2],
                     'switch_id': pi_name[2],
                     'fabric': fabric_name[-1],
                     'switch_info': pi_name[1]})
        else:
            binding_profile['local_link_information'].append(
                {'port_id': pi_fq_name[2],
                 'switch_id': pi_fq_name[2],
                 'fabric': fabric_name[-1],
                 'switch_info': pi_fq_name[1]})

        if tor_port_vlan_id != 0:
            kv_pairs = KeyValuePairs(
                [KeyValuePair(key='vpg', value=vpg_name[-1]),
                 KeyValuePair(key='vif_type', value='vrouter'),
                 KeyValuePair(key='tor_port_vlan_id', value=tor_port_vlan_id),
                 KeyValuePair(key='vnic_type', value='baremetal'),
                 KeyValuePair(key='profile',
                              value=json.dumps(binding_profile))])
        else:
            kv_pairs = KeyValuePairs(
                [KeyValuePair(key='vpg', value=vpg_name[-1]),
                 KeyValuePair(key='vif_type', value='vrouter'),
                 KeyValuePair(key='vnic_type', value='baremetal'),
                 KeyValuePair(key='profile',
                              value=json.dumps(binding_profile))])

        return kv_pairs

    def _validate_untagged_vmis(self, fabric_obj, proj_obj, pi_obj):
        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name = pi_obj.get_fq_name()

        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        vn2 = VirtualNetwork('vn2-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn2)

        # Create VPG
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name = vpg_obj.get_fq_name()

        # If tor_port_vlan_id is set, then it signifies a untagged VMI
        # Create first untagged VMI and attach it to Virtual Port Group
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name,
                                         tor_port_vlan_id='4094')

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        vpg_obj.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj)

        # Now, try to create the second untagged VMI.
        # This should fail as there can be only one untagged VMI in a VPG
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn2)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name,
                                         tor_port_vlan_id='4092')

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)

        with ExpectedException(BadRequest):
            self.api.virtual_machine_interface_create(vmi_obj_2)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)

    def _esi_to_long(self, esi):
        res = re.match('^((?:(?:[0-9]{2}):){9}[0-9]{2})$', esi.lower())
        if res is None:
            raise ValueError('invalid esi address')
        return int(res.group(0).replace(':', ''), 16)

    def _long_to_esi(self, esiint):
        if type(esiint) != int:
            raise ValueError('invalid integer')
        return ':'.join(['{}{}'.format(a, b)
                         for a, b
                         in zip(*[iter('{:020x}'.format(esiint))] * 2)])

    def _get_nth_esi(self, esi, n):
        esi_long = self._esi_to_long(esi)
        return self._long_to_esi(esi_long + n)

    def _create_pi_objects(self, pr_objs, pi_names):
        pi_obj_dict = {}
        esi_start_id = '00:11:22:33:44:55:66:77:88:11'
        esi_count = 1
        if not isinstance(pr_objs, list):
            pr_objs = [pr_objs] * len(pi_names)
        for pi_name in pi_names:
            esi_id = self._get_nth_esi(esi_start_id, esi_count)
            pi = PhysicalInterface(name=pi_name,
                                   parent_obj=pr_objs[esi_count - 1],
                                   ethernet_segment_identifier=esi_id)
            pi_uuid = self._vnc_lib.physical_interface_create(pi)
            pi_obj_dict[pi_name] = self._vnc_lib.physical_interface_read(
                id=pi_uuid)
            esi_count += 1
        return pi_obj_dict

    def _create_vpgs(self, fabric_obj, vpg_names):
        vpg_obj_dict = {}
        for vpg_name in vpg_names:
            vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
            vpg_uuid = self.api.virtual_port_group_create(vpg)
            vpg_obj_dict[vpg_name] = self._vnc_lib.virtual_port_group_read(
                id=vpg_uuid)
        return vpg_obj_dict

    def _create_vns(self, project_obj, vn_names):
        vn_obj_dict = {}
        for vn_name in vn_names:
            vn = VirtualNetwork(vn_name, parent_obj=project_obj)
            vn_uuid = self.api.virtual_network_create(vn)
            vn_obj_dict[vn_name] = self.api.virtual_network_read(id=vn_uuid)
        return vn_obj_dict

    def _create_vmis(self, vmi_infos):
        vmi_obj_dict = {}
        for vmi_info in vmi_infos:
            vmi_name = vmi_info.get('name')
            vmi_parent = vmi_info.get('parent_obj')
            vmi_vn = vmi_info.get('vn')
            vmi_vpg_uuid = vmi_info.get('vpg')
            vmi_fabric = vmi_info.get('fabric')
            vmi_pis = vmi_info.get('pis')
            vmi_vlan = vmi_info.get('vlan')
            vmi_is_untagged = vmi_info.get('is_untagged')

            # create vmi obj
            vmi_obj = VirtualMachineInterface(vmi_name, parent_obj=vmi_parent)
            vmi_obj.set_virtual_network(vmi_vn)
            vmi_vpg = self.api.virtual_port_group_read(id=vmi_vpg_uuid)

            # Create KV_Pairs for this VMI
            if vmi_is_untagged:
                kv_pairs = self._create_kv_pairs(
                    vmi_pis, vmi_fabric, vmi_vpg.get_fq_name(),
                    tor_port_vlan_id=vmi_vlan)
            else:
                kv_pairs = self._create_kv_pairs(
                    vmi_pis, vmi_fabric, vmi_vpg.get_fq_name())
                vmi_obj.set_virtual_machine_interface_properties(
                    VirtualMachineInterfacePropertiesType(
                        sub_interface_vlan_tag=vmi_vlan))
            vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)
            vmi_uuid = self.api.virtual_machine_interface_create(vmi_obj)
            # add VMI to vpg
            vmi_vpg.add_virtual_machine_interface(vmi_obj)
            self.api.virtual_port_group_update(vmi_vpg)
            vmi_obj_dict[vmi_name] = self.api.virtual_machine_interface_read(
                id=vmi_uuid)
        return vmi_obj_dict

    def _update_vmis(self, vmi_infos):
        vmi_obj_dict = {}
        for vmi_info in vmi_infos:
            vmi_name = vmi_info.get('name')
            vmi_uuid = vmi_info.get('vmi_uuid')
            vmi_vpg_uuid = vmi_info.get('vpg')
            vmi_fabric = vmi_info.get('fabric')
            vmi_pis = vmi_info.get('pis')
            vmi_vlan = vmi_info.get('vlan')
            vmi_is_untagged = vmi_info.get('is_untagged')

            # read vmi, vpg obj
            vmi_obj = self.api.virtual_machine_interface_read(id=vmi_uuid)
            vmi_vpg = self.api.virtual_port_group_read(id=vmi_vpg_uuid)

            # Create KV_Pairs for this VMI
            if vmi_is_untagged:
                kv_pairs = self._create_kv_pairs(
                    vmi_pis, vmi_fabric, vmi_vpg.get_fq_name(),
                    tor_port_vlan_id=vmi_vlan)
            else:
                kv_pairs = self._create_kv_pairs(
                    vmi_pis, vmi_fabric, vmi_vpg.get_fq_name())
                vmi_obj.set_virtual_machine_interface_properties(
                    VirtualMachineInterfacePropertiesType(
                        sub_interface_vlan_tag=vmi_vlan))
            vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)
            vmi_uuid = self.api.virtual_machine_interface_update(vmi_obj)
            # add VMI to vpg
            vmi_vpg.add_virtual_machine_interface(vmi_obj)
            self.api.virtual_port_group_update(vmi_vpg)
            vmi_obj_dict[vmi_name] = self.api.virtual_machine_interface_read(
                id=vmi_obj.uuid)
        return vmi_obj_dict

    def test_zk_lock_vpg_annotations(self):
        """Verify ZK path is based on VPG UUID."""
        backup_zk_init = ZookeeperLock.__init__
        backup_zk_enter = ZookeeperLock.__enter__

        # mock enter of ZookeeperLock class to get access to configured
        # zk_path and lock name
        def mocked_enter(*args, **kwargs):
            self.assertTrue(
                hasattr(args[0], 'name'),
                'ZK Lock object do not have name attr '
                'Attr present: %s' % args[0].__dict__)
            return backup_zk_enter(*args, **kwargs)

        # mock init of ZookeeperLock class to get access to configured
        # zk_path and lock name
        def mocked_init(*args, **kwargs):
            zk_path = kwargs.get('path') or ''
            zk_lock_name = kwargs.get('name')
            zk_spath = zk_path.split('/') or ['']
            self.assertEqual(
                zk_spath[-1], zk_lock_name,
                'ZK Path (%s) do not include VPG UUID (%s)' % (
                    zk_path, zk_lock_name))
            return backup_zk_init(*args, **kwargs)

        with mock.patch.object(ZookeeperLock, '__init__', mocked_init):
            with mock.patch.object(ZookeeperLock, '__enter__', mocked_enter):
                self.test_reinit_adds_enterprise_annotations()

    # New Case, attaching PI directly to VPG
    def test_add_and_delete_pis_at_vpg(self):
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()

        def process_ae_ids(x):
            return [int(i) for i in sorted(x)]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 15
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr2_pi_names = ['%s_pr2_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr2_pi_objs = self._create_pi_objects(pr_objs[1], pr2_pi_names)
        pi_objs.update(pr1_pi_objs)
        pi_objs.update(pr2_pi_objs)

        # create ten VPGs
        vpg_count = 10
        vpg_names = ['vpg_%s_%s' % (test_id, i) for i in range(
                     1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        # Case 1
        # Attach 3 PIs from PR1 to VPG-1
        ae_ids = {}
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        for pi in range(3):
            vpg_obj.add_physical_interface(pi_objs[pr1_pi_names[pi]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 3)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)

        # Case 2
        # Add 2 more PIs from PR-1 to VPG-1
        vpg_name = vpg_names[0]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        for pi in range(3, 5):
            vpg_obj.add_physical_interface(pi_objs[pr1_pi_names[pi]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 5)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)

        # Case 3
        # Delete PI-2/PR-1 from VPG-1, no changes in AE-ID allocation
        vpg_name = vpg_names[0]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_obj.del_physical_interface(pi_objs[pr1_pi_names[1]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 4)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)

        # Case 4
        # Create VPG-99 along with PI-5/PR1, PI-6/PR1, PI-7/PR1
        # Unsupported as of R2008
        vpg_name = 'vpg_%s_%s' % (test_id, 99)
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        for pi in range(5, 8):
            vpg.add_physical_interface(pi_objs[pr1_pi_names[pi]])
        with ExpectedException(BadRequest):
            self.api.virtual_port_group_create(vpg)

        # verification at Physical Routers
        # No changes expected as VPG-99 should have failed
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)

        # Case 5
        # Create VPG-2 with PI9/PR1, PI10/PR1 and PI1/PR2, PI2/PR2
        vpg_index = 1
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_names[vpg_index]]
        for pi in range(8, 10):
            vpg_obj.add_physical_interface(pr1_pi_objs[pr1_pi_names[pi]])
        for pi in range(2):
            vpg_obj.add_physical_interface(pr2_pi_objs[pr2_pi_names[pi]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 4)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 2)
        ae_id_sorted = process_ae_ids(ae_ids[vpg_name].values())
        self.assertEqual(ae_id_sorted, [0, 0, 1, 1])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 2)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0, 1])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0])

        # Case 6
        # Remove PI9/PR1 from VPG-2
        vpg_index = 1
        vpg_name = vpg_names[1]
        vpg_obj = vpg_objs[vpg_names[vpg_index]]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_obj.del_physical_interface(pr1_pi_objs[pr1_pi_names[8]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 3)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 2)
        self.assertEqual(process_ae_ids(ae_ids[vpg_name].values()), [0, 0, 1])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 2)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0, 1])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0])

        # Case 7
        # remove PI10/PR1, so one ae_id from PR1 will be deallocated
        # no change in PR2
        vpg_index = 1
        vpg_name = vpg_names[vpg_index]
        vpg_obj = vpg_objs[vpg_names[vpg_index]]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_obj.del_physical_interface(pr1_pi_objs[pr1_pi_names[9]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        self.assertEqual(process_ae_ids(ae_ids[vpg_name].values()), [0, 0])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0])

        # Case 8
        # Create VPG-3 with PI11/PR1 and PI3/PR2
        vpg_index = 2
        vpg_name = vpg_names[vpg_index]
        vpg_obj = vpg_objs[vpg_names[vpg_index]]
        for pi in [11]:
            vpg_obj.add_physical_interface(pr1_pi_objs[pr1_pi_names[pi]])
        for pi in [3]:
            vpg_obj.add_physical_interface(pr2_pi_objs[pr2_pi_names[pi]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        ae_id_sorted = process_ae_ids(ae_ids[vpg_name].values())
        self.assertEqual(ae_id_sorted, [1, 1])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 2)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 2)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0, 1])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0, 1])

        # Case 9
        # Create VPG-4 with PI4/PR2 and PI5/PR2 and no change in PR1
        vpg_index = 3
        vpg_name = vpg_names[vpg_index]
        vpg_obj = vpg_objs[vpg_names[vpg_index]]
        for pi in range(4, 6):
            vpg_obj.add_physical_interface(pr2_pi_objs[pr2_pi_names[pi]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        ae_id_sorted = process_ae_ids(ae_ids[vpg_name].values())
        self.assertEqual(ae_id_sorted, [2, 2])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 2)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 3)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0, 1])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0, 1, 2])

        # Case 10
        # Delete VPG-1 (0 is deallocated from PR-1 and PR-2 remains same)
        vpg_index = 0
        vpg_name = vpg_names[vpg_index]
        vpg_obj = vpg_objs[vpg_names[vpg_index]]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        # Now delete VPG-1
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        with ExpectedException(NoIdError):
            self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        for pi_ref in pi_refs:
            pi_obj = self.api.physical_interface_read(id=pi_ref['uuid'])
            self.assertFalse('virtual_port_group_back_refs' in pi_obj.__dict__)

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 3)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [1])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0, 1, 2])

        # Case 11
        # Create VPG-5 with PI6/PR2 and PI7/PR2 and no change in PR1
        vpg_index = 4
        vpg_name = vpg_names[vpg_index]
        vpg_obj = vpg_objs[vpg_names[vpg_index]]
        for pi in range(6, 8):
            vpg_obj.add_physical_interface(pr2_pi_objs[pr2_pi_names[pi]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        ae_id_sorted = process_ae_ids(ae_ids[vpg_name].values())
        self.assertEqual(ae_id_sorted, [3, 3])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 4)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [1])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]),
                         [0, 1, 2, 3])

        # Case 12
        # Create VPG-6 with PI13/PR1 and PI9/PR2 and verify PR1 gets PR1/0,1
        vpg_index = 5
        vpg_name = vpg_names[vpg_index]
        vpg_obj = vpg_objs[vpg_names[vpg_index]]
        for pi in [12]:
            vpg_obj.add_physical_interface(pr1_pi_objs[pr1_pi_names[pi]])
        for pi in [8]:
            vpg_obj.add_physical_interface(pr2_pi_objs[pr2_pi_names[pi]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 2)
        ae_id_sorted = process_ae_ids(ae_ids[vpg_name].values())
        self.assertEqual(ae_id_sorted, [0, 4])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 2)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 5)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0, 1])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]),
                         [0, 1, 2, 3, 4])

        # Case 13
        # Delete PI13/PR1 from VPG-6 verify both PI13/PR1 and
        # PI9/PR2 loses ae-id
        vpg_index = 5
        vpg_name = vpg_names[vpg_index]
        vpg_obj = vpg_objs[vpg_names[vpg_index]]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        for pi in [12]:
            vpg_obj.del_physical_interface(pr1_pi_objs[pr1_pi_names[pi]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 1)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        self.assertIsNone(list(set(ae_ids[vpg_name].values()))[0])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 4)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [1])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]),
                         [0, 1, 2, 3])

        # TO-Do cleanup

    def test_leftover_single_pi_allocation(self):
        """Leftover single PI from same PR is allocated."""
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()

        def process_ae_ids(x):
            return [int(i) for i in sorted(x)]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 3
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pi_objs.update(pr1_pi_objs)

        # create one VPG
        vpg_count = 1
        vpg_names = ['vpg_%s_%s' % (test_id, i) for
                     i in range(1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        # attach only PI1/PR1 to VPG-1
        # no AE-ID to be allocated
        ae_ids = {}
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj.add_physical_interface(pi_objs[pr1_pi_names[0]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)

        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 1)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        self.assertIsNone(list(ae_ids[vpg_name].values())[0])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [])

        # Attach PI1/PR1 and PI2/PR1 to VPG-1
        ae_ids = {}
        vpg_name = vpg_names[0]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        for pi in range(1, 3):
            vpg_obj.add_physical_interface(pi_objs[pr1_pi_names[pi]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)

        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 3)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])

    def test_leftover_single_pi_deallocation(self):
        """Leftover single PI from same PR is deallocated."""
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()

        def process_ae_ids(x):
            return [int(i) for i in sorted(x)]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 2
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pi_objs.update(pr1_pi_objs)

        # create one VPG
        vpg_count = 1
        vpg_names = ['vpg_%s_%s' % (test_id, i) for
                     i in range(1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        # Attach PI1/PR1 and PI2/PR1 to VPG-1
        ae_ids = {}
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        for pi in range(2):
            vpg_obj.add_physical_interface(pi_objs[pr1_pi_names[pi]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)

        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])

        # Delete PI1/PR1
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_obj.del_physical_interface(pr1_pi_objs[pr1_pi_names[0]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)

        # verify PI-refs are correct
        pi_refs = vpg_obj.get_physical_interface_refs()
        self.assertEqual(len(pi_refs), 1)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [])

    def test_delete_vpg_with_two_prs(self):
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()

        def process_ae_ids(x):
            return [int(i) for i in sorted(x)]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 2
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr2_pi_names = ['%s_pr2_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr2_pi_objs = self._create_pi_objects(pr_objs[1], pr2_pi_names)
        pi_objs.update(pr1_pi_objs)
        pi_objs.update(pr2_pi_objs)

        # create two VPGs
        vpg_count = 2
        vpg_names = ['vpg_%s_%s' % (test_id, i) for
                     i in range(1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        # Attach PI1/PR1, PI1/PR2, PI2/PR1, PI2/PR2 to VPG-1
        ae_ids = {}
        vpg_index = 0
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_names[vpg_index]]
        for pi in range(2):
            vpg_obj.add_physical_interface(pr1_pi_objs[pr1_pi_names[pi]])
        for pi in range(2):
            vpg_obj.add_physical_interface(pr2_pi_objs[pr2_pi_names[pi]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 4)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        ae_id_sorted = process_ae_ids(ae_ids[vpg_name].values())
        self.assertEqual(ae_id_sorted, [0, 0, 0, 0])
        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0])

        # Delete VPG-1
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        with ExpectedException(NoIdError):
            self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        for pi_ref in pi_refs:
            pi_obj = self.api.physical_interface_read(id=pi_ref['uuid'])
            self.assertFalse('virtual_port_group_back_refs' in pi_obj.__dict__)

        # Verify no AE-ID at ZK
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 0)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])

        # Attach PI1/PR1, PI1/PR2, PI2/PR1, PI2/PR2 to VPG-2
        vpg_index = 1
        vpg_name = vpg_names[1]
        vpg_obj = vpg_objs[vpg_names[vpg_index]]
        for pi in range(2):
            vpg_obj.add_physical_interface(pr1_pi_objs[pr1_pi_names[pi]])
        for pi in range(2):
            vpg_obj.add_physical_interface(pr2_pi_objs[pr2_pi_names[pi]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 4)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        ae_id_sorted = process_ae_ids(ae_ids[vpg_name].values())
        self.assertEqual(ae_id_sorted, [0, 0, 0, 0])

        # verification at ZK
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0])

    def test_adding_pi_refs_while_vpg_creation(self):
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()

        def process_ae_ids(x):
            return [int(i) for i in sorted(x)]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 1
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr2_pi_names = ['%s_pr2_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr2_pi_objs = self._create_pi_objects(pr_objs[1], pr2_pi_names)
        pi_objs.update(pr1_pi_objs)
        pi_objs.update(pr2_pi_objs)

        # Create local VPG-99
        vpg_name = 'vpg_%s_%s' % (test_id, 99)
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        for pi in range(1):
            vpg.add_physical_interface(pr1_pi_objs[pr1_pi_names[pi]])
        for pi in range(1):
            vpg.add_physical_interface(pr2_pi_objs[pr2_pi_names[pi]])

        # Add actual VPG to API server. This should fail
        with ExpectedException(BadRequest):
            self.api.virtual_port_group_create(vpg)

    def test_exhaust_ae_ids(self):
        """
        Raise Exhaustion Exception when more than MAX-AE-ID VPGs are attached.

        MAX-AE-ID == 128
        """
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id().split('.')[-1]

        def process_ae_ids(x):
            return [int(i) for i in sorted(x)]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        # create PR1, PR2 and 129 PIs on each PR
        pi_objs = {}
        pi_per_pr = 135
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr2_pi_names = ['%s_pr2_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr2_pi_objs = self._create_pi_objects(pr_objs[1], pr2_pi_names)
        pi_objs.update(pr1_pi_objs)
        pi_objs.update(pr2_pi_objs)

        # create VPGs
        vpg_count = 135
        vpg_names = ['vpg_%s_%s' % (test_id, i)
                     for i in range(1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        def update_vpg(vpg_obj, pis, vpg_count):
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            for pi in pis:
                vpg_obj.add_physical_interface(pi)
            self.api.virtual_port_group_update(vpg_obj)
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            pi_refs = vpg_obj.get_physical_interface_refs()
            self.assertEqual(len(pi_refs), 2)
            vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                          for ref in pi_refs}
            self.assertEqual(len(set(vpg_ae_ids.values())), 1)
            pr_ae_ids = get_zk_ae_ids()
            self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), vpg_count)
            self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), vpg_count)
            self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]),
                             list(range(vpg_count)))
            self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]),
                             list(range(vpg_count)))

        # Case 1: Update 128 VPGs with One PI from both PRs
        # This is expected to PASS
        for vpg_count in range(128):
            vpg_pis = [pr1_pi_objs[pr1_pi_names[vpg_count]],
                       pr2_pi_objs[pr2_pi_names[vpg_count]]]
            update_vpg(vpg_objs[vpg_names[vpg_count]], vpg_pis, vpg_count + 1)

        # Case 2: Try to update 129th VPG with a PI from both PRS
        # This is expected to FAIL as max AE-ID allowed is 128
        index = 129
        vpg_name = vpg_names[index]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_pis = [pr1_pi_objs[pr1_pi_names[index]],
                   pr2_pi_objs[pr2_pi_names[index]]]
        update_vpg(vpg_obj, vpg_pis, index)

        index = 130
        vpg_name = vpg_names[index]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_pis = [pr1_pi_objs[pr1_pi_names[index]],
                   pr2_pi_objs[pr2_pi_names[index]]]
        with ExpectedException(BadRequest):
            update_vpg(vpg_obj, vpg_pis, index)

    def _multiple_vpg_with_multiple_pi(
            self, proj_obj, fabric_obj, pr_objs, validation):
        """Test Steps.

        Add all test steps
        """
        test_id = self.id().split('.')[-1]
        fabric_name = fabric_obj.get_fq_name()
        vlan_vn_count = 10

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        vlan_ids = range(1, vlan_vn_count + 1)
        vn_names = ['vn_%s_%s' % (test_id, i)
                    for i in range(1, vlan_vn_count + 1)]
        vn_objs = self._create_vns(proj_obj, vn_names)

        # create four PIs, two on each PR
        pi_objs = {}
        pi_per_pr = 4
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr2_pi_names = ['%s_pr2_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr2_pi_objs = self._create_pi_objects(pr_objs[1], pr2_pi_names)
        pi_objs.update(pr1_pi_objs)
        pi_objs.update(pr2_pi_objs)

        # create three VPGs
        vpg_count = 10
        vpg_names = ['vpg_%s_%s' % (test_id, i) for
                     i in range(1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        # create 10 VMIs, 4 PIs (2 from each PR) in a VPG
        vmi_infos = []
        vpg1_pis = [pi.get_fq_name() for pi in
                    [pr1_pi_objs[pr1_pi_names[0]],
                     pr1_pi_objs[pr1_pi_names[1]],
                     pr2_pi_objs[pr2_pi_names[0]],
                     pr2_pi_objs[pr2_pi_names[1]]]]
        vpg2_pis = [pi.get_fq_name() for pi in
                    [pr1_pi_objs[pr1_pi_names[2]],
                     pr1_pi_objs[pr1_pi_names[3]],
                     pr2_pi_objs[pr2_pi_names[2]],
                     pr2_pi_objs[pr2_pi_names[3]]]]
        vpg1_vmi_names = ['vmi_vpg1_%s_%s' % (test_id, vmi_id) for vmi_id in
                          range(1, vlan_vn_count + 1)]
        vpg2_vmi_names = ['vmi_vpg2_%s_%s' % (test_id, vmi_id) for vmi_id in
                          range(1, vlan_vn_count + 1)]
        for vmi_id in range(1, vlan_vn_count + 1):
            info = {
                'name': vpg1_vmi_names[vmi_id - 1],
                'vmi_id': vmi_id,
                'parent_obj': proj_obj,
                'vn': vn_objs[vn_names[vmi_id - 1]],
                'vpg': vpg_objs[vpg_names[0]].uuid,
                'fabric': fabric_name,
                'pis': vpg1_pis,
                'vlan': vlan_ids[vmi_id - 1],
                'is_untagged': False}
            vmi_infos.append(info)
            info = {
                'name': vpg2_vmi_names[vmi_id - 1],
                'vmi_id': vmi_id,
                'parent_obj': proj_obj,
                'vn': vn_objs[vn_names[vmi_id - 1]],
                'vpg': vpg_objs[vpg_names[1]].uuid,
                'fabric': fabric_name,
                'pis': vpg2_pis,
                'vlan': vlan_ids[vmi_id - 1],
                'is_untagged': False}
            vmi_infos.append(info)
        vmi_objs = self._create_vmis(vmi_infos)
        for vpg_name, vpg_obj in vpg_objs.items():
            vpg_objs[vpg_name] = self.api.virtual_port_group_read(
                id=vpg_obj.uuid)

        # record AE-IDs allocated for each prouter
        ae_ids = {}
        for vpg in range(2):
            vpg_name = vpg_names[vpg]
            pi_refs = vpg_objs[vpg_name].get_physical_interface_refs()
            ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                                for ref in pi_refs}
            # verify all AE-IDs allocated per prouter are unique
            self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
            self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 2)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 2)

        # Case #1
        # Remove PI-1 from PR-1 through VMI-1 of VPG-1 update
        vmi_id = 1
        vmi_name = 'vmi_vpg1_%s_%s' % (test_id, vmi_id)
        vpg_name = 'vpg_%s_%s' % (test_id, vmi_id)
        vmi_infos = [
            {'name': vmi_name,
             'vmi_uuid': vmi_objs[vmi_name].uuid,
             'vpg': vpg_objs[vpg_name].uuid,
             'fabric': fabric_name,
             'pis': vpg1_pis[1:],
             'vlan': vlan_ids[vmi_id - 1],
             'is_untagged': False}]
        self._update_vmis(vmi_infos)

        # re-read VPGs
        for vpg, vpg_o in vpg_objs.items():
            vpg_objs[vpg] = self.api.virtual_port_group_read(
                id=vpg_o.uuid)

        # Verifications at VPG-1
        # check PI-1 is removed from VPG-1
        pi_refs = vpg_objs[vpg_name].get_physical_interface_refs()
        vpg1_ae_ids = [pi_ref['attr'].ae_num for pi_ref in pi_refs]
        self.assertEqual(len(pi_refs), 3)
        # verify AE-ID associated with VPG-1
        # AE-IDs of remaining PIs are unaffected
        self.assertEqual(len(set(vpg1_ae_ids)), 1)

        # Verifications at VPG-2
        pi_refs = vpg_objs[vpg_names[1]].get_physical_interface_refs()
        vpg2_ae_ids = [pi_ref['attr'].ae_num for pi_ref in pi_refs]
        self.assertEqual(len(pi_refs), 4)
        # verify AE-ID associated with VPG-2
        # AE-IDs of remaining PIs are unaffected
        self.assertEqual(len(set(vpg2_ae_ids)), 1)

        # verification at Physical Routers
        # since only PI-1 was removed, AE-ID allocation remains same
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 2)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 2)

        # Case #2
        # Remove all PIs but PI-1 in PR-1/VPG-1 through VMI-1 of VPG-1 update
        vmi_id = 1
        vmi_name = 'vmi_vpg1_%s_%s' % (test_id, vmi_id)
        vpg_name = 'vpg_%s_%s' % (test_id, vmi_id)
        vmi_infos = [
            {'name': vmi_name,
             'vmi_uuid': vmi_objs[vmi_name].uuid,
             'vpg': vpg_objs[vpg_name].uuid,
             'fabric': fabric_name,
             'pis': vpg1_pis[0],
             'vlan': vlan_ids[vmi_id - 1],
             'is_untagged': False}]
        self._update_vmis(vmi_infos)

        # re-read VPGs
        for vpg, vpg_o in vpg_objs.items():
            vpg_objs[vpg] = self.api.virtual_port_group_read(
                id=vpg_o.uuid)

        # Verifications at VPG-1
        # check PI-1 is removed from VPG-1
        pi_refs = vpg_objs[vpg_names[0]].get_physical_interface_refs()
        vpg1_ae_ids = [pi_ref['attr'].ae_num for pi_ref in pi_refs]
        self.assertEqual(len(pi_refs), 1)
        # verify AE-ID associated with VPG-1
        # AE-IDs of remaining PIs are unaffected
        self.assertEqual(len(set(vpg1_ae_ids)), 1)
        self.assertIsNone(vpg1_ae_ids[0])

        # Verifications at VPG-2
        pi_refs = vpg_objs[vpg_names[1]].get_physical_interface_refs()
        vpg2_ae_ids = [pi_ref['attr'].ae_num for pi_ref in pi_refs]
        self.assertEqual(len(pi_refs), 4)
        # verify AE-ID associated with VPG-2
        # AE-IDs of remaining PIs are unaffected
        self.assertEqual(len(set(vpg2_ae_ids)), 1)

        # verify at ZK Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 1)

        # Case 3
        # Create a new VPG with two PIs, one from each PRs
        case3_id = 99
        vpg3_uuid = vpg_objs[vpg_names[2]].uuid
        case3_vn_name = 'vn_case3_%s_%s' % (test_id, case3_id)
        case3_vn_objs = self._create_vns(proj_obj, [case3_vn_name])
        vn_objs.update(case3_vn_objs)
        pi_per_pr = 1
        case3_pr1_pi_name = '%s_case3_pr1_pi%d' % (test_id, case3_id)
        case3_pr2_pi_name = '%s_case3_pr2_pi%d' % (test_id, case3_id)
        case3_pr1_pi_objs = self._create_pi_objects(
            pr_objs[0], [case3_pr1_pi_name])
        case3_pr2_pi_objs = self._create_pi_objects(
            pr_objs[1], [case3_pr2_pi_name])
        vpg3_pis = [pi.get_fq_name() for pi in
                    [case3_pr1_pi_objs[case3_pr1_pi_name],
                     case3_pr2_pi_objs[case3_pr2_pi_name]]]
        pi_objs.update(case3_pr1_pi_objs)
        pi_objs.update(case3_pr2_pi_objs)
        vmi_info = {
            'name': 'vmi_vpg3_%s_%s' % (test_id, 99),
            'vmi_id': 99,
            'parent_obj': proj_obj,
            'vn': case3_vn_objs[case3_vn_name],
            'vpg': vpg3_uuid,
            'fabric': fabric_name,
            'pis': vpg3_pis,
            'vlan': case3_id,
            'is_untagged': False}
        case3_vmi_obj = self._create_vmis([vmi_info])
        vmi_objs.update(case3_vmi_obj)

        # re-read VPG-3
        vpg_objs[vpg_names[2]] = self.api.virtual_port_group_read(id=vpg3_uuid)
        # Verifications at VPG-3
        pi_refs = vpg_objs[vpg_names[2]].get_physical_interface_refs()
        vpg3_ae_ids = [pi_ref['attr'].ae_num for pi_ref in pi_refs]
        self.assertEqual(len(pi_refs), 2)
        # verify an AE-ID is allocated
        self.assertEqual(len(set(vpg3_ae_ids)), 1)

        # verify at ZK Physical Routers
        # Since a new VPG is added with PIs at Case-3
        # only two AE-IDs should remain in each prouter
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 2)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 2)
        # TO-DO
        # Verify AE-ID is re-allocated instead of new one

        # Case 4
        # Add PI1/PR1, PI2/PR1 to VPG-1, so a new AE-ID is allocated
        vmi_id = 9
        vmi_name = 'vmi_vpg1_%s_%s' % (test_id, vmi_id)
        vpg_name = 'vpg_%s_%s' % (test_id, 1)
        vmi_infos = [
            {'name': vmi_name,
             'vmi_uuid': vmi_objs[vmi_name].uuid,
             'vpg': vpg_objs[vpg_name].uuid,
             'fabric': fabric_name,
             'pis': vpg1_pis[0:2],
             'vlan': vlan_ids[vmi_id - 1],
             'is_untagged': False}]
        self._update_vmis(vmi_infos)

        # re-read VPGs
        for vpg_name, vpg_obj in vpg_objs.items():
            vpg_objs[vpg_name] = self.api.virtual_port_group_read(
                id=vpg_obj.uuid)

        # Verifications at VPG-1
        # check PI1/PR1 and PI2/PR1 are added to VPG-1
        pi_refs = vpg_objs[vpg_names[0]].get_physical_interface_refs()
        vpg1_ae_ids = [pi_ref['attr'].ae_num for pi_ref in pi_refs]
        self.assertEqual(len(pi_refs), 2)
        # verify AE-ID associated with VPG-1
        # A new AE-ID is allocated
        self.assertEqual(len(set(vpg1_ae_ids)), 1)

        # Verifications at VPG-2
        pi_refs = vpg_objs[vpg_names[1]].get_physical_interface_refs()
        vpg2_ae_ids = [pi_ref['attr'].ae_num for pi_ref in pi_refs]
        self.assertEqual(len(pi_refs), 4)
        # verify AE-ID associated with VPG-2
        # AE-IDs of remaining PIs are unaffected
        self.assertEqual(len(set(vpg2_ae_ids)), 1)

        # verify at ZK Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 3)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 2)
        # TO-DO
        # Verify AE-ID is re-allocated instead of new one

        # Case X1
        # Create a new VPG with two PIs, both belonging to the same PR
        caseX1_id = 101
        vpgX1_uuid = vpg_objs[vpg_names[3]].uuid
        caseX1_vn_names = ['vn_caseX1_%s_%s' % (test_id, caseX1_id)]
        caseX1_vn_objs = self._create_vns(proj_obj, caseX1_vn_names)
        vn_objs.update(caseX1_vn_objs)
        pi_per_pr = 2
        caseX1_pr1_pi_names = ['%s_caseX1_pr1_pi%d' % (test_id, caseX1_id),
                               '%s_caseX1_pr1_pi%d' % (test_id, caseX1_id + 1)]
        caseX1_pr1_pi_objs = self._create_pi_objects(
            pr_objs[0], caseX1_pr1_pi_names)
        vpgX1_pis = [pi.get_fq_name() for pi in
                     [caseX1_pr1_pi_objs[caseX1_pr1_pi_names[0]],
                     caseX1_pr1_pi_objs[caseX1_pr1_pi_names[1]]]]
        pi_objs.update(caseX1_pr1_pi_objs)
        vmi_info = {
            'name': 'vmi_vpg4_%s_%s' % (test_id, caseX1_id),
            'vmi_id': caseX1_id,
            'parent_obj': proj_obj,
            'vn': caseX1_vn_objs[caseX1_vn_names[0]],
            'vpg': vpgX1_uuid,
            'fabric': fabric_name,
            'pis': vpgX1_pis,
            'vlan': caseX1_id,
            'is_untagged': False}
        caseX1_vmi_obj = self._create_vmis([vmi_info])
        vmi_objs.update(caseX1_vmi_obj)
        # re-read VPG-3
        vpg_objs[vpg_names[3]] = self.api.virtual_port_group_read(
            id=vpgX1_uuid)
        # Verifications at VPG-3
        pi_refs = vpg_objs[vpg_names[3]].get_physical_interface_refs()
        vpgX1_ae_ids = [pi_ref['attr'].ae_num for pi_ref in pi_refs]
        self.assertEqual(len(pi_refs), 2)
        # verify an AE-ID is allocated
        self.assertEqual(len(set(vpgX1_ae_ids)), 1)

        # verify at ZK Physical Routers
        # Since a new VPG is added with PIs at Case-3
        # only two AE-IDs should remain in each prouter
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 4)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 2)
        # TO-DO
        # Verify AE-ID is re-allocated instead of new one

        # Case X2
        # Create a new VPG with two PIs,both belonging to the same PR
        caseX2_id = 103
        vpgX2_uuid = vpg_objs[vpg_names[4]].uuid
        caseX2_vn_names = ['vn_caseX2_%s_%s' % (test_id, caseX2_id)]
        caseX2_vn_objs = self._create_vns(proj_obj, caseX2_vn_names)
        vn_objs.update(caseX2_vn_objs)
        pi_per_pr = 2
        caseX2_pr1_pi_names = ['%s_caseX2_pr1_pi%d' % (test_id, caseX2_id),
                               '%s_caseX2_pr1_pi%d' % (test_id, caseX2_id + 1)]
        caseX2_pr1_pi_objs = self._create_pi_objects(
            pr_objs[0], caseX2_pr1_pi_names)
        vpgX2_pis = [pi.get_fq_name() for pi in
                     [caseX2_pr1_pi_objs[caseX2_pr1_pi_names[0]],
                     caseX2_pr1_pi_objs[caseX2_pr1_pi_names[1]]]]
        pi_objs.update(caseX2_pr1_pi_objs)
        vmi_info = {
            'name': 'vmi_vpg5_%s_%s' % (test_id, caseX2_id),
            'vmi_id': caseX2_id,
            'parent_obj': proj_obj,
            'vn': caseX2_vn_objs[caseX2_vn_names[0]],
            'vpg': vpgX2_uuid,
            'fabric': fabric_name,
            'pis': vpgX2_pis,
            'vlan': caseX2_id,
            'is_untagged': False}
        caseX2_vmi_obj = self._create_vmis([vmi_info])
        vmi_objs.update(caseX2_vmi_obj)
        # re-read VPG-3
        vpg_objs[vpg_names[4]] = self.api.virtual_port_group_read(
            id=vpgX2_uuid)
        # Verifications at VPG-3
        pi_refs = vpg_objs[vpg_names[4]].get_physical_interface_refs()
        self.assertEqual(len(pi_refs), 2)
        # verify an AE-ID is allocated
        self.assertEqual(len(set(vpgX1_ae_ids)), 1)

        # verify at ZK Physical Routers
        # Since a new VPG is added with PIs at Case-3
        # only two AE-IDs should remain in each prouter
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 5)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 2)
        # TO-DO
        # Verify AE-ID is re-allocated instead of new one

        # Case X3
        # Add PI2/PR1 to VPG-1, so a new AE-ID is allocated
        vmi_id = 10
        vmi_name = 'vmi_vpg1_%s_%s' % (test_id, vmi_id)
        vpg_name = 'vpg_%s_%s' % (test_id, 1)
        vpg1_uuid = vpg_objs[vpg_names[0]].uuid
        vmi_infos = [
            {'name': vmi_name,
             'vmi_uuid': vmi_objs[vmi_name].uuid,
             'vpg': vpg_objs[vpg_name].uuid,
             'fabric': fabric_name,
             'pis': vpg1_pis[1],
             'vlan': vlan_ids[vmi_id - 1],
             'is_untagged': False}]
        self._update_vmis(vmi_infos)

        # re-read VPG1
        vpg_objs[vpg_names[0]] = self.api.virtual_port_group_read(id=vpg1_uuid)

        # Verifications at VPG-1
        # check PI1/PR1 and PI2/PR1 are added to VPG-1
        pi_refs = vpg_objs[vpg_names[0]].get_physical_interface_refs()
        vpg1_ae_ids = [pi_ref['attr'].ae_num for pi_ref in pi_refs]
        self.assertEqual(len(pi_refs), 1)
        # verify AE-ID associated with VPG-1
        # A new AE-ID is allocated
        self.assertEqual(len(set(vpg1_ae_ids)), 1)

        # Verifications at VPG-2
        # pi_refs = vpg_objs[vpg_names[1]].get_physical_interface_refs()
        # vpg2_ae_ids = [pi_ref['attr'].ae_num for pi_ref in pi_refs]
        # self.assertEqual(len(pi_refs), 4)
        # verify AE-ID associated with VPG-2
        # AE-IDs of remaining PIs are unaffected
        # self.assertEqual(len(set(vpg2_ae_ids)), 1)

        # verify at ZK Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 4)
        # self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 2)
        # TO-DO
        # Verify AE-ID is re-allocated instead of new one

        # Case X4
        # Create a new VPG with two PIs, both belonging to the same PR
        caseX4_id = 66
        vpgX4_uuid = vpg_objs[vpg_names[5]].uuid
        caseX4_vn_names = ['vn_caseX4_%s_%s' % (test_id, caseX4_id)]
        caseX4_vn_objs = self._create_vns(proj_obj, caseX4_vn_names)
        vn_objs.update(caseX4_vn_objs)
        pi_per_pr = 2
        caseX4_pr1_pi_names = ['%s_caseX4_pr1_pi%d' % (test_id, caseX4_id),
                               '%s_caseX4_pr1_pi%d' % (test_id, caseX4_id + 1)]
        caseX4_pr1_pi_objs = self._create_pi_objects(
            pr_objs[0], caseX4_pr1_pi_names)
        vpgX4_pis = [pi.get_fq_name() for pi in
                     [caseX4_pr1_pi_objs[caseX4_pr1_pi_names[0]],
                     caseX4_pr1_pi_objs[caseX4_pr1_pi_names[1]]]]
        pi_objs.update(caseX4_pr1_pi_objs)
        vmi_info = {
            'name': 'vmi_vpg6_%s_%s' % (test_id, caseX4_id),
            'vmi_id': caseX4_id,
            'parent_obj': proj_obj,
            'vn': caseX4_vn_objs[caseX4_vn_names[0]],
            'vpg': vpgX4_uuid,
            'fabric': fabric_name,
            'pis': vpgX4_pis,
            'vlan': caseX4_id,
            'is_untagged': False}
        caseX4_vmi_obj = self._create_vmis([vmi_info])
        vmi_objs.update(caseX4_vmi_obj)
        # re-read VPG-5
        vpg_objs[vpg_names[5]] = self.api.virtual_port_group_read(
            id=vpgX4_uuid)
        # Verifications at VPG-5
        pi_refs = vpg_objs[vpg_names[5]].get_physical_interface_refs()
        vpgX4_ae_ids = [pi_ref['attr'].ae_num for pi_ref in pi_refs]
        self.assertEqual(len(pi_refs), 2)
        # verify an AE-ID is allocated
        self.assertEqual(len(set(vpgX4_ae_ids)), 1)

        # verify at ZK Physical Routers
        # Since a new VPG is added with PIs at Case-3
        # only two AE-IDs should remain in each prouter
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 5)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 2)
        # TO-DO
        # Verify AE-ID is re-allocated instead of new one

        # Case 5
        vpg_objs[vpg_names[0]] = self.api.virtual_port_group_read(
            id=vpg_objs[vpg_names[0]].uuid)
        curr_pis = [ref['to'] for ref in
                    vpg_objs[vpg_names[0]].get_physical_interface_refs()]
        case5_id = 100
        vpg_id = vpg_objs[vpg_names[0]].uuid
        case5_vn_name = 'vn_case5_%s_%s' % (test_id, case5_id)
        case5_vn_objs = self._create_vns(proj_obj, [case5_vn_name])
        vn_objs.update(case5_vn_objs)
        pi_per_pr = 1
        case5_pr2_pi_name = '%s_case5_pr2_pi%d' % (test_id, case5_id)
        case5_pr2_pi_objs = self._create_pi_objects(
            pr_objs[1], [case5_pr2_pi_name])
        vpg1_case5_pis = [pi.get_fq_name() for pi in
                          [case5_pr2_pi_objs[case5_pr2_pi_name]]]
        vpg1_case5_pis += curr_pis
        pi_objs.update(case5_pr2_pi_objs)
        vmi_info = {
            'name': 'vmi_vpg1_%s_%s' % (test_id, case5_id),
            'vmi_id': case5_id,
            'parent_obj': proj_obj,
            'vn': case5_vn_objs[case5_vn_name],
            'vpg': vpg_id,
            'fabric': fabric_name,
            'pis': vpg1_case5_pis,
            'vlan': case5_id,
            'is_untagged': False}
        case5_vmi_obj = self._create_vmis([vmi_info])
        vmi_objs.update(case5_vmi_obj)
        # re-read VPGs
        for vpg_name, vpg_obj in vpg_objs.items():
            vpg_objs[vpg_name] = self.api.virtual_port_group_read(
                id=vpg_obj.uuid)

        # Verifications at VPG-1
        # check PI-1 is removed from VPG-1
        pi_refs = vpg_objs[vpg_names[0]].get_physical_interface_refs()
        vpg1_ae_ids = [pi_ref['attr'].ae_num for pi_ref in pi_refs]
        self.assertEqual(len(pi_refs), 2)
        # verify AE-ID associated with VPG-1
        # AE-IDs of remaining PIs are unaffected
        self.assertEqual(len(set(vpg1_ae_ids)), 2)

        # verify at ZK Physical Routers
        # Since a new PI added to existing two PIs to VPG-1
        # VPG-1 will have three PIs, but expects same AE-ID be
        # reallocated
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 6)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 3)
        # TO-DO
        # Verify AE-ID is re-allocated instead of new one

        # cleanup
        for _, vmi_obj in vmi_objs.items():
            self.api.virtual_machine_interface_delete(id=vmi_obj.uuid)
        for _, vpg_obj in vpg_objs.items():
            self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        for _, pi_obj in pi_objs.items():
            self.api.physical_interface_delete(id=pi_obj.uuid)
        for _, vn_obj in vn_objs.items():
            self.api.virtual_network_delete(id=vn_obj.uuid)
        self.api.physical_router_delete(id=pr_objs[0].uuid)
        self.api.physical_router_delete(id=pr_objs[1].uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)
        self.api.project_delete(id=proj_obj.uuid)

    def _multiple_vpg_with_multiple_pi_old(
            self, proj_obj, fabric_obj, pr_obj, validation):
        pi_count = 300
        pi_per_vmi = 3
        fabric_name = fabric_obj.get_fq_name()
        test_id = self.id()
        vlan_vn_count = int(pi_count / 3) + 1

        vlan_ids = range(1, vlan_vn_count)
        vn_names = ['vn_%s_%s' % (test_id, i)
                    for i in range(1, vlan_vn_count)]
        vn_objs = self._create_vns(proj_obj, vn_names)

        pi_names = ['phy_intf_%s_%s' % (test_id, i) for i in range(
                    1, pi_count + 1)]
        pi_objs = self._create_pi_objects(pr_obj, pi_names)
        vpg_names = ['vpg_%s_%s' % (test_id, i) for i in range(
                     1, vlan_vn_count)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        vmi_infos = []
        for vmi_id in range(1, vlan_vn_count):
            pinames = pi_names[(vmi_id - 1) * pi_per_vmi: vmi_id * pi_per_vmi]
            pis = [pi_objs[pi].get_fq_name() for pi in pinames]
            info = {
                'name': 'vmi_%s_%s' % (test_id, vmi_id),
                'vmi_id': vmi_id,
                'parent_obj': proj_obj,
                'vn': vn_objs[vn_names[vmi_id - 1]],
                'vpg': vpg_objs[vpg_names[vmi_id - 1]].uuid,
                'fabric': fabric_name,
                'pis': pis,
                'vlan': vlan_ids[vmi_id - 1],
                'is_untagged': False}
            vmi_infos.append(info)

        vmi_objs = self._create_vmis(vmi_infos)
        all_ae_nums = []
        for vpg_name in vpg_names:
            vpg_uuid = vpg_objs[vpg_name].uuid
            vpg_obj = self.api.virtual_port_group_read(id=vpg_uuid)
            ae_nums = [
                vpg_obj.physical_interface_refs[i]['attr'].ae_num
                for i in range(0, pi_per_vmi)]
            # check AE-ID present in each PI ref
            self.assertEqual(
                len(ae_nums), len(vpg_obj.physical_interface_refs))
            # check AE-ID present in each PI is same
            self.assertEqual(len(set(ae_nums)), 1)
            all_ae_nums += list(set(ae_nums))
        # check unique AE-ID are allocted for each PI ref
        self.assertEqual(len(all_ae_nums), int(pi_count / pi_per_vmi))

        # replace with new pis on first VMI
        extra_pi_start = pi_count + 1
        extra_pi_end = extra_pi_start + 4
        extra_pi_names = ['phy_intf_%s_%s' % (test_id, i)
                          for i in range(extra_pi_start, extra_pi_end)]
        extra_pi_objs = self._create_pi_objects(pr_obj, extra_pi_names)
        extra_pi_fq_names = [extra_pi_objs[pi].get_fq_name()
                             for pi, obj in extra_pi_objs.items()]

        vmi_obj_1 = vmi_objs[vmi_objs.keys()[0]]
        vpg_obj_1 = vpg_objs[vpg_objs.keys()[0]]
        vpg_obj_1 = self.api.virtual_port_group_read(
            id=vpg_obj_1.uuid)
        ae_nums_org = [
            vpg_obj_1.physical_interface_refs[i]['attr'].ae_num
            for i in range(0, pi_per_vmi)]
        kv_pairs = self._create_kv_pairs(
            extra_pi_fq_names, fabric_name,
            vpg_objs[vpg_objs.keys()[0]].get_fq_name())
        vmi_obj_1.set_virtual_machine_interface_bindings(kv_pairs)
        self.api.virtual_machine_interface_update(vmi_obj_1)
        vpg_obj_1 = self.api.virtual_port_group_read(
            id=vpg_objs[vpg_objs.keys()[0]].uuid)
        extra_ae_nums = [
            vpg_obj_1.physical_interface_refs[i]['attr'].ae_num
            for i in range(0, extra_pi_end - extra_pi_start)]

        # check all AE-IDs are same
        self.assertEqual(len(set(extra_ae_nums)), 1)

        # ensure AE_ID is same as before
        self.assertEqual(
            set(ae_nums_org),
            set(extra_ae_nums),
            'AE numbers before (%s) and after (%s) '
            'replacing PI are not same' % (
                ae_nums_org, extra_ae_nums))

        # add a three more pis to first VMI
        extra_2_pi_start = extra_pi_end + 1
        extra_2_pi_end = extra_2_pi_start + 4
        extra_2_pi_names = ['phy_intf_%s_%s' % (test_id, i)
                            for i in range(extra_2_pi_start, extra_2_pi_end)]
        extra_2_pi_objs = self._create_pi_objects(pr_obj, extra_2_pi_names)
        extra_2_pi_fq_names = [extra_2_pi_objs[pi].get_fq_name()
                               for pi, obj in extra_2_pi_objs.items()]
        vmi_obj_1 = vmi_objs[vmi_objs.keys()[0]]
        kv_pairs = self._create_kv_pairs(
            extra_pi_fq_names + extra_2_pi_fq_names, fabric_name,
            vpg_objs[vpg_objs.keys()[0]].get_fq_name())
        vmi_obj_1.set_virtual_machine_interface_bindings(kv_pairs)
        self.api.virtual_machine_interface_update(vmi_obj_1)
        vpg_obj_1 = self.api.virtual_port_group_read(
            id=vpg_objs[vpg_objs.keys()[0]].uuid)
        curr_pi_obj_count = (
            len(extra_pi_fq_names) + len(extra_2_pi_fq_names))
        ae_nums_new = [
            vpg_obj_1.physical_interface_refs[i]['attr'].ae_num
            for i in range(0, curr_pi_obj_count)]

        # check AE-ID allocated for all PIs
        self.assertEqual(len(ae_nums_new), curr_pi_obj_count)
        # check AE-ID allocated for all PIs are same
        self.assertEqual(len(set(ae_nums_new)), 1)

        # ensure AE_ID is same as before
        self.assertEqual(
            set(ae_nums_org),
            set(ae_nums_new),
            'AE numbers before (%s) and after (%s) '
            'replacing PI are not same' % (
                ae_nums_org, ae_nums_new))

        # cleanup
        for vmi_name, vmi_obj in vmi_objs.items():
            self.api.virtual_machine_interface_delete(id=vmi_obj.uuid)
        for vpg_name, vpg_obj in vpg_objs.items():
            self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        total_pis = {}
        total_pis.update(pi_objs)
        total_pis.update(extra_pi_objs)
        total_pis.update(extra_2_pi_objs)
        for pi_name, pi_obj in total_pis.items():
            self.api.physical_interface_delete(id=pi_obj.uuid)
        for vn_name, vn_obj in vn_objs.items():
            self.api.virtual_network_delete(id=vn_obj.uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)
        self.api.project_delete(id=proj_obj.uuid)

    def test_multiple_vpg_with_multiple_pi(self):
        """Verify adding a PI to VMI in enterprise style."""
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        self._multiple_vpg_with_multiple_pi(
            proj_obj, fabric_obj, pr_objs, 'enterprise')

        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        self._multiple_vpg_with_multiple_pi(
            proj_obj, fabric_obj, pr_objs, 'serviceprovider')

    def _test_add_new_pi_to_vmi(
            self, proj_obj, fabric_obj, pr_obj, validation):
        vlan_1 = 42
        vlan_2 = '4094'
        fabric_name = fabric_obj.get_fq_name()
        test_id = self.id()

        pi_names = ['%s_phy_intf_%s' % (test_id, i) for i in range(1, 5)]
        pi_objs = self._create_pi_objects(pr_obj, pi_names)
        pi1_fq_name, pi2_fq_name, pi3_fq_name, pi4_fq_name = [
            pi_objs.get(pi_name).get_fq_name() for pi_name in pi_names]
        pi1_obj, pi2_obj, pi3_obj, pi4_obj = [
            pi_objs.get(pi_name) for pi_name in pi_names]

        # Create VPG
        vpg_objs = self._create_vpgs(fabric_obj, ['vpg-1'])
        vpg_obj = vpg_objs['vpg-1']
        vpg_name = vpg_obj.get_fq_name()

        # Create VN
        vn_names = ['vn-%s-%s' % (test_id, count) for count in range(1, 3)]
        vn_objs = self._create_vns(proj_obj, vn_names)
        vn1_obj, vn2_obj = [vn_objs[vn_name] for vn_name in vn_names]

        # create a vmi with two phy int, tagged vlan case
        vmi_infos = [
            {'name': '%s-1' % test_id, 'vmi_id': '1',
             'parent_obj': proj_obj, 'vn': vn1_obj, 'vpg': vpg_obj.uuid,
             'fabric': fabric_name, 'pis': pi1_fq_name,
             'vlan': vlan_1, 'is_untagged': False}]
        vmi_objs = self._create_vmis(vmi_infos)
        vmi1_obj = vmi_objs.get('%s-1' % test_id)
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)

        # fomat annotations to look for in VPG
        vmi1_kvp = KeyValuePair(
            key='validation:%s/vn:%s/vlan_id:%d' % (
                validation, vn1_obj.uuid, vlan_1),
            value=vmi1_obj.uuid)
        # verify annotations are added
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)

        # verify that it has refs to two phy intf
        phy_refs = vpg_obj.get_physical_interface_refs()
        assert len(phy_refs) == 1

        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        assert vmi1_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)

        # add one phy.int from VMI and ensure annotations remains
        # same
        kv_pairs = self._create_kv_pairs(
            [pi1_fq_name, pi2_fq_name], fabric_name, vpg_name)
        vmi1_obj.set_virtual_machine_interface_bindings(kv_pairs)
        self.api.virtual_machine_interface_update(vmi1_obj)

        # verify annotations are added
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        # verify that it has refs to one phy intf
        phy_refs = vpg_obj.get_physical_interface_refs()
        assert len(phy_refs) == 2

        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        assert vmi1_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)

        # create a vmi with two phy int, untagged vlan case
        # this VMI addition replaces phy.int added previous
        # VMI. Per DM, this is the current implementation
        vmi_infos = [
            {'name': '%s-2' % test_id, 'vmi_id': '2',
             'parent_obj': proj_obj, 'vn': vn2_obj, 'vpg': vpg_obj.uuid,
             'fabric': fabric_name, 'pis': pi3_fq_name,
             'vlan': vlan_2, 'is_untagged': True}]
        vmi_objs = self._create_vmis(vmi_infos)
        vmi2_obj = vmi_objs.get('%s-2' % test_id)
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)

        # fomat annotations to look for in VPG
        vmi2_kvp = KeyValuePair(
            key='validation:%s/vn:%s/vlan_id:%s' % (
                validation, vn2_obj.uuid, vlan_2),
            value=vmi2_obj.uuid)
        vmi2_untagged_kvp = KeyValuePair(
            key='validation:%s/untagged_vlan_id' % validation,
            value='%s:%s' % (vlan_2, vmi2_obj.uuid))

        # verify annotations are added
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        # verify that it has refs to two phy intf
        phy_refs = vpg_obj.get_physical_interface_refs()
        assert len(phy_refs) == 1

        # remove one phy.int from VMI and ensure annotations remains
        # same. this is untagged case
        kv_pairs = self._create_kv_pairs(
            [pi3_fq_name, pi4_fq_name], fabric_name, vpg_name,
            tor_port_vlan_id=vlan_2)
        vmi2_obj.set_virtual_machine_interface_bindings(kv_pairs)
        self.api.virtual_machine_interface_update(vmi2_obj)

        # verify that it has refs to one phy intf
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        phy_refs = vpg_obj.get_physical_interface_refs()
        assert len(phy_refs) == 2

        # verify annotations remains same
        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        assert vmi1_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)
        assert vmi2_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_kvp, vpg_kvps)
        assert vmi2_untagged_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_untagged_kvp, vpg_kvps)

        # Delete VMIs from VPG
        self.api.virtual_machine_interface_delete(id=vmi1_obj.uuid)
        self.api.virtual_machine_interface_delete(id=vmi2_obj.uuid)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        self.api.physical_interface_delete(id=pi1_obj.uuid)
        self.api.physical_interface_delete(id=pi2_obj.uuid)
        self.api.physical_interface_delete(id=pi3_obj.uuid)
        self.api.physical_interface_delete(id=pi4_obj.uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_enterprise_add_new_pi_to_vmi(self):
        """Verify adding a PI to VMI in enterprise style."""
        validation = 'enterprise'
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()
        self._test_add_new_pi_to_vmi(
            proj_obj, fabric_obj, pr_obj, validation)

    def test_sp_add_new_pi_to_vmi(self):
        """Verify adding a PI to VMI in SP style."""
        validation = 'serviceprovider'
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites(
            enterprise_style_flag=False)
        self._test_add_new_pi_to_vmi(
            proj_obj, fabric_obj, pr_obj, validation)

    def _test_delete_existing_pi_from_vmi(
            self, proj_obj, fabric_obj, pr_obj, validation):
        vlan_1 = 42
        vlan_2 = '4094'
        fabric_name = fabric_obj.get_fq_name()
        test_id = self.id()

        pi_names = ['%s_phy_intf_%s' % (test_id, i) for i in range(1, 5)]
        pi_objs = self._create_pi_objects(pr_obj, pi_names)
        pi1_fq_name, pi2_fq_name, pi3_fq_name, pi4_fq_name = [
            pi_objs.get(pi_name).get_fq_name() for pi_name in pi_names]
        pi1_obj, pi2_obj, pi3_obj, pi4_obj = [
            pi_objs.get(pi_name) for pi_name in pi_names]

        # Create VPG
        vpg_objs = self._create_vpgs(fabric_obj, ['vpg-1'])
        vpg_obj = vpg_objs['vpg-1']
        vpg_name = vpg_obj.get_fq_name()

        # Create VN
        vn_names = ['vn-%s-%s' % (test_id, count) for count in range(1, 3)]
        vn_objs = self._create_vns(proj_obj, vn_names)
        vn1_obj, vn2_obj = [vn_objs[vn_name] for vn_name in vn_names]

        # create a vmi with two phy int, tagged vlan case
        vmi_infos = [
            {'name': '%s-1' % test_id, 'vmi_id': '1',
             'parent_obj': proj_obj, 'vn': vn1_obj, 'vpg': vpg_obj.uuid,
             'fabric': fabric_name, 'pis': [pi1_fq_name, pi2_fq_name],
             'vlan': vlan_1, 'is_untagged': False}]
        vmi_objs = self._create_vmis(vmi_infos)
        vmi1_obj = vmi_objs.get('%s-1' % test_id)
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)

        # fomat annotations to look for in VPG
        vmi1_kvp = KeyValuePair(
            key='validation:%s/vn:%s/vlan_id:%d' % (
                validation, vn1_obj.uuid, vlan_1),
            value=vmi1_obj.uuid)
        # verify annotations are added
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)

        # verify that it has refs to two phy intf
        phy_refs = vpg_obj.get_physical_interface_refs()
        assert len(phy_refs) == 2

        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        assert vmi1_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)

        # remove one phy.int from VMI and ensure annotations remains
        # same
        kv_pairs = self._create_kv_pairs(pi1_fq_name, fabric_name, vpg_name)
        vmi1_obj.set_virtual_machine_interface_bindings(kv_pairs)
        self.api.virtual_machine_interface_update(vmi1_obj)

        # verify annotations are added
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        # verify that it has refs to one phy intf
        phy_refs = vpg_obj.get_physical_interface_refs()
        assert len(phy_refs) == 1

        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        assert vmi1_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)

        # create a vmi with two phy int, untagged vlan case
        # this VMI addition replaces phy.int added previous
        # VMI. Per DM, this is the current implementation
        vmi_infos = [
            {'name': '%s-2' % test_id, 'vmi_id': '2',
             'parent_obj': proj_obj, 'vn': vn2_obj, 'vpg': vpg_obj.uuid,
             'fabric': fabric_name, 'pis': [pi3_fq_name, pi4_fq_name],
             'vlan': vlan_2, 'is_untagged': True}]
        vmi_objs = self._create_vmis(vmi_infos)
        vmi2_obj = vmi_objs.get('%s-2' % test_id)
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)

        # fomat annotations to look for in VPG
        vmi2_kvp = KeyValuePair(
            key='validation:%s/vn:%s/vlan_id:%s' % (
                validation, vn2_obj.uuid, vlan_2),
            value=vmi2_obj.uuid)
        vmi2_untagged_kvp = KeyValuePair(
            key='validation:%s/untagged_vlan_id' % validation,
            value='%s:%s' % (vlan_2, vmi2_obj.uuid))

        # verify annotations are added
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        # verify that it has refs to two phy intf
        phy_refs = vpg_obj.get_physical_interface_refs()
        assert len(phy_refs) == 2

        # remove one phy.int from VMI and ensure annotations remains
        # same. this is untagged case
        kv_pairs = self._create_kv_pairs(
            pi3_fq_name, fabric_name, vpg_name, tor_port_vlan_id=vlan_2)
        vmi2_obj.set_virtual_machine_interface_bindings(kv_pairs)
        self.api.virtual_machine_interface_update(vmi2_obj)

        # verify that it has refs to one phy intf
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        phy_refs = vpg_obj.get_physical_interface_refs()
        assert len(phy_refs) == 1

        # verify annotations remains same
        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        assert vmi1_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)
        assert vmi2_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_kvp, vpg_kvps)
        assert vmi2_untagged_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_untagged_kvp, vpg_kvps)

        # Delete VMIs from VPG
        self.api.virtual_machine_interface_delete(id=vmi1_obj.uuid)
        self.api.virtual_machine_interface_delete(id=vmi2_obj.uuid)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        self.api.physical_interface_delete(id=pi1_obj.uuid)
        self.api.physical_interface_delete(id=pi2_obj.uuid)
        self.api.physical_interface_delete(id=pi3_obj.uuid)
        self.api.physical_interface_delete(id=pi4_obj.uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_enterprise_delete_existing_pi_from_vmi(self):
        """Verify deleting a PI from VMI."""
        validation = 'enterprise'
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()
        self._test_delete_existing_pi_from_vmi(
            proj_obj, fabric_obj, pr_obj, validation)

    def test_sp_delete_existing_pi_from_vmi(self):
        """Verify deleting a PI from VMI."""
        validation = 'serviceprovider'
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites(
            enterprise_style_flag=False)
        self._test_delete_existing_pi_from_vmi(
            proj_obj, fabric_obj, pr_obj, validation)

    def test_reinit_adds_enterprise_annotations(self):
        """Verify annotations are added in the enterprise VPG."""
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        vlan_1 = 42
        validation = 'enterprise'

        esi_id = '00:11:22:33:44:55:66:77:88:99'
        vlan_1 = 42
        vlan_2 = '4094'
        vlan_3 = 4093
        validation = 'enterprise'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name = pi_obj.get_fq_name()

        # Create VPG
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name = vpg_obj.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj_1 = VirtualMachineInterface(self.id() + "1",
                                            parent_obj=proj_obj)
        vmi_obj_1.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj_1.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_1.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(
                sub_interface_vlan_tag=vlan_1))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj_1)
        vpg_obj.add_virtual_machine_interface(vmi_obj_1)
        self.api.virtual_port_group_update(vpg_obj)

        # Attach Second VMI with untagged vlan
        vn2 = VirtualNetwork('vn2-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn2)

        # Create first untagged VMI and attach it to Virtual Port Group
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn2)

        # Create KV_Pairs for this VMI with an untagged VLAN
        # If tor_port_vlan_id is set, then it signifies a untagged VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name,
                                         tor_port_vlan_id=vlan_2)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)
        vmi_uuid_2 = self.api.virtual_machine_interface_create(vmi_obj_2)
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_obj.add_virtual_machine_interface(vmi_obj_2)
        self.api.virtual_port_group_update(vpg_obj)

        # Create another third VN with second tagged VMI
        vn3 = VirtualNetwork('vn3-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn3)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn3
        vmi_obj_3 = VirtualMachineInterface(self.id() + "3",
                                            parent_obj=proj_obj)
        vmi_obj_3.set_virtual_network(vn3)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj_3.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_3.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(
                sub_interface_vlan_tag=vlan_3))
        vmi_uuid_3 = self.api.virtual_machine_interface_create(vmi_obj_3)
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_obj.add_virtual_machine_interface(vmi_obj_3)
        self.api.virtual_port_group_update(vpg_obj)

        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)

        # fomat annotations to look for in VPG
        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        vmi1_kvp = KeyValuePair(
            key='validation:%s/vn:%s/vlan_id:%d' % (
                validation, vn1.uuid, vlan_1),
            value=vmi_uuid_1)
        vmi2_kvp = KeyValuePair(
            key='validation:%s/vn:%s/vlan_id:%s' % (
                validation, vn2.uuid, vlan_2),
            value=vmi_uuid_2)
        vmi2_untagged_kvp = KeyValuePair(
            key='validation:%s/untagged_vlan_id' % validation,
            value='%s:%s' % (vlan_2, vmi_uuid_2))
        vmi3_kvp = KeyValuePair(
            key='validation:%s/vn:%s/vlan_id:%d' % (
                validation, vn3.uuid, vlan_3),
            value=vmi_uuid_3)

        # verify annotations are added
        assert vmi1_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)
        assert vmi2_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_kvp, vpg_kvps)
        assert vmi2_untagged_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_untagged_kvp, vpg_kvps)
        assert vmi3_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi3_kvp, vpg_kvps)

        # annotations to remove
        updates = [{'field': 'annotations',
                    'operation': 'delete',
                    'position': kvp.key} for kvp in vpg_kvps]
        # remote annotations at DB
        self._api_server._db_conn._object_db.prop_collection_update(
            'virtual_port_group', vpg_obj.uuid, updates)

        # verify that annotations are removed from VPG
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        assert vmi1_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)
        assert vmi2_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi2_kvp, vpg_kvps)
        assert vmi2_untagged_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi2_untagged_kvp, vpg_kvps)
        assert vmi3_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi3_kvp, vpg_kvps)

        # API server DB reinit
        self._api_server._db_init_entries()

        # verify that annoations are added back
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        assert vmi1_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)
        assert vmi2_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_kvp, vpg_kvps)
        assert vmi2_untagged_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_untagged_kvp, vpg_kvps)
        assert vmi3_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi3_kvp, vpg_kvps)

        # Delete VMIs from VPG
        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_machine_interface_delete(id=vmi_uuid_2)
        self.api.virtual_machine_interface_delete(id=vmi_uuid_3)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_reinit_adds_sp_annotations(self):
        """Verify annotations are added in the service provider VPG."""
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites(
            enterprise_style_flag=False)

        esi_id = '00:11:22:33:44:55:66:77:88:99'
        vlan_1 = 42
        vlan_2 = '4094'
        vlan_3 = 4093
        validation = 'serviceprovider'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name = pi_obj.get_fq_name()

        # Create VPG
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name = vpg_obj.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj_1 = VirtualMachineInterface(self.id() + "1",
                                            parent_obj=proj_obj)
        vmi_obj_1.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj_1.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_1.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(
                sub_interface_vlan_tag=vlan_1))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj_1)
        vpg_obj.add_virtual_machine_interface(vmi_obj_1)
        self.api.virtual_port_group_update(vpg_obj)

        # Attach Second VMI with untagged vlan
        vn2 = VirtualNetwork('vn2-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn2)

        # Create first untagged VMI and attach it to Virtual Port Group
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn2)

        # Create KV_Pairs for this VMI with an untagged VLAN
        # If tor_port_vlan_id is set, then it signifies a untagged VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name,
                                         tor_port_vlan_id=vlan_2)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)
        vmi_uuid_2 = self.api.virtual_machine_interface_create(vmi_obj_2)
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_obj.add_virtual_machine_interface(vmi_obj_2)
        self.api.virtual_port_group_update(vpg_obj)

        # Create another third VN with second tagged VMI
        vn3 = VirtualNetwork('vn3-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn3)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn3
        vmi_obj_3 = VirtualMachineInterface(self.id() + "3",
                                            parent_obj=proj_obj)
        vmi_obj_3.set_virtual_network(vn3)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj_3.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_3.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(
                sub_interface_vlan_tag=vlan_3))
        vmi_uuid_3 = self.api.virtual_machine_interface_create(vmi_obj_3)
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_obj.add_virtual_machine_interface(vmi_obj_3)
        self.api.virtual_port_group_update(vpg_obj)

        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)

        # fomat annotations to look for in VPG
        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        vmi1_kvp = KeyValuePair(
            key='validation:%s/vn:%s/vlan_id:%d' % (
                validation, vn1.uuid, vlan_1),
            value=vmi_uuid_1)
        vmi2_kvp = KeyValuePair(
            key='validation:%s/vn:%s/vlan_id:%s' % (
                validation, vn2.uuid, vlan_2),
            value=vmi_uuid_2)
        vmi2_untagged_kvp = KeyValuePair(
            key='validation:%s/untagged_vlan_id' % validation,
            value='%s:%s' % (vlan_2, vmi_uuid_2))
        vmi3_kvp = KeyValuePair(
            key='validation:%s/vn:%s/vlan_id:%d' % (
                validation, vn3.uuid, vlan_3),
            value=vmi_uuid_3)

        # verify annotations are added
        assert vmi1_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)
        assert vmi2_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_kvp, vpg_kvps)
        assert vmi2_untagged_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_untagged_kvp, vpg_kvps)
        assert vmi3_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi3_kvp, vpg_kvps)

        # annotations to remove
        updates = [{'field': 'annotations',
                    'operation': 'delete',
                    'position': kvp.key} for kvp in vpg_kvps]
        # remote annotations at DB
        self._api_server._db_conn._object_db.prop_collection_update(
            'virtual_port_group', vpg_obj.uuid, updates)

        # verify that annotations are removed from VPG
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        assert vmi1_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)
        assert vmi2_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi2_kvp, vpg_kvps)
        assert vmi2_untagged_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi2_untagged_kvp, vpg_kvps)
        assert vmi3_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi3_kvp, vpg_kvps)

        # API server DB reinit
        self._api_server._db_init_entries()

        # verify that annoations are added back
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        assert vmi1_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)
        assert vmi2_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_kvp, vpg_kvps)
        assert vmi2_untagged_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_untagged_kvp, vpg_kvps)
        assert vmi3_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi3_kvp, vpg_kvps)

        # Delete VMIs from VPG
        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_machine_interface_delete(id=vmi_uuid_2)
        self.api.virtual_machine_interface_delete(id=vmi_uuid_3)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_enterprise_vpg_annotations(self):
        """Verify annotations are added in the enterprise VPG."""
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()

        esi_id = '00:11:22:33:44:55:66:77:88:99'
        vlan_1 = 42
        vlan_2 = '4094'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name = pi_obj.get_fq_name()

        # Create VPG
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name = vpg_obj.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj_1 = VirtualMachineInterface(self.id() + "1",
                                            parent_obj=proj_obj)
        vmi_obj_1.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj_1.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_1.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(
                sub_interface_vlan_tag=vlan_1))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj_1)
        vpg_obj.add_virtual_machine_interface(vmi_obj_1)
        self.api.virtual_port_group_update(vpg_obj)

        # Attach Second VMI with untagged vlan
        vn2 = VirtualNetwork('vn2-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn2)

        # Create first untagged VMI and attach it to Virtual Port Group
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn2)

        # Create KV_Pairs for this VMI with an untagged VLAN
        # If tor_port_vlan_id is set, then it signifies a untagged VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name,
                                         tor_port_vlan_id=vlan_2)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)
        vmi_uuid_2 = self.api.virtual_machine_interface_create(vmi_obj_2)
        vpg_obj.add_virtual_machine_interface(vmi_obj_2)
        self.api.virtual_port_group_update(vpg_obj)

        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)

        # verify annotations are added
        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        vmi1_kvp = KeyValuePair(
            key='validation:enterprise/vn:%s/vlan_id:%d' % (
                vn1.uuid, vlan_1),
            value=vmi_uuid_1)
        vmi2_kvp = KeyValuePair(
            key='validation:enterprise/vn:%s/vlan_id:%s' % (
                vn2.uuid, vlan_2),
            value=vmi_uuid_2)
        vmi2_untagged_kvp = KeyValuePair(
            key='validation:enterprise/untagged_vlan_id',
            value='%s:%s' % (vlan_2, vmi_uuid_2))
        assert vmi1_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)
        assert vmi2_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_kvp, vpg_kvps)
        assert vmi2_untagged_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_untagged_kvp, vpg_kvps)

        # Delete VMIs from VPG
        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_machine_interface_delete(id=vmi_uuid_2)

        # Verify annotations are removed when VMI is
        # unassociated
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []

        assert vmi1_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)
        assert vmi2_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi2_kvp, vpg_kvps)
        assert vmi2_untagged_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi2_untagged_kvp, vpg_kvps)

        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_sp_vpg_annotations(self):
        """Verify annotations are added in the SP VPG."""
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites(
            enterprise_style_flag=False)

        esi_id = '00:11:22:33:44:55:66:77:88:99'
        vlan_1 = 42
        vlan_2 = '4094'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name = pi_obj.get_fq_name()

        # Create VPG
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name = vpg_obj.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj_1 = VirtualMachineInterface(self.id() + "1",
                                            parent_obj=proj_obj)
        vmi_obj_1.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj_1.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_1.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(
                sub_interface_vlan_tag=vlan_1))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj_1)
        vpg_obj.add_virtual_machine_interface(vmi_obj_1)
        self.api.virtual_port_group_update(vpg_obj)

        # Attach Second VMI with untagged vlan
        vn2 = VirtualNetwork('vn2-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn2)

        # Create first untagged VMI and attach it to Virtual Port Group
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn2)

        # Create KV_Pairs for this VMI with an untagged VLAN
        # If tor_port_vlan_id is set, then it signifies a untagged VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name,
                                         tor_port_vlan_id=vlan_2)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)
        vmi_uuid_2 = self.api.virtual_machine_interface_create(vmi_obj_2)
        vpg_obj.add_virtual_machine_interface(vmi_obj_2)
        self.api.virtual_port_group_update(vpg_obj)

        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)

        # verify annotations are added
        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        vmi1_kvp = KeyValuePair(
            key='validation:serviceprovider/vn:%s/vlan_id:%d' % (
                vn1.uuid, vlan_1),
            value=vmi_uuid_1)
        vmi2_kvp = KeyValuePair(
            key='validation:serviceprovider/vn:%s/vlan_id:%s' % (
                vn2.uuid, vlan_2),
            value=vmi_uuid_2)
        vmi2_untagged_kvp = KeyValuePair(
            key='validation:serviceprovider/untagged_vlan_id',
            value='%s:%s' % (vlan_2, vmi_uuid_2))
        assert vmi1_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)
        assert vmi2_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_kvp, vpg_kvps)
        assert vmi2_untagged_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_untagged_kvp, vpg_kvps)

        # Delete VMIs from VPG
        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_machine_interface_delete(id=vmi_uuid_2)

        # Verify annotations are removed when VMI is
        # unassociated
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []

        assert vmi1_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)
        assert vmi2_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi2_kvp, vpg_kvps)
        assert vmi2_untagged_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi2_untagged_kvp, vpg_kvps)

        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_single_untagged_vmi_for_enterprise(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()

        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)
        self._validate_untagged_vmis(fabric_obj, proj_obj, pi_obj)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_single_untagged_vmi_for_service_provider(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites(
            enterprise_style_flag=False)

        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)
        self._validate_untagged_vmis(fabric_obj, proj_obj, pi_obj)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_same_vn_in_same_vpg_for_enterprise(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()

        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name = pi_obj.get_fq_name()

        # Create VPG
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name = vpg_obj.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        vpg_obj.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj)

        # Now, try to create the second VMI.
        # This should fail as a VN can be attached to a VPG only once
        # in a Enterprise style fabric
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_2.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=43))
        with ExpectedException(BadRequest):
            self.api.virtual_machine_interface_create(vmi_obj_2)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def _test_update_vlan_in_vmi_in_same_vpg(
            self, validation, proj_obj, fabric_obj, pr_obj):
        """Create and verify below sequence works as expected.

        Sequence of validation in this test
        :User Creates:
        :42 - VMI1 (tagged)
        :4094 - VMI 2(untagged)
        :System Creates:
        :    42 VMI-1 annotation
        :    4094 VMI-2 annotation
        :    4094 VMI-2 untagged annotation

        :User changes vlan-ids
        :43 - VMI1 (tagged)
        :4093 - VMI 2 (untagged)
        :System Creates/Updates:
        :    43 VMI-1 annotation
        :    4093 VMI-2 annotation
        :    4093 VMI-2 untagged annotation
        :    42 VMI-1  annotation - removed
        :    4094 VMI-2  annotation - removed
        :    4094 VMI-2 untagged annotation - removed

        :User changes untagged to tagged
        :4093 - VMI 2 (tagged)
        :System Creates/Updates:
        :    4093 - VMI-2 annotation (retains)
        :    4093 VMI-2 untagged annotation - removed
        :    43 VMI-1 annotation

        :User changes tagged to untagged
        :43 - VMI1 (untagged)
        :System Creates/Updates:
        :    43 VMI-1 annotation (retains)
        :    4093 - VMI-2 annotation (retains)
        :    43 - VMI-1 untagged annotation - added
        """
        vlan_1 = 42
        vlan_2 = '4094'
        vlan_3 = 43
        vlan_4 = '4093'
        fabric_name = fabric_obj.get_fq_name()
        test_id = self.id()

        # Create Physical Interfaces
        pi_names = ['%s_phy_intf_%s' % (test_id, i) for i in range(1, 3)]
        pi_objs = self._create_pi_objects(pr_obj, pi_names)
        pi1_fq_name, pi2_fq_name = [
            pi_objs.get(pi_name).get_fq_name() for pi_name in pi_names]
        pi1_obj, pi2_obj = [
            pi_objs.get(pi_name) for pi_name in pi_names]

        # Create VPG
        vpg_objs = self._create_vpgs(fabric_obj, ['vpg-1'])
        vpg_obj = vpg_objs['vpg-1']

        # Create VNs
        vn_names = ['vn-%s-%s' % (test_id, count) for count in range(1, 3)]
        vn_objs = self._create_vns(proj_obj, vn_names)
        vn1_obj, vn2_obj = [vn_objs[vn_name] for vn_name in vn_names]

        # create two VMIs one with tagged VLAN and other with
        # untagged VLAN
        vmi_infos = [
            {'name': '%s-1' % test_id, 'vmi_id': '1',
             'parent_obj': proj_obj, 'vn': vn1_obj, 'vpg': vpg_obj.uuid,
             'fabric': fabric_name, 'pis': pi1_fq_name,
             'vlan': vlan_1, 'is_untagged': False},
            {'name': '%s-2' % test_id, 'vmi_id': '2',
             'parent_obj': proj_obj, 'vn': vn2_obj, 'vpg': vpg_obj.uuid,
             'fabric': fabric_name, 'pis': pi2_fq_name,
             'vlan': vlan_2, 'is_untagged': True}]
        vmi_objs = self._create_vmis(vmi_infos)
        vmi1_obj = vmi_objs.get('%s-1' % test_id)
        vmi2_obj = vmi_objs.get('%s-2' % test_id)
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)

        # fomat annotations to look for in VPG
        vmi1_kvp = KeyValuePair(
            key='validation:%s/vn:%s/vlan_id:%d' % (
                validation, vn1_obj.uuid, vlan_1),
            value=vmi1_obj.uuid)
        vmi2_kvp = KeyValuePair(
            key='validation:%s/vn:%s/vlan_id:%s' % (
                validation, vn2_obj.uuid, vlan_2),
            value=vmi2_obj.uuid)
        vmi2_untagged_kvp = KeyValuePair(
            key='validation:%s/untagged_vlan_id' % validation,
            value='%s:%s' % (vlan_2, vmi2_obj.uuid))

        # verify annotations are added
        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        assert vmi1_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)
        assert vmi2_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_kvp, vpg_kvps)
        assert vmi2_untagged_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_untagged_kvp, vpg_kvps)

        # update vlan-ID in both VMIs
        vmi_infos = [
            {'name': '%s-1' % test_id, 'vmi_uuid': vmi1_obj.uuid,
             'vpg': vpg_obj.uuid,
             'fabric': fabric_name, 'pis': pi1_fq_name,
             'vlan': vlan_3, 'is_untagged': False},
            {'name': '%s-2' % test_id, 'vmi_uuid': vmi2_obj.uuid,
             'vpg': vpg_obj.uuid,
             'fabric': fabric_name, 'pis': pi2_fq_name,
             'vlan': vlan_4, 'is_untagged': True}]
        vmi_objs = self._update_vmis(vmi_infos)
        vmi1_obj = vmi_objs.get('%s-1' % test_id)
        vmi2_obj = vmi_objs.get('%s-2' % test_id)
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)

        # fomat annotations to look for in VPG
        vmi3_kvp = KeyValuePair(
            key='validation:%s/vn:%s/vlan_id:%d' % (
                validation, vn1_obj.uuid, vlan_3),
            value=vmi1_obj.uuid)
        vmi4_kvp = KeyValuePair(
            key='validation:%s/vn:%s/vlan_id:%s' % (
                validation, vn2_obj.uuid, vlan_4),
            value=vmi2_obj.uuid)
        vmi4_untagged_kvp = KeyValuePair(
            key='validation:%s/untagged_vlan_id' % validation,
            value='%s:%s' % (vlan_4, vmi2_obj.uuid))

        # verify annotations are updated
        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        # verify annotations are updated
        assert vmi3_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)
        assert vmi4_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi2_kvp, vpg_kvps)
        assert vmi4_untagged_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi4_untagged_kvp, vpg_kvps)
        assert vmi1_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi1_kvp, vpg_kvps)
        assert vmi2_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi2_kvp, vpg_kvps)
        assert vmi2_untagged_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi2_untagged_kvp, vpg_kvps)

        # Change VLAN type from untagged to tagged
        vmi_infos = [
            {'name': '%s-2' % test_id, 'vmi_uuid': vmi2_obj.uuid,
             'vpg': vpg_obj.uuid,
             'fabric': fabric_name, 'pis': pi2_fq_name,
             'vlan': vlan_4, 'is_untagged': False}]
        vmi_objs = self._update_vmis(vmi_infos)
        vmi2_obj = vmi_objs.get('%s-2' % test_id)
        # verify annotations are updated
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        # should persist
        assert vmi3_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi3_kvp, vpg_kvps)
        assert vmi4_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi4_kvp, vpg_kvps)
        # should have got removed
        assert vmi4_untagged_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi4_untagged_kvp, vpg_kvps)

        # Change VLAN type from tagged to untagged
        vmi_infos = [
            {'name': '%s-1' % test_id, 'vmi_uuid': vmi1_obj.uuid,
             'vpg': vpg_obj.uuid,
             'fabric': fabric_name, 'pis': pi1_fq_name,
             'vlan': '%s' % vlan_3, 'is_untagged': True}]
        vmi_objs = self._update_vmis(vmi_infos)
        vmi1_obj = vmi_objs.get('%s-1' % test_id)
        # verify annotations are updated
        vpg_obj = self.api.virtual_port_group_read(id=vpg_obj.uuid)
        vpg_annotations = vpg_obj.get_annotations() or KeyValuePairs()
        vpg_kvps = vpg_annotations.get_key_value_pair() or []
        # format new annoation
        vmi3_untagged_kvp = KeyValuePair(
            key='validation:%s/untagged_vlan_id' % validation,
            value='%s:%s' % (vlan_3, vmi1_obj.uuid))

        # newly created
        assert vmi3_untagged_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi3_untagged_kvp, vpg_kvps)
        # should persist
        assert vmi3_kvp in vpg_kvps, \
            "(%s) kv pair not found in vpg kvps (%s)" % (
                vmi3_kvp, vpg_kvps)
        assert vmi4_kvp in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi4_kvp, vpg_kvps)
        assert vmi4_untagged_kvp not in vpg_kvps, \
            "(%s) kv pair found in vpg kvps (%s)" % (
                vmi4_untagged_kvp, vpg_kvps)

        self.api.virtual_machine_interface_delete(id=vmi1_obj.uuid)
        self.api.virtual_machine_interface_delete(id=vmi2_obj.uuid)
        self.api.virtual_network_delete(id=vn1_obj.uuid)
        self.api.virtual_network_delete(id=vn2_obj.uuid)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        self.api.physical_interface_delete(id=pi1_obj.uuid)
        self.api.physical_interface_delete(id=pi2_obj.uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)
        self.api.project_delete(id=proj_obj.uuid)

    def test_update_vlan_in_vmi_in_same_vpg(self):
        # enterprise with fabric level validations
        validation = 'enterprise'
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()
        self._test_update_vlan_in_vmi_in_same_vpg(
            validation, proj_obj, fabric_obj, pr_obj)

        # enterprise without fabric level validations
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites(
            disable_vlan_vn_uniqueness_check=True)
        self._test_update_vlan_in_vmi_in_same_vpg(
            validation, proj_obj, fabric_obj, pr_obj)

        # service provider style
        validation = 'serviceprovider'
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites(
            enterprise_style_flag=False)
        self._test_update_vlan_in_vmi_in_same_vpg(
            validation, proj_obj, fabric_obj, pr_obj)

    def test_same_vn_with_same_vlan_across_vpg_in_enterprise(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()

        # Create Physical Interface for VPG-1
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_1 = pi_obj.get_fq_name()

        # Create Physical Interface for VPG-2
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface2'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid_2 = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid_2)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_2 = pi_obj.get_fq_name()

        # Create VPG-1
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj_1 = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name_1 = vpg_obj_1.get_fq_name()

        # Create VPG-2
        vpg_name = "vpg-2"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj_2 = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name_2 = vpg_obj_2.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name_1,
                                         fabric_name,
                                         vpg_name_1)

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        vpg_obj_1.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj_1)

        # Create a VMI that's attached to vpg-2 and having reference
        # to vn1
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name_2,
                                         fabric_name,
                                         vpg_name_2)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_2.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_2 = self.api.virtual_machine_interface_create(vmi_obj_2)
        vpg_obj_2.add_virtual_machine_interface(vmi_obj_2)
        self.api.virtual_port_group_update(vpg_obj_2)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_machine_interface_delete(id=vmi_uuid_2)
        self.api.virtual_port_group_delete(id=vpg_obj_1.uuid)
        self.api.virtual_port_group_delete(id=vpg_obj_2.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_interface_delete(id=pi_uuid_2)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_same_vn_with_different_vlan_across_vpg_in_enterprise(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()

        # Create Physical Interface for VPG-1
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_1 = pi_obj.get_fq_name()

        # Create Physical Interface for VPG-2
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface2'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid_2 = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid_2)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_2 = pi_obj.get_fq_name()

        # Create VPG-1
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj_1 = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name_1 = vpg_obj_1.get_fq_name()

        # Create VPG-2
        vpg_name = "vpg-2"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj_2 = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name_2 = vpg_obj_2.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name_1,
                                         fabric_name,
                                         vpg_name_1)

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        vpg_obj_1.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj_1)

        # Create a VMI that's attached to vpg-2 and having reference
        # to vn1
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name_2,
                                         fabric_name,
                                         vpg_name_2)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)

        # Unlike test_same_vn_with_same_vlan_across_vpg_in_enterprise, we
        # set a different vlan_tag and it should fail
        vmi_obj_2.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=43))

        with ExpectedException(BadRequest):
            self.api.virtual_machine_interface_create(vmi_obj_2)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_port_group_delete(id=vpg_obj_1.uuid)
        self.api.virtual_port_group_delete(id=vpg_obj_2.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_interface_delete(id=pi_uuid_2)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_different_vn_on_same_vlan_across_vpgs_in_enterprise(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()

        # Create Physical Interface for VPG-1
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_1 = pi_obj.get_fq_name()

        # Create Physical Interface for VPG-2
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface2'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid_2 = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid_2)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_2 = pi_obj.get_fq_name()

        # Create VPG-1
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj_1 = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name_1 = vpg_obj_1.get_fq_name()

        # Create VPG-2
        vpg_name = "vpg-2"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj_2 = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name_2 = vpg_obj_2.get_fq_name()

        # Create VN-1
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create VN-2
        vn2 = VirtualNetwork('vn2-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn2)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name_1,
                                         fabric_name,
                                         vpg_name_1)

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        vpg_obj_1.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj_1)

        # Create a VMI that's attached to vpg-2 and having reference
        # to vn2, but with same vlan_tag=42, this should fail
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn2)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name_2,
                                         fabric_name,
                                         vpg_name_2)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_2.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))

        with ExpectedException(BadRequest):
            self.api.virtual_machine_interface_create(vmi_obj_2)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_port_group_delete(id=vpg_obj_1.uuid)
        self.api.virtual_port_group_delete(id=vpg_obj_2.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_interface_delete(id=pi_uuid_2)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_disable_different_vn_on_same_vlan_across_vpgs_in_enterprise(self):
        """Verify disable_vlan_vn_uniqueness_check is True.

        When disable_vlan_vn_uniqueness_check=True, validations across
        VPGs are disabled.
        """
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites(
            disable_vlan_vn_uniqueness_check=True)

        # Create Physical Interface for VPG-1
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_1 = pi_obj.get_fq_name()

        # Create Physical Interface for VPG-2
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface2'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid_2 = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid_2)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_2 = pi_obj.get_fq_name()

        # Create VPG-1
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj_1 = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name_1 = vpg_obj_1.get_fq_name()

        # Create VPG-2
        vpg_name = "vpg-2"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj_2 = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name_2 = vpg_obj_2.get_fq_name()

        # Create VN-1
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create VN-2
        vn2 = VirtualNetwork('vn2-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn2)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name_1,
                                         fabric_name,
                                         vpg_name_1)

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        vpg_obj_1.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj_1)

        # Create a VMI that's attached to vpg-2 and having reference
        # to vn2, but with same vlan_tag=42, this should fail
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn2)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name_2,
                                         fabric_name,
                                         vpg_name_2)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_2.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))

        # since disable_vlan_vn_uniqueness_check=True
        # validations at second VPG do not happen
        vmi_uuid_2 = self.api.virtual_machine_interface_create(vmi_obj_2)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_2)
        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_port_group_delete(id=vpg_obj_1.uuid)
        self.api.virtual_port_group_delete(id=vpg_obj_2.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_interface_delete(id=pi_uuid_2)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_single_vn_in_vpg_with_same_vlan_twice_for_service_provider(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites(
            enterprise_style_flag=False)

        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name = pi_obj.get_fq_name()

        # Create VPG
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name = vpg_obj.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        vpg_obj.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj)

        # Now, try to create the second VMI.
        # This should fail as a VN can only be attached to a VPG with different
        # VLAN in a service provider style fabric
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_2.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        with ExpectedException(BadRequest):
            self.api.virtual_machine_interface_create(vmi_obj_2)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_single_vn_in_vpg_with_different_vlan_for_service_provider(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites(
            enterprise_style_flag=False)

        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name = pi_obj.get_fq_name()

        # Create VPG
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name = vpg_obj.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        vpg_obj.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj)

        # Now, try to create the second VMI with same VN, but with a different
        # VLAN. This should pass
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name)

        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_2.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=43))
        vmi_uuid_2 = self.api.virtual_machine_interface_create(vmi_obj_2)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_machine_interface_delete(id=vmi_uuid_2)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_vmi_update(self):
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            enterprise_style_flag=False, create_second_pr=True)

        pr_obj_1 = pr_objs[0]
        pr_obj_2 = pr_objs[1]

        # Create first PI
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi_1 = PhysicalInterface(name=pi_name,
                                 parent_obj=pr_obj_1,
                                 ethernet_segment_identifier=esi_id)
        pi_uuid_1 = self._vnc_lib.physical_interface_create(pi_1)
        pi_obj_1 = self._vnc_lib.physical_interface_read(id=pi_uuid_1)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_1 = pi_obj_1.get_fq_name()

        # Create second PI
        pi_name = self.id() + '_physical_interface2'
        pi_2 = PhysicalInterface(name=pi_name,
                                 parent_obj=pr_obj_2,
                                 ethernet_segment_identifier=esi_id)
        pi_uuid_2 = self._vnc_lib.physical_interface_create(pi_2)
        pi_obj_2 = self._vnc_lib.physical_interface_read(id=pi_uuid_2)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name_2 = pi_obj_2.get_fq_name()

        # Create VPG
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name = vpg_obj.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Populate binding profile to be used in VMI create
        binding_profile = {'local_link_information': [
            {'port_id': pi_fq_name_1[2],
             'switch_id': pi_fq_name_1[2],
             'fabric': fabric_name[-1],
             'switch_info': pi_fq_name_1[1]},
            {'port_id': pi_fq_name_2[2],
             'switch_id': pi_fq_name_2[2],
             'fabric': fabric_name[-1],
             'switch_info': pi_fq_name_2[1]}]}

        kv_pairs = KeyValuePairs(
            [KeyValuePair(key='vpg', value=vpg_name[-1]),
             KeyValuePair(key='vif_type', value='vrouter'),
             KeyValuePair(key='vnic_type', value='baremetal'),
             KeyValuePair(key='profile',
                          value=json.dumps(binding_profile))])

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        # Read physical interface type, it should be set to access
        pi_obj_1 = self._vnc_lib.physical_interface_read(id=pi_uuid_1)
        pi_obj_2 = self._vnc_lib.physical_interface_read(id=pi_uuid_2)
        intf_type_pi_1 = pi_obj_1.get_physical_interface_type()
        self.assertEqual(intf_type_pi_1, 'access')
        intf_type_pi_2 = pi_obj_2.get_physical_interface_type()
        self.assertEqual(intf_type_pi_2, 'access')
        vpg_obj.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj)

        # Now, remove one of the local_link_information
        binding_profile = {'local_link_information': [
            {'port_id': pi_fq_name_1[2],
             'switch_id': pi_fq_name_1[2],
             'fabric': fabric_name[-1],
             'switch_info': pi_fq_name_1[1]}]}

        kv_pairs = KeyValuePairs(
            [KeyValuePair(key='vpg', value=vpg_name[-1]),
             KeyValuePair(key='vif_type', value='vrouter'),
             KeyValuePair(key='vnic_type', value='baremetal'),
             KeyValuePair(key='profile',
                          value=json.dumps(binding_profile))])

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)
        self.api.virtual_machine_interface_update(vmi_obj)

        # Read physical interface type again, pi_2's should be set to None
        pi_obj_1 = self._vnc_lib.physical_interface_read(id=pi_uuid_1)
        pi_obj_2 = self._vnc_lib.physical_interface_read(id=pi_uuid_2)
        intf_type_pi_1 = pi_obj_1.get_physical_interface_type()
        self.assertEqual(intf_type_pi_1, 'access')
        intf_type_pi_2 = pi_obj_2.get_physical_interface_type()
        self.assertEqual(intf_type_pi_2, None)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        # Read physical interface type again, it should be set to None
        pi_obj_1 = self._vnc_lib.physical_interface_read(id=pi_uuid_1)
        pi_obj_2 = self._vnc_lib.physical_interface_read(id=pi_uuid_2)
        intf_type_pi_1 = pi_obj_1.get_physical_interface_type()
        self.assertEqual(intf_type_pi_1, None)
        intf_type_pi_2 = pi_obj_2.get_physical_interface_type()
        self.assertEqual(intf_type_pi_2, None)
        self.api.physical_interface_delete(id=pi_uuid_1)
        self.api.physical_interface_delete(id=pi_uuid_2)
        self.api.physical_router_delete(id=pr_obj_1.uuid)
        self.api.physical_router_delete(id=pr_obj_2.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def create_vpg_prerequisite_objects(self, _vpg_name, _vn_name,
                                        _fabric_obj, _proj_obj, vmi_idx,
                                        pi_1_fq_name, pi_2_fq_name):

        # Create VPG for positive case
        vpg = VirtualPortGroup(_vpg_name, parent_obj=_fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_fq_name = vpg_obj.get_fq_name()

        vn_obj = VirtualNetwork('%s-%s' % (_vn_name,
                                self.id()), parent_obj=_proj_obj)

        vmi_obj = VirtualMachineInterface(self.id() + vmi_idx,
                                          parent_obj=_proj_obj)
        vmi_obj.set_virtual_network(vn_obj)
        fabric_name = _fabric_obj.get_fq_name()
        self.api.virtual_network_create(vn_obj)

        binding_profile = {'local_link_information': [{
                           'port_id': pi_1_fq_name[2],
                           'switch_id': pi_1_fq_name[2],
                           'fabric': fabric_name[-1],
                           'switch_info': pi_1_fq_name[1]},
                          {'port_id': pi_2_fq_name[2],
                           'switch_id': pi_2_fq_name[2],
                           'fabric': fabric_name[-1],
                           'switch_info': pi_2_fq_name[1]}]}

        kv_pairs = KeyValuePairs(
            [KeyValuePair(key='vpg', value=vpg_fq_name[-1]),
             KeyValuePair(key='vif_type', value='vrouter'),
             KeyValuePair(key='vnic_type', value='baremetal'),
             KeyValuePair(key='profile',
                          value=json.dumps(binding_profile))])

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))

        return vmi_obj, vn_obj, vpg_obj

    def test_vmi_create_invalid_missing_pi(self):

        # positive case: pi is created and is not missing
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            enterprise_style_flag=False, create_second_pr=True)

        pr_obj_1 = pr_objs[0]
        pr_obj_2 = pr_objs[1]
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi_1 = PhysicalInterface(name=pi_name,
                                 parent_obj=pr_obj_1,
                                 ethernet_segment_identifier=esi_id)
        pi_uuid_1 = self._vnc_lib.physical_interface_create(pi_1)
        pi_obj_1 = self._vnc_lib.physical_interface_read(id=pi_uuid_1)

        pi_fq_name_1 = pi_obj_1.get_fq_name()

        # create second pi
        pi_name = self.id() + '_physical_interface2'
        pi_2 = PhysicalInterface(name=pi_name,
                                 parent_obj=pr_obj_2,
                                 ethernet_segment_identifier=esi_id)
        pi_uuid_2 = self._vnc_lib.physical_interface_create(pi_2)
        pi_obj_2 = self._vnc_lib.physical_interface_read(id=pi_uuid_2)

        pi_fq_name_2 = pi_obj_2.get_fq_name()

        # create vpg for positive case
        vpg_name_positive = "vpg-positive"
        vmi_pos, vn_pos, vpg_pos = self.create_vpg_prerequisite_objects(
            vpg_name_positive, 'vn1-%s' % (self.id()),
            fabric_obj, proj_obj, "1", pi_fq_name_1, pi_fq_name_2
        )

        # case of succesful creation and reading
        self.api.virtual_machine_interface_create(vmi_pos)
        vmi_uuid = vmi_pos.get_uuid()
        self.api.virtual_machine_interface_read(id=vmi_uuid)
        self._vnc_lib.physical_interface_delete(id=pi_uuid_2)
        self._vnc_lib.physical_interface_delete(id=pi_uuid_1)
        self.api.virtual_machine_interface_delete(id=vmi_uuid)
        self.api.virtual_port_group_delete(id=vpg_pos.uuid)

        # Create a Second VPG which will have missing VPGs
        vpg_name_missing_pi = "vpg-missing_pi"
        vmi_neg, vn_neg, vpg_neg = self.create_vpg_prerequisite_objects(
            vpg_name_missing_pi, 'vn2-%s' % (self.id()),
            fabric_obj, proj_obj, "2", pi_fq_name_1, pi_fq_name_2
        )

        # now creation of second vmi would lead to a BadRequest Error
        with ExpectedException(NoIdError):
            self.api.virtual_machine_interface_create(vmi_neg)

        # Delete all created objects
        self.api.physical_router_delete(id=pr_obj_1.uuid)
        self.api.physical_router_delete(id=pr_obj_2.uuid)
        self.api.virtual_port_group_delete(id=vpg_neg.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_ae_id_deallocated_for_vpg_multihoming_interfaces(self):
        mock_zk = self._api_server._db_conn._zk_db
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            enterprise_style_flag=False, create_second_pr=True)

        pr_obj_1 = pr_objs[0]
        pr_obj_2 = pr_objs[1]

        # Create first PI
        pi_name = self.id() + '_physical_interface1'
        pi_1 = PhysicalInterface(name=pi_name, parent_obj=pr_obj_1)
        pi_uuid_1 = self._vnc_lib.physical_interface_create(pi_1)
        pi_obj_1 = self._vnc_lib.physical_interface_read(id=pi_uuid_1)
        pi_fq_name_1 = pi_obj_1.get_fq_name()

        # Create second PI
        pi_name = self.id() + '_physical_interface2'
        pi_2 = PhysicalInterface(name=pi_name, parent_obj=pr_obj_2)
        pi_uuid_2 = self._vnc_lib.physical_interface_create(pi_2)
        pi_obj_2 = self._vnc_lib.physical_interface_read(id=pi_uuid_2)
        pi_fq_name_2 = pi_obj_2.get_fq_name()

        # Create VPG
        vpg_name = "vpg-1"
        fabric_name = fabric_obj.get_fq_name()
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_fq_name = vpg_obj.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)
        vn1_obj = self.api.virtual_network_read(id=vn1.uuid)
        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1", parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1_obj)

        # Populate binding profile to be used in VMI create
        binding_profile = {'local_link_information': [
            {'port_id': pi_fq_name_1[2],
             'switch_id': pi_fq_name_1[2],
             'fabric': fabric_name[-1],
             'switch_info': pi_fq_name_1[1]},
            {'port_id': pi_fq_name_2[2],
             'switch_id': pi_fq_name_2[2],
             'fabric': fabric_name[-1],
             'switch_info': pi_fq_name_2[1]}]}

        kv_pairs = KeyValuePairs(
            [KeyValuePair(key='vpg', value=vpg_fq_name[-1]),
             KeyValuePair(key='vif_type', value='vrouter'),
             KeyValuePair(key='vnic_type', value='baremetal'),
             KeyValuePair(key='profile',
                          value=json.dumps(binding_profile))])

        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)
        self.api.virtual_machine_interface_create(vmi_obj)

        vpg_obj.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj)
        vpg_boj = self.api.virtual_port_group_read(id=vpg_obj.uuid)

        # get phy_rtr_name and ae_id pairs
        pi_refs = vpg_boj.physical_interface_refs
        pr_ae_id_pairs = []
        for pi_ref in pi_refs:
            ae_id = pi_ref['attr'].__dict__.get('ae_num')
            pi_fq_name = pi_ref['to']
            pi = self.api.physical_interface_read(fq_name=pi_fq_name)
            pr = self.api.physical_router_read(id=pi.parent_uuid)
            pr_ae_id_pairs.append((pr.name, ae_id))

        # test ae-id is allocated
        for phy_rtr_name, ae_id in pr_ae_id_pairs:
            self.assertTrue(mock_zk.ae_id_is_occupied(phy_rtr_name, ae_id))

        # delete VPG
        self.api.virtual_machine_interface_delete(id=vmi_obj.uuid)
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)

        # test ae-id is deallocated
        for phy_rtr_name, ae_id in pr_ae_id_pairs:
            self.assertFalse(mock_zk.ae_id_is_occupied(phy_rtr_name, ae_id))

        # cleanup
        self.api.physical_interface_delete(id=pi_uuid_1)
        self.api.physical_interface_delete(id=pi_uuid_2)
        self.api.physical_router_delete(id=pr_obj_1.uuid)
        self.api.physical_router_delete(id=pr_obj_2.uuid)
        self.api.virtual_network_read(id=vn1_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    # Using VPG update logic
    def test_same_pi_can_not_attach_to_different_vpg(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()
        test_id = self.id()

        pi_per_pr = 4
        pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                    i in range(1, pi_per_pr + 1)]
        pi_objs = self._create_pi_objects(pr_obj, pi_names)

        vpg_count = 2
        vpg_names = ['vpg_%s_%s' % (test_id, i) for i in range(
            1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # Add all PIs to VPG-1
        vpg_obj = vpg_objs[vpg_names[0]]
        for pi_name in pi_names:
            vpg_obj.add_physical_interface(pi_objs[pi_name])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        self.assertEqual(len(pi_refs), 4)

        # Try Add PI1, PI2 to VPG-2
        vpg_obj = vpg_objs[vpg_names[1]]
        vpg_obj.add_physical_interface(pi_objs[pi_names[0]])
        vpg_obj.add_physical_interface(pi_objs[pi_names[1]])
        with ExpectedException(BadRequest):
            self.api.virtual_port_group_update(vpg_obj)

        # Delete PI1, PI2 from VPG-1
        vpg_obj = vpg_objs[vpg_names[0]]
        for pi_name in pi_names[:2]:
            vpg_obj.del_physical_interface(pi_objs[pi_name])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        self.assertEqual(len(pi_refs), 2)

        # Now try attach PI1, PI2 to VPG-2,
        # should work as PI1, PI2 are no longer attached to VPG1
        vpg_obj = vpg_objs[vpg_names[1]]
        vpg_obj.add_physical_interface(pi_objs[pi_names[0]])
        vpg_obj.add_physical_interface(pi_objs[pi_names[1]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        self.assertEqual(len(pi_refs), 2)

        # cleanup
        for _, vpg_obj in vpg_objs.items():
            self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        for _, pi_obj in pi_objs.items():
            self.api.physical_interface_delete(id=pi_obj.uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)
        self.api.project_delete(id=proj_obj.uuid)

    # To verify allocation and dellocation - through VMI
    def test_ae_id_alloc_dealloc(self):
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id().split('.')[-1]
        fabric_name = fabric_obj.get_fq_name()
        vlan_vn_count = 3

        def process_ae_ids(x):
            return [int(i) for i in sorted(x)]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        vlan_ids = range(1, vlan_vn_count + 1)
        vn_names = ['vn_%s_%s' % (test_id, i)
                    for i in range(1, vlan_vn_count + 1)]
        vn_objs = self._create_vns(proj_obj, vn_names)

        vmi_objs = {}
        # create four PIs, two on each PR
        pi_objs = {}
        pi_per_pr = 1
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr2_pi_names = ['%s_pr2_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr2_pi_objs = self._create_pi_objects(pr_objs[1], pr2_pi_names)
        pi_objs.update(pr1_pi_objs)
        pi_objs.update(pr2_pi_objs)

        # create VPGs
        vpg_count = 2
        vpg_names = ['vpg_%s_%s' % (test_id, i)
                     for i in range(1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        # Case 1: Attach PI-1/PR-1 and PI-1/PR-2 to VPG-1
        # one AE-ID i.e 0 to be allocated to VPG-1
        vpg_index = 0
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_pi_fqs = [pi.get_fq_name() for _, pi in pi_objs.items()]
        vmi_infos = []
        vmi_vn_vlan_map = defaultdict(list)
        for vmi_id in range(3):
            vmi_name = 'vmi_%s_%s' % (test_id, vmi_id)
            info = {
                'name': vmi_name,
                'vmi_id': '%s' % vmi_id,
                'parent_obj': proj_obj,
                'vn': vn_objs[vn_names[vmi_id - 1]],
                'vpg': vpg_obj.uuid,
                'fabric': fabric_name,
                'pis': vpg_pi_fqs,
                'vlan': vlan_ids[vmi_id - 1],
                'is_untagged': False}
            vmi_infos.append(info)
            vmi_vn_vlan_map[vmi_name] = (
                vn_objs[vn_names[vmi_id - 1]], vlan_ids[vmi_id - 1])
        vmi_objs = self._create_vmis(vmi_infos)
        for vpg, vpg_o in vpg_objs.items():
            vpg_objs[vpg] = self.api.virtual_port_group_read(
                id=vpg_o.uuid)

        # record AE-IDs allocated for each prouter
        ae_ids = {}
        pi_refs = vpg_objs[vpg_name].get_physical_interface_refs()
        self.assertEqual(len(pi_refs), len(vpg_pi_fqs))
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())),
                         len(vpg_pi_fqs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        self.assertEqual(list(ae_ids[vpg_name].values()), [0, 0])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0])

        # Case 2: Attach only one PI to VPG-1
        vpg_index = 0
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_pi_fqs = [pr1_pi_objs[pr1_pi_names[0]].get_fq_name()]
        vmi_infos = []
        vmi_names = list(vmi_objs.keys())
        for vmi_id in range(3):
            info = {
                'name': vmi_names[vmi_id],
                'vmi_uuid': '%s' % vmi_objs[vmi_names[vmi_id]].uuid,
                'vn': vmi_vn_vlan_map[vmi_names[vmi_id]][0],
                'vpg': vpg_obj.uuid,
                'fabric': fabric_name,
                'pis': vpg_pi_fqs,
                'vlan': vmi_vn_vlan_map[vmi_names[vmi_id]][1],
                'is_untagged': False}
            vmi_infos.append(info)
        vmi_objs = self._update_vmis(vmi_infos)
        for vpg, vpg_o in vpg_objs.items():
            vpg_objs[vpg] = self.api.virtual_port_group_read(
                id=vpg_o.uuid)

        # record AE-IDs allocated for each prouter
        ae_ids = {}
        pi_refs = vpg_objs[vpg_name].get_physical_interface_refs()
        self.assertEqual(len(pi_refs), len(vpg_pi_fqs))
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())),
                         len(vpg_pi_fqs))
        self.assertIsNone(list(ae_ids[vpg_name].values())[0])
        self.assertEqual(list(ae_ids[vpg_name].values()), [None])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [])
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])

        # Case-3: Add All PIs back to VPG and update VMIs
        # ensure AE-IDs are allocated back
        vpg_index = 0
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_pi_fqs = [pi.get_fq_name() for _, pi in pi_objs.items()]
        vmi_infos = []
        vmi_names = list(vmi_objs.keys())
        for vmi_id in range(3):
            info = {
                'name': vmi_names[vmi_id],
                'vmi_uuid': '%s' % vmi_objs[vmi_names[vmi_id]].uuid,
                'vn': vmi_vn_vlan_map[vmi_names[vmi_id]][0],
                'vpg': vpg_obj.uuid,
                'fabric': fabric_name,
                'pis': vpg_pi_fqs,
                'vlan': vmi_vn_vlan_map[vmi_names[vmi_id]][1],
                'is_untagged': False}
            vmi_infos.append(info)
        vmi_objs = self._update_vmis(vmi_infos)
        for vpg, vpg_o in vpg_objs.items():
            vpg_objs[vpg] = self.api.virtual_port_group_read(
                id=vpg_o.uuid)

        # record AE-IDs allocated for each prouter
        ae_ids = {}
        pi_refs = vpg_objs[vpg_name].get_physical_interface_refs()
        self.assertEqual(len(pi_refs), len(vpg_pi_fqs))
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())),
                         len(vpg_pi_fqs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        self.assertEqual(list(ae_ids[vpg_name].values()), [0, 0])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0])

        # Case-4: Delete two VMIs
        # Ensure all AE-IDs are deallocated
        for _, vmi_obj in list(vmi_objs.items())[0:2]:
            self.api.virtual_machine_interface_delete(id=vmi_obj.uuid)

        # record AE-IDs allocated for each prouter
        ae_ids = {}
        pi_refs = vpg_objs[vpg_name].get_physical_interface_refs()
        self.assertEqual(len(pi_refs), len(vpg_pi_fqs))
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())),
                         len(vpg_pi_fqs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        self.assertEqual(list(ae_ids[vpg_name].values()), [0, 0])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0])

        # Case 5 : Delete VPG-1
        vpg_index = 0
        vpg_obj = vpg_objs[vpg_names[vpg_index]]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        for vmi_ref in vpg_obj.get_virtual_machine_interface_refs():
            self.api.virtual_machine_interface_delete(id=vmi_ref['uuid'])
        del vpg_objs[vpg_names[0]]
        # Now delete VPG-1
        self.api.virtual_port_group_delete(id=vpg_obj.uuid)
        # Verification at Physical Routers
        with ExpectedException(NoIdError):
            self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 0)
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])

        # cleanup
        # remove VMIs
        # already cleared during Case 5
        for _, vmi_obj in vmi_objs.items():
            try:
                self.api.virtual_machine_interface_delete(id=vmi_obj.uuid)
            except NoIdError:
                pass
        # remove VNs
        for _, vn_obj in vn_objs.items():
            self.api.virtual_network_delete(id=vn_obj.uuid)
        # remove VPGs
        for _, vpg_obj in vpg_objs.items():
            try:
                self.api.virtual_port_group_delete(id=vpg_obj.uuid)
            except NoIdError:
                pass
        # remove PIs
        for _, pi_obj in pi_objs.items():
            self.api.physical_interface_delete(id=pi_obj.uuid)
        # remove PR
        for pr_obj in pr_objs:
            self.api.physical_router_delete(id=pr_obj.uuid)
        # remove fabric
        self.api.fabric_delete(id=fabric_obj.uuid)
        # remove project
        self.api.project_delete(id=proj_obj.uuid)

    def test_two_virtual_port_groups_for_single_pi(self):
        # Similar to e.g. test_same_vn_with_same_vlan_across_vpg_in_enterprise,
        # but one of VPG's has no name and we try to bind one PI to both VPGs
        # Such scenario should fail. PI can be attached to only one VPG
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()

        # Create Physical Interface
        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        fabric_name = fabric_obj.get_fq_name()
        pi_fq_name = pi_obj.get_fq_name()

        # Create VPG-1
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj_1 = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name_1 = vpg_obj_1.get_fq_name()

        # Create VPG-2 with no name
        vpg = VirtualPortGroup(parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj_2 = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name_2 = vpg_obj_2.get_fq_name()

        # Create single VN
        vn1 = VirtualNetwork('vn1-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn1)

        # Create a VMI that's attached to vpg-1 and having reference
        # to vn1
        vmi_obj = VirtualMachineInterface(self.id() + "1",
                                          parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn1)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name_1)
        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=42))
        vmi_uuid_1 = self.api.virtual_machine_interface_create(vmi_obj)
        vpg_obj_1.add_virtual_machine_interface(vmi_obj)
        self.api.virtual_port_group_update(vpg_obj_1)

        # Create single VN for second VMI
        vn2 = VirtualNetwork('vn2-%s' % (self.id()), parent_obj=proj_obj)
        self.api.virtual_network_create(vn2)
        # Create a VMI that's attached to vpg-2 and having reference
        # to vn2
        vmi_obj_2 = VirtualMachineInterface(self.id() + "2",
                                            parent_obj=proj_obj)
        vmi_obj_2.set_virtual_network(vn2)

        # Create KV_Pairs for this VMI
        kv_pairs = self._create_kv_pairs(pi_fq_name,
                                         fabric_name,
                                         vpg_name_2)
        vmi_obj_2.set_virtual_machine_interface_bindings(kv_pairs)

        vmi_obj_2.set_virtual_machine_interface_properties(
            VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=43))

        with ExpectedException(BadRequest):
            self.api.virtual_machine_interface_create(vmi_obj_2)

        self.api.virtual_machine_interface_delete(id=vmi_uuid_1)
        self.api.virtual_port_group_delete(id=vpg_obj_1.uuid)
        self.api.virtual_port_group_delete(id=vpg_obj_2.uuid)
        self.api.physical_interface_delete(id=pi_uuid)
        self.api.physical_router_delete(id=pr_obj.uuid)
        self.api.fabric_delete(id=fabric_obj.uuid)

    def test_add_non_existing_pi_to_vpg(self):
        proj_obj, fabric_obj, pr_obj = self._create_prerequisites()

        esi_id = '00:11:22:33:44:55:66:77:88:99'
        pi_name = self.id() + '_physical_interface1'
        pi = PhysicalInterface(name=pi_name,
                               parent_obj=pr_obj,
                               ethernet_segment_identifier=esi_id)
        pi_uuid = self._vnc_lib.physical_interface_create(pi)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuid)

        # create VPG-1
        vpg_name = "vpg-1"
        vpg = VirtualPortGroup(vpg_name, parent_obj=fabric_obj)
        vpg_uuid = self.api.virtual_port_group_create(vpg)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_uuid)
        vpg_name = vpg_obj.get_fq_name()

        # Delete PI-1
        self.api.physical_interface_delete(id=pi_uuid)
        # Now add and update the non-existing PI to VPG
        # This should fail
        vpg_obj.add_physical_interface(pi_obj)
        with ExpectedException(NoIdError):
            self.api.virtual_port_group_update(vpg_obj)

    def test_add_pis_simultaneously_to_vpg(self):
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()
        VPG_CLASS = self._api_server.get_resource_class('virtual-port-group')
        org_process_ae_id = VPG_CLASS._process_ae_id

        class MockVpg(VPG_CLASS):
            org_process_ae_id = VPG_CLASS._process_ae_id
            HOLD_API = True
            @classmethod
            def mock_process_ae_id(cls, db_obj_dict, vpg_name, obj_dict=None):
                while cls.HOLD_API:
                    print('sleeping for HOLD_API to clear for '
                          'args = %s' % obj_dict)
                    gevent.sleep(0.5)
                return cls.org_process_ae_id(db_obj_dict, vpg_name, obj_dict)

        def process_ae_ids(x):
            return [int(i) for i in sorted(x) if i is not None]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 8
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr2_pi_names = ['%s_pr2_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr2_pi_objs = self._create_pi_objects(pr_objs[1], pr2_pi_names)
        pi_objs.update(pr1_pi_objs)
        pi_objs.update(pr2_pi_objs)

        # create a VPG
        vpg_count = 1
        vpg_names = ['vpg_%s_%s' % (test_id, i) for i in range(
                     1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        def _attach_pi_simultaneously(vpg_obj, pi_uuids):
            # Attach PIs from PR1 to VPG-1
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            try:
                # mock _process_ae_id at VPG resource
                VPG_CLASS._process_ae_id = MockVpg.mock_process_ae_id
                MockVpg.HOLD_API = True
                for pi_uuid in pi_uuids:
                    gevent.spawn(
                        self.api.ref_update,
                        "virtual-port-group",
                        vpg_obj.uuid,
                        "physical-interface",
                        pi_uuid,
                        None,
                        "ADD",
                        None)
                gevent.sleep(2)
                MockVpg.HOLD_API = False
                gevent.sleep(3)
            except gevent.timeout.Timeout:
                self.assertFalse(
                    False,
                    '%s failed unexpectedly' % VPG_CLASS._process_ae_id)
            finally:
                # reset mock to original
                VPG_CLASS._process_ae_id = org_process_ae_id
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            pi_refs = vpg_obj.get_physical_interface_refs()
            return vpg_obj, pi_refs

        # Case 1
        # Attach 2 PIs from PR1 to VPG-1
        ae_ids = {}
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_uuids = [pi_objs[pr1_pi_names[pi]].uuid for pi in range(2)]
        vpg_obj, pi_refs = _attach_pi_simultaneously(vpg_obj, pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        ae_id_sorted = process_ae_ids(ae_ids[vpg_name].values())
        self.assertEqual(ae_id_sorted, [0] * 2)

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])

        # Case 2
        # Attach 2 more PIs from PR1 to VPG-1
        ae_ids = {}
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_uuids = [pi_objs[pr1_pi_names[pi]].uuid for pi in range(2, 4)]
        vpg_obj, pi_refs = _attach_pi_simultaneously(vpg_obj, pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 4)
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        ae_id_sorted = process_ae_ids(ae_ids[vpg_name].values())
        self.assertEqual(ae_id_sorted, [0] * 4)

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])

        # Case 3
        # Attach 2 more PIs from PR1 to VPG-1
        ae_ids = {}
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_uuids = [pi_objs[pr1_pi_names[pi]].uuid for pi in range(4, 6)]
        vpg_obj, pi_refs = _attach_pi_simultaneously(vpg_obj, pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 6)
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        #  verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        ae_id_sorted = process_ae_ids(ae_ids[vpg_name].values())
        self.assertEqual(ae_id_sorted, [0] * 6)

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])

        # Case 4
        # Attach 2 more PIs from PR1 to VPG-1
        ae_ids = {}
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_uuids = [pi_objs[pr1_pi_names[pi]].uuid for pi in range(6, 8)]
        vpg_obj, pi_refs = _attach_pi_simultaneously(vpg_obj, pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 8)
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        ae_id_sorted = process_ae_ids(ae_ids[vpg_name].values())
        self.assertEqual(ae_id_sorted, [0] * 8)

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])

    @skip("CEM-17571: Inconsistent, needs tuning gevent sleep duration")
    def test_add_multiple_pis_simultaneously_to_vpg_with_1_pi(self):
        """Verify Same AE-ID for simultaneous PI attached to same VPG.

        Case 1: Create a VPG with 1 PI/PR1, No AE-ID is allocated
        Case 2: Attach 149 PI/PR1, Only 1 AE-ID allocated to all PIs
        """
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()
        VPG_CLASS = self._api_server.get_resource_class('virtual-port-group')
        org_process_ae_id = VPG_CLASS._process_ae_id

        class MockVpg(VPG_CLASS):
            org_process_ae_id = VPG_CLASS._process_ae_id
            HOLD_API = True
            @classmethod
            def mock_process_ae_id(cls, db_obj_dict, vpg_name, obj_dict=None):
                while cls.HOLD_API:
                    print('sleeping for HOLD_API to clear for '
                          'args = %s' % obj_dict)
                    gevent.sleep(0.5)
                return cls.org_process_ae_id(db_obj_dict, vpg_name, obj_dict)

        def process_ae_ids(x):
            return [int(i) for i in sorted(x) if i is not None]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 150
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr2_pi_names = ['%s_pr2_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr2_pi_objs = self._create_pi_objects(pr_objs[1], pr2_pi_names)
        pi_objs.update(pr1_pi_objs)
        pi_objs.update(pr2_pi_objs)

        # create a VPG
        vpg_count = 1
        vpg_names = ['vpg_%s_%s' % (test_id, i) for i in range(
                     1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        def _attach_pi_simultaneously(vpg_obj, pi_uuids):
            # Attach PIs from PR1 to VPG-1
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            try:
                # mock _process_ae_id at VPG resource
                VPG_CLASS._process_ae_id = MockVpg.mock_process_ae_id
                MockVpg.HOLD_API = True
                for pi_uuid in pi_uuids:
                    gevent.spawn(
                        self.api.ref_update,
                        "virtual-port-group",
                        vpg_obj.uuid,
                        "physical-interface",
                        pi_uuid,
                        None,
                        "ADD",
                        None)
                gevent.sleep(2)
                MockVpg.HOLD_API = False
                gevent.sleep(6)
            except gevent.timeout.Timeout:
                self.assertFalse(
                    False,
                    '%s failed unexpectedly' % VPG_CLASS._process_ae_id)
            finally:
                # reset mock to original
                VPG_CLASS._process_ae_id = org_process_ae_id
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            pi_refs = vpg_obj.get_physical_interface_refs()
            return vpg_obj, pi_refs

        # Case 1
        # Attach 1 PIs from PR1 to VPG-1
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_uuids = [pi_objs[pr1_pi_names[pi]].uuid for pi in range(1)]
        vpg_obj, pi_refs = _attach_pi_simultaneously(vpg_obj, pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 1)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertIsNone(list(vpg_ae_ids.values())[0])
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertEqual(ae_id_sorted, [])
        # verification at ZK for AE-IDs in Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [])

        # Case 2
        # Attach rest of 149 PIs from PR1 to VPG-1
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_uuids = [pi_objs[pr1_pi_names[pi]].uuid for pi in range(1, 150)]
        vpg_obj, pi_refs = _attach_pi_simultaneously(vpg_obj, pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 150)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertEqual(len(set(vpg_ae_ids.values())), 1)
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertEqual(ae_id_sorted, [0] * 150)
        # verification at ZK for AE-IDs in Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])

    def test_add_multiple_pis_simultaneously_to_vpg_check_reallocation(self):
        """Verify AE-ID re-allocated is unique.

        Case 1: Create a VPG-1 with 2 PIs from PR1, ae=0 allocated
        Case 2: Create another VPG-2 with 2 more PIs from PR1, ae=1 allocated
        Case 3: Deattach 1 PI from VPG-1, so ae=0 is deallocated
        Case 4: Create another VPG-3 with 2 PIs from PR1, ae=0 allocated
        Case 5: Add two PIs to VPG-1 again, ae=2 allocated
        """
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()
        VPG_CLASS = self._api_server.get_resource_class('virtual-port-group')
        org_process_ae_id = VPG_CLASS._process_ae_id

        class MockVpg(VPG_CLASS):
            org_process_ae_id = VPG_CLASS._process_ae_id
            HOLD_API = True
            @classmethod
            def mock_process_ae_id(cls, db_obj_dict, vpg_name, obj_dict=None):
                while cls.HOLD_API:
                    print('sleeping for HOLD_API to clear for '
                          'args = %s' % obj_dict)
                    gevent.sleep(0.5)
                return cls.org_process_ae_id(db_obj_dict, vpg_name, obj_dict)

        def process_ae_ids(x):
            return [int(i) for i in sorted(x) if i is not None]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 6
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr2_pi_names = ['%s_pr2_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr2_pi_objs = self._create_pi_objects(pr_objs[1], pr2_pi_names)
        pi_objs.update(pr1_pi_objs)
        pi_objs.update(pr2_pi_objs)

        # create a VPG
        vpg_count = 3
        vpg_names = ['vpg_%s_%s' % (test_id, i) for i in range(
                     1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        def _attach_pi_simultaneously(vpg_obj, pi_uuids):
            # Attach PIs from PR1 to VPG-1
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            try:
                # mock _process_ae_id at VPG resource
                VPG_CLASS._process_ae_id = MockVpg.mock_process_ae_id
                MockVpg.HOLD_API = True
                for pi_uuid in pi_uuids:
                    gevent.spawn(
                        self.api.ref_update,
                        "virtual-port-group",
                        vpg_obj.uuid,
                        "physical-interface",
                        pi_uuid,
                        None,
                        "ADD",
                        None)
                gevent.sleep(2)
                MockVpg.HOLD_API = False
                gevent.sleep(3)
            except gevent.timeout.Timeout:
                self.assertFalse(
                    False,
                    '%s failed unexpectedly' % VPG_CLASS._process_ae_id)
            finally:
                # reset mock to original
                VPG_CLASS._process_ae_id = org_process_ae_id
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            pi_refs = vpg_obj.get_physical_interface_refs()
            return vpg_obj, pi_refs

        # Case 1
        # Attach 2 PIs from PR1 to VPG-1
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_uuids = [pi.uuid for pi in list(pr1_pi_objs.values())[0:2]]
        vpg_obj, pi_refs = _attach_pi_simultaneously(vpg_obj, pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertEqual(len(list(vpg_ae_ids.values())), 2)
        self.assertEqual(len(set(vpg_ae_ids.values())), 1)
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertEqual(ae_id_sorted, [0, 0])
        # verification at ZK for AE-IDs in Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])

        # Case 2
        # Attach 2 PIs from PR1 to VPG-2
        vpg_name = vpg_names[1]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_uuids = [pi.uuid for pi in list(pr1_pi_objs.values())[2:4]]
        vpg_obj, pi_refs = _attach_pi_simultaneously(vpg_obj, pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertEqual(len(list(vpg_ae_ids.values())), 2)
        self.assertEqual(len(set(vpg_ae_ids.values())), 1)
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertEqual(ae_id_sorted, [1, 1])
        # verification at ZK for AE-IDs in Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 2)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0, 1])
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])

        # Case 3
        # Deattach 1 PIs from PR1 to VPG-1
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_obj = list(pr1_pi_objs.values())[0]
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_obj.uuid)
        vpg_obj.del_physical_interface(pi_obj)
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 1)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertIsNone(list(vpg_ae_ids.values())[0])
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertEqual(ae_id_sorted, [])
        # verification at ZK for AE-IDs in Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [1])
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])

        # Case 4
        # Attach 2 PIs from PR1 to VPG-3
        vpg_name = vpg_names[2]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_uuids = [pi.uuid for pi in list(pr1_pi_objs.values())[4:6]]
        vpg_obj, pi_refs = _attach_pi_simultaneously(vpg_obj, pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertEqual(len(list(vpg_ae_ids.values())), 2)
        self.assertEqual(len(set(vpg_ae_ids.values())), 1)
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertEqual(ae_id_sorted, [0, 0])
        # verification at ZK for AE-IDs in Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 2)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0, 1])
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])

        # Case 5
        # Attach 1 PIs from PR1 to VPG-1
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_obj = list(pr1_pi_objs.values())[0]
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_obj.uuid)
        vpg_obj.add_physical_interface(pi_obj)
        self._vnc_lib.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertEqual(len(list(vpg_ae_ids.values())), 2)
        self.assertEqual(len(set(vpg_ae_ids.values())), 1)
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertEqual(ae_id_sorted, [2, 2])
        # verification at ZK for AE-IDs in Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 3)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0, 1, 2])
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])

    def test_add_multiple_pis_simultaneously_to_vpg_check_deallocation(self):
        """Verify AE-ID de-allocated in both PRs.

        Case 1: Create VPG-1 wtih PI1/PR1, PI1/PR2, ae=0 allocated at both PRs
        Case 2: Deattach PI1/PR1 from VPG-1, so ae=0 is deallocated from PR1
        Case 3: Ensure ae=0 from PR2 is also removed at ZK
        """
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()
        VPG_CLASS = self._api_server.get_resource_class('virtual-port-group')
        org_process_ae_id = VPG_CLASS._process_ae_id

        class MockVpg(VPG_CLASS):
            org_process_ae_id = VPG_CLASS._process_ae_id
            HOLD_API = True
            @classmethod
            def mock_process_ae_id(cls, db_obj_dict, vpg_name, obj_dict=None):
                while cls.HOLD_API:
                    print('sleeping for HOLD_API to clear for '
                          'args = %s' % obj_dict)
                    gevent.sleep(0.5)
                return cls.org_process_ae_id(db_obj_dict, vpg_name, obj_dict)

        def process_ae_ids(x):
            return [int(i) for i in sorted(x) if i is not None]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 1
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr2_pi_names = ['%s_pr2_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr2_pi_objs = self._create_pi_objects(pr_objs[1], pr2_pi_names)
        pi_objs.update(pr1_pi_objs)
        pi_objs.update(pr2_pi_objs)

        # create a VPG
        vpg_count = 1
        vpg_names = ['vpg_%s_%s' % (test_id, i) for i in range(
                     1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        def _attach_pi_simultaneously(vpg_obj, pi_uuids):
            # Attach PIs from PR1 to VPG-1
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            try:
                # mock _process_ae_id at VPG resource
                VPG_CLASS._process_ae_id = MockVpg.mock_process_ae_id
                MockVpg.HOLD_API = True
                for pi_uuid in pi_uuids:
                    gevent.spawn(
                        self.api.ref_update,
                        "virtual-port-group",
                        vpg_obj.uuid,
                        "physical-interface",
                        pi_uuid,
                        None,
                        "ADD",
                        None)
                gevent.sleep(2)
                MockVpg.HOLD_API = False
                gevent.sleep(3)
            except gevent.timeout.Timeout:
                self.assertFalse(
                    False,
                    '%s failed unexpectedly' % VPG_CLASS._process_ae_id)
            finally:
                # reset mock to original
                VPG_CLASS._process_ae_id = org_process_ae_id
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            pi_refs = vpg_obj.get_physical_interface_refs()
            return vpg_obj, pi_refs

        # Case 1
        # Attach 2 PIs from PR1 to VPG-1
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pr1_pi_uuids = list(pr1_pi_objs.values())[0].uuid
        pr2_pi_uuids = list(pr2_pi_objs.values())[0].uuid
        pi_uuids = [pr1_pi_uuids, pr2_pi_uuids]
        vpg_obj, pi_refs = _attach_pi_simultaneously(vpg_obj, pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertEqual(len(list(vpg_ae_ids.values())), 2)
        self.assertEqual(len(set(vpg_ae_ids.values())), 1)
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertEqual(ae_id_sorted, [0, 0])
        # verification at ZK for AE-IDs in Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0])

        # Case 2
        # Deattach 1 PIs from PR1 to VPG-1
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_obj = list(pr1_pi_objs.values())[0]
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_obj.uuid)
        vpg_obj.del_physical_interface(pi_obj)
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 1)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertIsNone(list(vpg_ae_ids.values())[0])
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertEqual(ae_id_sorted, [])
        # verification at ZK for AE-IDs in Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [])
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])

    @skip("CEM-17571: Skip this until we figure out the reason for flakiness")
    def test_add_delete_a_pi_simultaneously_to_vpg_with_1_pi(self):
        """Verify simultaneous attach PI and delete PI.

        # Case 1: Create a VPG with 1 PI1/PR1. No AE-ID allocated
        # Case 2: Add PI2/PR1 while deleting PI1/PR1 simultaneously.
        #         No AE-ID allocated
        """
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()
        VPG_CLASS = self._api_server.get_resource_class('virtual-port-group')
        org_process_ae_id = VPG_CLASS._process_ae_id

        class MockVpg(VPG_CLASS):
            org_process_ae_id = VPG_CLASS._process_ae_id
            HOLD_API = True
            @classmethod
            def mock_process_ae_id(cls, db_obj_dict, vpg_name, obj_dict=None):
                while cls.HOLD_API:
                    print('sleeping for HOLD_API to clear for '
                          'args = %s' % obj_dict)
                    gevent.sleep(0.5)
                return cls.org_process_ae_id(db_obj_dict, vpg_name, obj_dict)

        def process_ae_ids(x):
            return [int(i) for i in sorted(x) if i is not None]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 3
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pi_objs.update(pr1_pi_objs)

        # create a VPG
        vpg_count = 1
        vpg_names = ['vpg_%s_%s' % (test_id, i) for i in range(
                     1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        def _attach_pi_simultaneously(
                vpg_obj, create_pi_uuids, delete_pi_uuids=None):
            if delete_pi_uuids is None:
                delete_pi_uuids = []
            # Attach PIs from PR1 to VPG-1
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            try:
                # mock _process_ae_id at VPG resource
                VPG_CLASS._process_ae_id = MockVpg.mock_process_ae_id
                MockVpg.HOLD_API = True
                for pi_uuid in delete_pi_uuids:
                    gevent.spawn(
                        self.api.ref_update,
                        "virtual-port-group",
                        vpg_obj.uuid,
                        "physical-interface",
                        pi_uuid,
                        None,
                        "DELETE")
                for pi_uuid in create_pi_uuids:
                    gevent.spawn(
                        self.api.ref_update,
                        "virtual-port-group",
                        vpg_obj.uuid,
                        "physical-interface",
                        pi_uuid,
                        None,
                        "ADD",
                        None)
                gevent.sleep(2)
                MockVpg.HOLD_API = False
                gevent.sleep(2)
            except gevent.timeout.Timeout:
                self.assertFalse(
                    False,
                    '%s failed unexpectedly' % VPG_CLASS._process_ae_id)
            finally:
                # reset mock to original
                VPG_CLASS._process_ae_id = org_process_ae_id
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            pi_refs = vpg_obj.get_physical_interface_refs()
            return vpg_obj, pi_refs

        # Case 1
        # Attach PI-1/PR-1 to VPG-1
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_uuids = [list(pr1_pi_objs.values())[0].uuid]
        # vpg_obj, pi_refs = _attach_pi_simultaneously(vpg_obj, pi_uuids)
        pi_obj = self._vnc_lib.physical_interface_read(id=pi_uuids[0])
        vpg_obj.add_physical_interface(pi_obj)
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 1)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertIsNone(list(vpg_ae_ids.values())[0])
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertEqual(ae_id_sorted, [])
        # verification at ZK for AE-IDs in Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [])

        # Case 2
        # Attach PI-2 from PR1 to VPG-1 and delete exiting PI-1/PR-1
        # simultaneously
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        existing_pi_uuids = [ref['uuid'] for ref in pi_refs]
        pi_uuids = [list(pr1_pi_objs.values())[1].uuid]
        vpg_obj, pi_refs = _attach_pi_simultaneously(
            vpg_obj, pi_uuids, existing_pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 1)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertIsNone(list(vpg_ae_ids.values())[0])
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertEqual(ae_id_sorted, [])
        # verification at ZK for AE-IDs in Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [])

    def test_delete_pi_simultaneously_to_vpg_with_multiple_pi(self):
        """Verify AE-ID deallocated at PRs when leftover PIs exists.

        Case 1: Attach 3 PIs from each PR to VPG, one AE-ID allocated per PR
        Case 2: Delete 1 PI from each PR, AE-ID allocated remains
        Case 3: Detach all PIs from PR-1, AE-ID at PR-1 will be deallocated
        """
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()
        VPG_CLASS = self._api_server.get_resource_class('virtual-port-group')
        org_process_ae_id = VPG_CLASS._process_ae_id

        class MockVpg(VPG_CLASS):
            org_process_ae_id = VPG_CLASS._process_ae_id
            HOLD_API = True
            @classmethod
            def mock_process_ae_id(cls, db_obj_dict, vpg_name, obj_dict=None):
                while cls.HOLD_API:
                    print('sleeping for HOLD_API to clear for '
                          'args = %s' % obj_dict)
                    gevent.sleep(0.5)
                return cls.org_process_ae_id(db_obj_dict, vpg_name, obj_dict)

        def process_ae_ids(x):
            return [int(i) for i in sorted(x) if i is not None]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 3
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr2_pi_names = ['%s_pr2_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr2_pi_objs = self._create_pi_objects(pr_objs[1], pr2_pi_names)
        pi_objs.update(pr1_pi_objs)
        pi_objs.update(pr2_pi_objs)

        # create a VPG
        vpg_count = 1
        vpg_names = ['vpg_%s_%s' % (test_id, i) for i in range(
                     1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        def _attach_pi_simultaneously(
                vpg_obj, create_pi_uuids=None, delete_pi_uuids=None):
            if create_pi_uuids is None:
                create_pi_uuids = []
            if delete_pi_uuids is None:
                delete_pi_uuids = []
            # Attach PIs from PR1 to VPG-1
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            try:
                # mock _process_ae_id at VPG resource
                VPG_CLASS._process_ae_id = MockVpg.mock_process_ae_id
                # MockVpg.HOLD_API = True
                MockVpg.HOLD_API = False
                for pi_uuid in create_pi_uuids:
                    gevent.spawn(
                        self.api.ref_update,
                        "virtual-port-group",
                        vpg_obj.uuid,
                        "physical-interface",
                        pi_uuid,
                        None,
                        "ADD",
                        None)
                for pi_uuid in delete_pi_uuids:
                    gevent.spawn(
                        self.api.ref_update,
                        "virtual-port-group",
                        vpg_obj.uuid,
                        "physical-interface",
                        pi_uuid,
                        None,
                        "DELETE",
                        None)
                gevent.sleep(2)
                MockVpg.HOLD_API = False
                gevent.sleep(3)
            except gevent.timeout.Timeout:
                self.assertFalse(
                    False,
                    '%s failed unexpectedly' % VPG_CLASS._process_ae_id)
            finally:
                # reset mock to original
                VPG_CLASS._process_ae_id = org_process_ae_id
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            pi_refs = vpg_obj.get_physical_interface_refs()
            return vpg_obj, pi_refs

        # Case 1
        # Attach 3 PIs/PR1 and 3 PIs/PR2 to VPG1
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pr1_pi_uuids = [pi_objs[pr1_pi_names[pi]].uuid for pi in range(3)]
        pr2_pi_uuids = [pi_objs[pr2_pi_names[pi]].uuid for pi in range(3)]
        pi_uuids = pr1_pi_uuids + pr2_pi_uuids
        vpg_obj, pi_refs = _attach_pi_simultaneously(vpg_obj, pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 6)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertEqual(len(vpg_ae_ids.values()), 6)
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertEqual(ae_id_sorted, [0] * 6)
        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0])

        # Case 2
        # Deattach PI-1/PR-1, PI-1/PR-2 from VPG-1
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_uuids = [pr1_pi_uuids[0], pr2_pi_uuids[0]]
        vpg_obj, pi_refs = _attach_pi_simultaneously(
            vpg_obj, delete_pi_uuids=pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 4)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertEqual(len(vpg_ae_ids.values()), 4)
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertEqual(ae_id_sorted, [0] * 4)
        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0])

        # Case 3
        # Deattach all PIs/PR-1. AE-IDs at PR-1 to be de-allocated
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_uuids = pr1_pi_uuids[1:3]
        vpg_obj, pi_refs = _attach_pi_simultaneously(
            vpg_obj, delete_pi_uuids=pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertEqual(len(vpg_ae_ids.values()), 2)
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertEqual(ae_id_sorted, [0] * 2)
        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 0)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0])

    def test_add_delete_2_pi_simultaneously_to_vpg_with_two_pi(self):
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()
        VPG_CLASS = self._api_server.get_resource_class('virtual-port-group')
        org_process_ae_id = VPG_CLASS._process_ae_id

        class MockVpg(VPG_CLASS):
            org_process_ae_id = VPG_CLASS._process_ae_id
            HOLD_API = True
            @classmethod
            def mock_process_ae_id(cls, db_obj_dict, vpg_name, obj_dict=None):
                while cls.HOLD_API:
                    print('sleeping for HOLD_API to clear for '
                          'args = %s' % obj_dict)
                    gevent.sleep(0.5)
                return cls.org_process_ae_id(db_obj_dict, vpg_name, obj_dict)

        def process_ae_ids(x):
            return [int(i) for i in sorted(x) if i is not None]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 4
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr2_pi_names = ['%s_pr2_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr2_pi_objs = self._create_pi_objects(pr_objs[1], pr2_pi_names)
        pi_objs.update(pr1_pi_objs)
        pi_objs.update(pr2_pi_objs)

        # create a VPG
        vpg_count = 1
        vpg_names = ['vpg_%s_%s' % (test_id, i) for i in range(
                     1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        def _attach_pi_simultaneously(
                vpg_obj, create_pi_uuids=None, delete_pi_uuids=None):
            if create_pi_uuids is None:
                create_pi_uuids = []
            if delete_pi_uuids is None:
                delete_pi_uuids = []
            # Attach PIs from PR1 to VPG-1
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            try:
                # mock _process_ae_id at VPG resource
                VPG_CLASS._process_ae_id = MockVpg.mock_process_ae_id
                # MockVpg.HOLD_API = True
                MockVpg.HOLD_API = False
                for pi_uuid in create_pi_uuids:
                    gevent.spawn(
                        self.api.ref_update,
                        "virtual-port-group",
                        vpg_obj.uuid,
                        "physical-interface",
                        pi_uuid,
                        None,
                        "ADD",
                        None)
                for pi_uuid in delete_pi_uuids:
                    gevent.spawn(
                        self.api.ref_update,
                        "virtual-port-group",
                        vpg_obj.uuid,
                        "physical-interface",
                        pi_uuid,
                        None,
                        "DELETE",
                        None)
                gevent.sleep(2)
                MockVpg.HOLD_API = False
                gevent.sleep(5)
            except gevent.timeout.Timeout:
                self.assertFalse(
                    False,
                    '%s failed unexpectedly' % VPG_CLASS._process_ae_id)
            finally:
                # reset mock to original
                VPG_CLASS._process_ae_id = org_process_ae_id
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            pi_refs = vpg_obj.get_physical_interface_refs()
            return vpg_obj, pi_refs

        # Case 1
        # Attach 2 PIs from PR1 to VPG-1
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_uuids = [pi.uuid for pi in list(pr1_pi_objs.values())[0:2]]
        vpg_obj, pi_refs = _attach_pi_simultaneously(vpg_obj, pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertEqual(len(vpg_ae_ids.values()), 2)
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertEqual(ae_id_sorted, [0] * 2)
        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])

        # Case 2
        # Attach 2 PIs from PR2 to VPG-1 and Delete existing PIs/PR1
        # simultaneously
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        exiting_pi_refs = vpg_obj.get_physical_interface_refs()
        existing_pi_uuids = [ref['uuid'] for ref in exiting_pi_refs]
        pi_uuids = [pi.uuid for pi in list(pr2_pi_objs.values())[0:2]]
        vpg_obj, pi_refs = _attach_pi_simultaneously(
            vpg_obj, pi_uuids, existing_pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertEqual(len(set(vpg_ae_ids.values())), 1)
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertEqual(ae_id_sorted, [0] * 2)
        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 0)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0])

        # Case 3
        # Attach PI3/PR1 and PI3/PR2 to VPG-1 and
        # Delete existing PIs/PR2 simultaneously
        # One AE-ID to be allocated to each PI as each PI belong to
        # different PR
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        exiting_pi_refs = vpg_obj.get_physical_interface_refs()
        existing_pi_uuids = [ref['uuid'] for ref in exiting_pi_refs]
        pr1_pi_uuids = [list(pr1_pi_objs.values())[3].uuid]
        pr2_pi_uuids = [list(pr2_pi_objs.values())[3].uuid]
        pi_uuids = pr1_pi_uuids + pr2_pi_uuids
        vpg_obj, pi_refs = _attach_pi_simultaneously(
            vpg_obj, pi_uuids, existing_pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), len(pi_refs))
        self.assertEqual(len(vpg_ae_ids.values()), 2)
        ae_id_sorted = process_ae_ids(vpg_ae_ids.values())
        self.assertTrue(
            (ae_id_sorted == [0, 0] or ae_id_sorted == [0, 1]))
        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [0])

        # Case 4
        # Delete PI3/PR1 and PI3/PR2 simultaneously
        # all AE-IDs to be de-allocated
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        exiting_pi_refs = vpg_obj.get_physical_interface_refs()
        existing_pi_uuids = [ref['uuid'] for ref in exiting_pi_refs]
        vpg_obj, pi_refs = _attach_pi_simultaneously(
            vpg_obj, delete_pi_uuids=existing_pi_uuids)
        # verify PI-refs are correct
        self.assertIsNone(pi_refs)
        vpg_ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                      for ref in pi_refs or []}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(vpg_ae_ids.keys())), 0)
        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 0)
        self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 0)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])

    def test_add_pis_simultaneously_to_3_vpgs_with_no_pis(self):
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()
        VPG_CLASS = self._api_server.get_resource_class('virtual-port-group')
        org_process_ae_id = VPG_CLASS._process_ae_id

        class MockVpg(VPG_CLASS):
            org_process_ae_id = VPG_CLASS._process_ae_id
            HOLD_API = True
            @classmethod
            def mock_process_ae_id(cls, db_obj_dict, vpg_name, obj_dict=None):
                while cls.HOLD_API:
                    print('sleeping for HOLD_API to clear for '
                          'args = %s' % obj_dict)
                    gevent.sleep(0.5)
                return cls.org_process_ae_id(db_obj_dict, vpg_name, obj_dict)

        def process_ae_ids(x):
            return [int(i) for i in sorted(x) if i is not None]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 6
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr1_pi_uuids = [pi_obj.uuid for pi_obj in list(pr1_pi_objs.values())]
        pi_objs.update(pr1_pi_objs)

        # create VPG
        vpg_count = 3
        vpg_names = ['vpg_%s_%s' % (test_id, i) for i in range(
                     1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        def _attach_pi_simultaneously_and_verify(vpg_infos, oper):
            try:
                # mock _process_ae_id at VPG resource
                VPG_CLASS._process_ae_id = MockVpg.mock_process_ae_id
                # MockVpg.HOLD_API = True
                MockVpg.HOLD_API = False
                vpg_objs = {}
                for vpg_obj, curr_pi_uuids in vpg_infos.items():
                    create_pi_uuids = curr_pi_uuids[0]
                    delete_pi_uuids = curr_pi_uuids[1]
                    vpg_objs[vpg_obj] = (len(create_pi_uuids),
                                         len(delete_pi_uuids))
                    for pi_uuid in create_pi_uuids:
                        gevent.spawn(
                            self.api.ref_update,
                            "virtual-port-group",
                            vpg_obj.uuid,
                            "physical-interface",
                            pi_uuid,
                            None,
                            "ADD",
                            None)
                    for pi_uuid in delete_pi_uuids:
                        gevent.spawn(
                            self.api.ref_update,
                            "virtual-port-group",
                            vpg_obj.uuid,
                            "physical-interface",
                            pi_uuid,
                            None,
                            "DELETE",
                            None)
                gevent.sleep(2)
                MockVpg.HOLD_API = False
                gevent.sleep(3)
            except gevent.timeout.Timeout:
                self.assertFalse(
                    False,
                    '%s failed unexpectedly' % VPG_CLASS._process_ae_id)
            finally:
                # reset mock to original
                VPG_CLASS._process_ae_id = org_process_ae_id

            aes_at_vpgs = defaultdict(list)
            for vpg_obj, pi_counts in vpg_objs.items():
                create_pi_count = pi_counts[0]
                vpg_obj = self._vnc_lib.virtual_port_group_read(
                    id=vpg_obj.uuid)
                pi_refs = vpg_obj.get_physical_interface_refs() or []
                self.assertEqual(len(pi_refs), create_pi_count)
                ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                          for ref in pi_refs}
                # verify all AE-IDs allocated per prouter are unique
                self.assertEqual(len(set(ae_ids.keys())), len(pi_refs))
                self.assertEqual(len(ae_ids.values()), create_pi_count)
                self.assertEqual(len(set(ae_ids.values())),
                                 1 if create_pi_count else 0)
                pi_aes = defaultdict(list)
                for pi_ref in pi_refs:
                    pi_aes[pi_ref['to'][1]] += [pi_ref['attr'].ae_num]
                for pr, aes in pi_aes.items():
                    if pr not in aes_at_vpgs:
                        aes_at_vpgs[pr] += aes
                        continue
                    for gl_ae in aes_at_vpgs[pr]:
                        self.assertNotIn(gl_ae, aes)
                    aes_at_vpgs[pr] += aes
            # verification at Physical Routers
            ae_count = len(vpg_objs.keys()) if oper == 'create' else 0
            pr_ae_ids = get_zk_ae_ids()
            self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), ae_count)
            self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]),
                             list(range(ae_count)))

        pi_uuid_tups = [(pr1_pi_uuids[i], pr1_pi_uuids[i + 1])
                        for i in [0, 2, 4]]
        # Case 1
        # Attach 2 PIs from PR1 to all VPGs
        vpg_infos = {vpg_objs[vpg_names[i]]: (pi_uuid_tups[i], [])
                     for i in range(3)}
        _attach_pi_simultaneously_and_verify(vpg_infos, 'create')

        # Case 2
        # Delete 2  PIs from all VPGs
        vpg_infos = {vpg_objs[vpg_names[i]]: ([], pi_uuid_tups[i])
                     for i in range(3)}
        _attach_pi_simultaneously_and_verify(vpg_infos, 'delete')

    def test_add_pis_simultaneously_to_3_vpgs_with_1_pi(self):
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()
        VPG_CLASS = self._api_server.get_resource_class('virtual-port-group')
        org_process_ae_id = VPG_CLASS._process_ae_id

        class MockVpg(VPG_CLASS):
            org_process_ae_id = VPG_CLASS._process_ae_id
            HOLD_API = True
            @classmethod
            def mock_process_ae_id(cls, db_obj_dict, vpg_name, obj_dict=None):
                while cls.HOLD_API:
                    print('sleeping for HOLD_API to clear for '
                          'args = %s' % obj_dict)
                    gevent.sleep(0.5)
                return cls.org_process_ae_id(db_obj_dict, vpg_name, obj_dict)

        def process_ae_ids(x):
            return [int(i) for i in sorted(x) if i is not None]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 12
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr1_pi_uuids = [pi_obj.uuid for pi_obj in list(pr1_pi_objs.values())]
        pi_objs.update(pr1_pi_objs)

        # create VPG
        vpg_count = 3
        vpg_names = ['vpg_%s_%s' % (test_id, i) for i in range(
                     1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        def _attach_pi_simultaneously_and_verify(vpg_infos, factor):
            try:
                # mock _process_ae_id at VPG resource
                VPG_CLASS._process_ae_id = MockVpg.mock_process_ae_id
                # MockVpg.HOLD_API = True
                MockVpg.HOLD_API = False
                vpg_objs = {}
                for vpg_obj, curr_pi_uuids in vpg_infos.items():
                    create_pi_uuids = curr_pi_uuids[0]
                    delete_pi_uuids = curr_pi_uuids[1]
                    vpg_objs[vpg_obj] = (len(create_pi_uuids),
                                         len(delete_pi_uuids))
                    for pi_uuid in create_pi_uuids:
                        gevent.spawn(
                            self.api.ref_update,
                            "virtual-port-group",
                            vpg_obj.uuid,
                            "physical-interface",
                            pi_uuid,
                            None,
                            "ADD",
                            None)
                    for pi_uuid in delete_pi_uuids:
                        gevent.spawn(
                            self.api.ref_update,
                            "virtual-port-group",
                            vpg_obj.uuid,
                            "physical-interface",
                            pi_uuid,
                            None,
                            "DELETE",
                            None)
                gevent.sleep(2)
                MockVpg.HOLD_API = False
                gevent.sleep(3)
            except gevent.timeout.Timeout:
                self.assertFalse(
                    False,
                    '%s failed unexpectedly' % VPG_CLASS._process_ae_id)
            finally:
                # reset mock to original
                VPG_CLASS._process_ae_id = org_process_ae_id

            aes_at_vpgs = defaultdict(list)
            for vpg_obj, pi_counts in vpg_objs.items():
                create_pi_count = pi_counts[0] * factor
                if not create_pi_count and factor:
                    create_pi_count = pi_counts[1]
                vpg_obj = self._vnc_lib.virtual_port_group_read(
                    id=vpg_obj.uuid)
                pi_refs = vpg_obj.get_physical_interface_refs() or []
                self.assertEqual(len(pi_refs), create_pi_count)
                ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                          for ref in pi_refs}
                # verify all AE-IDs allocated per prouter are unique
                self.assertEqual(len(set(ae_ids.keys())), len(pi_refs))
                self.assertEqual(len(ae_ids.values()), create_pi_count)
                self.assertEqual(len(set(ae_ids.values())),
                                 1 if create_pi_count else 0)
                pi_aes = defaultdict(list)
                for pi_ref in pi_refs:
                    pi_aes[pi_ref['to'][1]] += [pi_ref['attr'].ae_num]
                for pr, aes in pi_aes.items():
                    if pr not in aes_at_vpgs:
                        aes_at_vpgs[pr] += aes
                        continue
                    for gl_ae in aes_at_vpgs[pr]:
                        self.assertNotIn(gl_ae, aes)
                    aes_at_vpgs[pr] += aes
            # verification at Physical Routers
            ae_count = len(vpg_objs.keys()) * (
                factor - 1 if factor > 1 else factor)
            pr_ae_ids = get_zk_ae_ids()
            self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), ae_count)
            self.assertEqual(process_ae_ids(
                pr_ae_ids[pr_objs[0].name]), list(range(ae_count)))

        pi_uuid_tups = [(pr1_pi_uuids[i], pr1_pi_uuids[i + 1])
                        for i in [0, 2, 4]]
        # Case 1
        # Attach 2 PIs from PR1 to all VPGs
        vpg_infos = {vpg_objs[vpg_names[i]]: (pi_uuid_tups[i], [])
                     for i in range(3)}
        _attach_pi_simultaneously_and_verify(vpg_infos, 1)

        # Case 2
        # Attach 2 more PIs from PR1 to all VPGs
        pi_uuid_tups = [(pr1_pi_uuids[i], pr1_pi_uuids[i + 1])
                        for i in [6, 8, 10]]
        vpg_infos = {vpg_objs[vpg_names[i]]: (pi_uuid_tups[i], [])
                     for i in range(3)}
        _attach_pi_simultaneously_and_verify(vpg_infos, 2)

        # Case 3
        # Delete first 2 PIs from all VPGs
        pi_uuid_tups = [(pr1_pi_uuids[i], pr1_pi_uuids[i + 1])
                        for i in [0, 2, 4]]
        vpg_infos = {vpg_objs[vpg_names[i]]: ([], pi_uuid_tups[i])
                     for i in range(3)}
        _attach_pi_simultaneously_and_verify(vpg_infos, 1)

        # Case 4
        # Delete all PIs from all VPGs
        pi_uuid_tups = [(pr1_pi_uuids[i], pr1_pi_uuids[i + 1])
                        for i in [6, 8, 10]]
        vpg_infos = {vpg_objs[vpg_names[i]]: ([], pi_uuid_tups[i])
                     for i in range(3)}
        _attach_pi_simultaneously_and_verify(vpg_infos, 0)

    def test_add_pis_simultaneously_to_multiple_vpgs(self):
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()
        VPG_CLASS = self._api_server.get_resource_class('virtual-port-group')
        org_process_ae_id = VPG_CLASS._process_ae_id

        class MockVpg(VPG_CLASS):
            org_process_ae_id = VPG_CLASS._process_ae_id
            HOLD_API = True
            @classmethod
            def mock_process_ae_id(cls, db_obj_dict, vpg_name, obj_dict=None):
                while cls.HOLD_API:
                    print('sleeping for HOLD_API to clear for '
                          'args = %s' % obj_dict)
                    gevent.sleep(0.5)
                return cls.org_process_ae_id(db_obj_dict, vpg_name, obj_dict)

        def process_ae_ids(x):
            return [int(i) for i in sorted(x) if i is not None]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 510
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr2_pi_names = ['%s_pr2_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr2_pi_objs = self._create_pi_objects(pr_objs[1], pr2_pi_names)
        pr1_pi_uuids = [pi_obj.uuid for pi_obj in pr1_pi_objs.values()]
        pr2_pi_uuids = [pi_obj.uuid for pi_obj in pr2_pi_objs.values()]
        pi_maps = zip(pr1_pi_uuids, pr2_pi_uuids)
        pi_objs.update(pr1_pi_objs)
        pi_objs.update(pr2_pi_objs)

        # create VPG
        vpg_count = 125
        vpg_names = ['vpg_%s_%s' % (test_id, i) for i in range(
                     1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        def _attach_pi_simultaneously_and_verify(vpg_pi_dicts, pi_count):
            ret_dict = {}
            # Attach PIs from PR1 to VPG-1
            try:
                # mock _process_ae_id at VPG resource
                VPG_CLASS._process_ae_id = MockVpg.mock_process_ae_id
                MockVpg.HOLD_API = False
                # MockVpg.HOLD_API = True
                for vpg_obj, pi_uuids in vpg_pi_dicts.items():
                    vpg_obj = self._vnc_lib.virtual_port_group_read(
                        id=vpg_obj.uuid)
                    for pi_uuid in pi_uuids:
                        gevent.spawn(
                            self.api.ref_update,
                            "virtual-port-group",
                            vpg_obj.uuid,
                            "physical-interface",
                            pi_uuid,
                            None,
                            "ADD",
                            None)
                gevent.sleep(3)
                MockVpg.HOLD_API = False
                gevent.sleep(3)
            except gevent.timeout.Timeout:
                self.assertFalse(
                    False,
                    '%s failed unexpectedly' % VPG_CLASS._process_ae_id)
            finally:
                # reset mock to original
                VPG_CLASS._process_ae_id = org_process_ae_id
            # verify PI-refs are correct
            aes_at_vpgs = defaultdict(list)
            for vpg_obj in vpg_pi_dicts.keys():
                vpg_obj = self._vnc_lib.virtual_port_group_read(
                    id=vpg_obj.uuid)
                pi_refs = vpg_obj.get_physical_interface_refs()
                self.assertEqual(len(pi_refs), pi_count)
                ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                          for ref in pi_refs}
                # verify all AE-IDs allocated per prouter are unique
                self.assertEqual(len(set(ae_ids.keys())), len(pi_refs))
                self.assertEqual(len(ae_ids.values()), len(pi_refs))
                self.assertTrue(len(set(ae_ids.values())) <= 2)
                pi_aes = defaultdict(list)
                for pi_ref in pi_refs:
                    pi_aes[pi_ref['to'][1]] += [pi_ref['attr'].ae_num]
                for pr, aes in pi_aes.items():
                    if pr not in aes_at_vpgs:
                        aes_at_vpgs[pr] += aes
                        continue
                    for gl_ae in aes_at_vpgs[pr]:
                        self.assertNotIn(gl_ae, aes)
                    aes_at_vpgs[pr] += aes

            # verification at Physical Routers
            pr_ae_ids = get_zk_ae_ids()
            self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 125)
            self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), 125)
            self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]),
                             list(range(125)))
            self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]),
                             list(range(125)))
            return ret_dict

        # Case 1
        # Attach 125 PIs from PR1 to VPG-1
        pi_maps = list(zip(pr1_pi_uuids, pr2_pi_uuids))[0:125]
        vpg_infos = {vpg_objs[vpg_names[i]]: pi_maps[i] for i in range(125)}
        _attach_pi_simultaneously_and_verify(vpg_infos, 2)

        # Case 2
        # Attach another 125 PIs from PR1 and PR2 to VPGs(same as Case 1)
        pi_maps = list(zip(pr1_pi_uuids, pr2_pi_uuids))[125:250]
        vpg_infos = {vpg_objs[vpg_names[i]]: pi_maps[i] for i in range(125)}
        _attach_pi_simultaneously_and_verify(vpg_infos, 4)

        # Case 3
        # Attach 125 more PIs from PR1 and PR2 to VPGs
        pi_maps = list(zip(pr1_pi_uuids, pr2_pi_uuids))[250:375]
        vpg_infos = {vpg_objs[vpg_names[i]]: pi_maps[i] for i in range(125)}
        _attach_pi_simultaneously_and_verify(vpg_infos, 6)

        # Case 4
        # Attach 2 more PIs from PR1 to VPG-1
        pi_maps = list(zip(pr1_pi_uuids, pr2_pi_uuids))[375:500]
        vpg_infos = {vpg_objs[vpg_names[i]]: pi_maps[i] for i in range(125)}
        _attach_pi_simultaneously_and_verify(vpg_infos, 8)

    # CEM-17796
    def test_add_delete_simultaneously_to_vpg_check_alloc_dealloc(self):
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()
        VPG_CLASS = self._api_server.get_resource_class('virtual-port-group')
        org_process_ae_id = VPG_CLASS._process_ae_id

        class MockVpg(VPG_CLASS):
            org_process_ae_id = VPG_CLASS._process_ae_id
            HOLD_API = 0.1
            @classmethod
            def mock_process_ae_id(cls, db_obj_dict, vpg_name, obj_dict=None):
                sleep_secs = 0.01 * cls.HOLD_API
                print('sleeping for (%s) to simulate parallel calls '
                      'args = %s' % (sleep_secs, obj_dict))
                # gevent.sleep(sleep_secs)
                print('Released HOLD_API for args = %s' % obj_dict)
                return cls.org_process_ae_id(db_obj_dict, vpg_name, obj_dict)

        def process_ae_ids(x):
            return [int(i) for i in sorted(x) if i is not None]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 8
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr2_pi_names = ['%s_pr2_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr2_pi_objs = self._create_pi_objects(pr_objs[1], pr2_pi_names)
        pr1_pi_uuids = [pi_obj.uuid for pi_obj in list(pr1_pi_objs.values())]
        pr2_pi_uuids = [pi_obj.uuid for pi_obj in list(pr2_pi_objs.values())]
        pi_objs.update(pr1_pi_objs)
        pi_objs.update(pr2_pi_objs)

        # create VPG
        vpg_count = 2
        vpg_names = ['vpg_%s_%s' % (test_id, i) for i in range(
                     1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        def _attach_pi_simultaneously_and_verify(vpg_infos, factor):
            try:
                # mock _process_ae_id at VPG resource
                VPG_CLASS._process_ae_id = MockVpg.mock_process_ae_id
                vpg_objs = {}
                MockVpg.HOLD_API = 0.1
                for vpg_obj, curr_pi_uuids in vpg_infos.items():
                    create_pi_uuids = curr_pi_uuids[0]
                    delete_pi_uuids = curr_pi_uuids[1]
                    vpg_objs[vpg_obj] = (len(create_pi_uuids),
                                         len(delete_pi_uuids))
                    for pi_uuid in create_pi_uuids:
                        gevent.spawn(
                            self.api.ref_update,
                            "virtual-port-group",
                            vpg_obj.uuid,
                            "physical-interface",
                            pi_uuid,
                            None,
                            "ADD",
                            None)
                    for pi_uuid in delete_pi_uuids:
                        gevent.spawn(
                            self.api.ref_update,
                            "virtual-port-group",
                            vpg_obj.uuid,
                            "physical-interface",
                            pi_uuid,
                            None,
                            "DELETE",
                            None)
                MockVpg.HOLD_API += 0.1
                gevent.sleep(5)
            except gevent.timeout.Timeout:
                self.assertFalse(
                    False,
                    '%s failed unexpectedly' % VPG_CLASS._process_ae_id)
            finally:
                # reset mock to original
                VPG_CLASS._process_ae_id = org_process_ae_id

            aes_at_vpgs = {}
            aes_at_prs = defaultdict(list)
            for vpg_obj, pi_counts in vpg_objs.items():
                create_pi_count = pi_counts[0] * factor
                if not create_pi_count and not factor:
                    create_pi_count = 1
                vpg_obj = self._vnc_lib.virtual_port_group_read(
                    id=vpg_obj.uuid)
                pi_refs = vpg_obj.get_physical_interface_refs() or []
                self.assertEqual(len(pi_refs), create_pi_count)
                ae_ids = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                          for ref in pi_refs}
                # verify all AE-IDs allocated per prouter are unique
                if factor == 0:
                    self.assertIsNone(list(ae_ids.values())[0])
                    continue
                self.assertEqual(len(set(ae_ids.keys())), len(pi_refs))
                self.assertEqual(len(ae_ids.values()), create_pi_count)
                self.assertEqual(len(set(ae_ids.values())),
                                 1 if create_pi_count else 0)
                pi_aes = defaultdict(list)
                for pi_ref in pi_refs:
                    pi_aes[pi_ref['to'][1]] += [pi_ref['attr'].ae_num]
                for pr, aes in pi_aes.items():
                    aes = list(set(aes))
                    self.assertEqual(len(aes), 1)
                    if vpg_obj.uuid not in aes_at_vpgs:
                        self.assertNotIn(aes[0], aes_at_vpgs.values())
                        aes_at_vpgs[vpg_obj.uuid] = aes[0]
                    for ae_pr, pr_ae in aes_at_prs.items():
                        if ae_pr == pr:
                            self.assertNotIn(aes[0], pr_ae)
                    else:
                        aes_at_prs[pr] += aes
            # verification at Physical Routers
            ae_count = len(vpg_objs.keys()) * (
                factor - 1 if factor > 1 else factor)
            pr_ae_ids = get_zk_ae_ids()
            self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), ae_count)
            self.assertEqual(process_ae_ids(
                pr_ae_ids[pr_objs[0].name]), list(range(ae_count)))
            self.assertEqual(len(pr_ae_ids[pr_objs[1].name]), ae_count)
            self.assertEqual(process_ae_ids(
                pr_ae_ids[pr_objs[1].name]), list(range(ae_count)))

        # Case 1
        # Attach 2 PI/PR1 and 2 PI/PR2 to 2 VPGs
        # one AE-ID per PR is allocated to each VPG
        pi_uuid_tups = [[pr1_pi_uuids[i], pr1_pi_uuids[i + 1],
                        pr2_pi_uuids[i], pr2_pi_uuids[i + 1]]
                        for i in [0, 2]]
        vpg_infos = {vpg_objs[vpg_names[i]]: (pi_uuid_tups[i], [])
                     for i in range(2)}
        _attach_pi_simultaneously_and_verify(vpg_infos, 1)

        # Case 2
        # Attach 2 more PI/PR1 and 2 more PI/PR2 to 2 VPGs
        # No change in AE-ID allocations
        pi_uuid_tups = [[pr1_pi_uuids[i], pr1_pi_uuids[i + 1],
                        pr2_pi_uuids[i], pr2_pi_uuids[i + 1]]
                        for i in [4, 6]]
        vpg_infos = {vpg_objs[vpg_names[i]]: (pi_uuid_tups[i], [])
                     for i in range(2)}
        _attach_pi_simultaneously_and_verify(vpg_infos, 2)

        # Case 3
        # Delete all but 1 PI/PR1 and 1 PI/PR2 from 2 VPGs
        # All AE-IDs are deallocated
        vpg_infos = {}
        for _, vpg in vpg_objs.items():
            vpg_obj = self._vnc_lib.virtual_port_group_read(
                id=vpg.uuid)
            pi_refs = vpg_obj.get_physical_interface_refs()
            self.assertEqual(len(pi_refs), len(pi_objs) / 2)
            # deleting PIs
            vpg_infos[vpg_obj] = ([], [ref['uuid'] for ref in pi_refs[:-1]])
        _attach_pi_simultaneously_and_verify(vpg_infos, 0)

    # CEM-17432
    def test_ae_id_alloc_dealloc_for_mock_ref_update(self):
        proj_obj, fabric_obj, pr_objs = self._create_prerequisites(
            create_second_pr=True)
        test_id = self.id()
        VPG_CLASS = self._api_server.get_resource_class('virtual-port-group')
        # org_process_ae_id = VPG_CLASS._process_ae_id

        def process_ae_ids(x):
            return [int(i) for i in sorted(x)]

        def get_zk_ae_ids(prs=None):
            prefix = os.path.join(
                self.__class__.__name__,
                'id', 'aggregated-ethernet')
            zk_client = self._api_server._db_conn._zk_db._zk_client._zk_client
            if not prs:
                prs = [os.path.join(prefix, pr.name) for pr in pr_objs]
            else:
                if not isinstance(prs, list):
                    prs = [prs]
                prs = [os.path.join(prefix, pr) for pr in prs]
            ae_ids = {}
            for pr in prs:
                pr_org = os.path.split(pr)[-1]
                ae_ids[pr_org] = zk_client.get_children(pr)
            return ae_ids

        pi_per_pr = 4
        pi_objs = {}
        pr1_pi_names = ['%s_pr1_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr2_pi_names = ['%s_pr2_pi%d' % (test_id, i) for
                        i in range(1, pi_per_pr + 1)]
        pr1_pi_objs = self._create_pi_objects(pr_objs[0], pr1_pi_names)
        pr2_pi_objs = self._create_pi_objects(pr_objs[1], pr2_pi_names)
        pi_objs.update(pr1_pi_objs)
        pi_objs.update(pr2_pi_objs)

        # create one VPG
        vpg_count = 1
        vpg_names = ['vpg_%s_%s' % (test_id, i) for i in range(
                     1, vpg_count + 1)]
        vpg_objs = self._create_vpgs(fabric_obj, vpg_names)

        # record AE-IDs in ZK before creating any VPG
        ae_ids = [x for x in get_zk_ae_ids().values() if x]
        self.assertEqual(len(ae_ids), 0)

        # Case 1
        # Attach PI-1/PR-1 to VPG-1
        ae_ids = {}
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        for pi in range(1):
            vpg_obj.add_physical_interface(pi_objs[pr1_pi_names[pi]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 1)
        # verify No AE-Id is allocated
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertIsNone(list(ae_ids[vpg_name].values())[0])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 0)

        # Case 2
        # Attach PI-2/PR-1 to VPG-1
        class MockVpg(VPG_CLASS):
            org_process_ae_id = VPG_CLASS._process_ae_id
            @classmethod
            def mock_process_ae_id(cls, db_obj_dict, vpg_name, obj_dict=None):
                def mock_ref_update_exc(*args, **kwargs):
                    raise Exception(
                        "Fake ref update exception while adding PI")
                with test_common.patch(VncDbClient,
                                       'ref_update', mock_ref_update_exc):
                    return cls.org_process_ae_id(db_obj_dict,
                                                 vpg_name, obj_dict)

        # Mock ref_update (inside _process_ae_id) and attach PI-3/PR-1 to VPG-1
        def mock_ref_update_fail(*args, **kwargs):
            raise Exception("Fake ref update in stateful_update")

        def _attach_pi_sequentially(vpg_obj, pi_uuids):
            # Attach PIs from PR1 to VPG-1
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            # mock _process_ae_id at VPG resource
            VPG_CLASS = self._api_server.get_resource_class(
                'virtual-port-group')
            org_process_ae_id = VPG_CLASS._process_ae_id
            try:
                VPG_CLASS._process_ae_id = MockVpg.mock_process_ae_id
                with ExpectedException(vnc_api_HttpError):
                    for pi_uuid in pi_uuids:
                        self.api.ref_update(
                            "virtual-port-group",
                            vpg_obj.uuid,
                            "physical-interface",
                            pi_uuid,
                            None,
                            "ADD",
                            None)
            finally:
                VPG_CLASS._process_ae_id = org_process_ae_id
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            pi_refs = vpg_obj.get_physical_interface_refs()
            return vpg_obj, pi_refs

        # verify PI-refs are correct
        ae_ids = {}
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_uuids = [pi_objs[pr1_pi_names[pi]].uuid for pi in range(1, 2)]
        vpg_obj, pi_refs = _attach_pi_sequentially(vpg_obj, pi_uuids)
        # verify PI-refs are correct
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 1)
        # verify No AE-Id is allocated
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertIsNone(list(ae_ids[vpg_name].values())[0])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 0)

        # Case 3
        # without mocks, add PI-2/PR-1
        ae_ids = {}
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        for pi in range(1, 2):
            vpg_obj.add_physical_interface(pr1_pi_objs[pr1_pi_names[pi]])
        self.api.virtual_port_group_update(vpg_obj)
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_refs = vpg_obj.get_physical_interface_refs()
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        self.assertEqual(process_ae_ids(ae_ids[vpg_name].values()), [0, 0])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])

        # Case 4
        # Now attach PI-3/PR-1 to VPG-1
        def _attach_pi_sequentially(vpg_obj, pi_uuids):
            # Attach PIs from PR1 to VPG-1
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            # mock _process_ae_id at VPG resource
            with test_common.patch(VncDbClient,
                                   'ref_update', mock_ref_update_fail):
                with ExpectedException(vnc_api_HttpError):
                    for pi_uuid in pi_uuids:
                        self.api.ref_update(
                            "virtual-port-group",
                            vpg_obj.uuid,
                            "physical-interface",
                            pi_uuid,
                            None,
                            "ADD",
                            None)
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            pi_refs = vpg_obj.get_physical_interface_refs()
            return vpg_obj, pi_refs

        ae_ids = {}
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_uuids = [pi_objs[pr1_pi_names[pi]].uuid for pi in range(2, 3)]
        vpg_obj, pi_refs = _attach_pi_sequentially(vpg_obj, pi_uuids)
        # verify PI-refs are correct
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        # verify No AE-Id is allocated
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        self.assertEqual(process_ae_ids(ae_ids[vpg_name].values()), [0, 0])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])

        # Case 5
        # Now attach PI-1/PR-2 to VPG-1
        # verify PI-refs are correct
        def _attach_pi_sequentially(vpg_obj, pi_uuids):
            # Attach PIs from PR1 to VPG-1
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            # mock _process_ae_id at VPG resource
            with test_common.patch(VncDbClient,
                                   'ref_update', mock_ref_update_fail):
                with ExpectedException(vnc_api_HttpError):
                    for pi_uuid in pi_uuids:
                        self.api.ref_update(
                            "virtual-port-group",
                            vpg_obj.uuid,
                            "physical-interface",
                            pi_uuid,
                            None,
                            "ADD",
                            None)
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
            pi_refs = vpg_obj.get_physical_interface_refs()
            return vpg_obj, pi_refs

        ae_ids = {}
        vpg_name = vpg_names[0]
        vpg_obj = vpg_objs[vpg_name]
        vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg_obj.uuid)
        pi_uuids = [pi_objs[pr2_pi_names[pi]].uuid for pi in range(0, 1)]
        vpg_obj, pi_refs = _attach_pi_sequentially(vpg_obj, pi_uuids)
        # verify PI-refs are correct
        self.assertEqual(len(pi_refs), 2)
        ae_ids[vpg_name] = {ref['href'].split('/')[-1]: ref['attr'].ae_num
                            for ref in pi_refs}
        # verify all AE-IDs allocated per prouter are unique
        self.assertEqual(len(set(ae_ids[vpg_name].keys())), len(pi_refs))
        self.assertEqual(len(set(ae_ids[vpg_name].values())), 1)
        self.assertEqual(process_ae_ids(ae_ids[vpg_name].values()), [0, 0])

        # verification at Physical Routers
        pr_ae_ids = get_zk_ae_ids()
        self.assertEqual(len(pr_ae_ids[pr_objs[0].name]), 1)
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[0].name]), [0])
        self.assertEqual(process_ae_ids(pr_ae_ids[pr_objs[1].name]), [])
