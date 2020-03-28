#
# Copyright (c) 2013,2014 Juniper Networks, Inc. All rights reserved.
#


import logging

import cfgm_common
from testtools import ExpectedException
from vnc_api.gen.resource_client import DataCenterInterconnect
from vnc_api.gen.resource_client import LogicalRouter
from vnc_api.gen.resource_client import NetworkIpam
from vnc_api.gen.resource_client import VirtualMachineInterface
from vnc_api.gen.resource_xsd import LogicalRouterPRListParams
from vnc_api.gen.resource_xsd import LogicalRouterPRListType
from vnc_api.vnc_api import IpamSubnetType, PhysicalRouter
from vnc_api.vnc_api import PolicyStatementType, PolicyTermType
from vnc_api.vnc_api import RoutingPolicy, SubnetType
from vnc_api.vnc_api import TermActionListType, TermMatchConditionType
from vnc_api.vnc_api import VirtualNetwork, VirtualNetworkType, VnSubnetsType

from vnc_cfg_api_server.tests import test_case


logger = logging.getLogger(__name__)


class TestDataCenterInterconnect(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestDataCenterInterconnect, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestDataCenterInterconnect, cls).tearDownClass(*args, **kwargs)

    def _create_prs(self, dict_prs):
        self.physical_routers = []
        for prname in dict_prs.keys():
            phy_router_obj = PhysicalRouter(
                parent_type='global-system-config',
                fq_name=["default-global-system-config",
                         prname],
                physical_router_management_ip="1.1.1.1",
                physical_router_vendor_name="juniper",
                physical_router_product_name="qfx5k",
                physical_router_user_credentials={"username": "username",
                                                  "password": "password"},
                physical_router_device_family='junos-qfx')
            self._vnc_lib.physical_router_create(phy_router_obj)
            self.physical_routers.append(phy_router_obj)
            dict_prs[prname] = phy_router_obj
        return
    # end _create_prs

    def make_vn_name(self, subnet):
        return "VN_%s" % subnet

    def make_rp_name(self, subnet):
        return "RP_%s" % subnet

    def make_lr_name(self, subnet1, subnet2=None, subnet3=None):
        if subnet3 is not None:
            return "LR_%s_%s_%s" % (subnet1, subnet2, subnet3)
        if subnet2 is not None:
            return "LR_%s_%s" % (subnet1, subnet2)
        return "LR_%s" % subnet1

    def create_vn_ipam(self, id):
        ipam1_obj = NetworkIpam('ipam' + '-' + id)
        ipam1_uuid = self._vnc_lib.network_ipam_create(ipam1_obj)
        return self._vnc_lib.network_ipam_read(id=ipam1_uuid)
    # end create_vn_ipam

    def create_vn_with_subnets(self, id, vn_name, ipam_obj, subnet,
                               subnetmask=24):
        vn_obj = VirtualNetwork(vn_name)
        vn_obj_properties = VirtualNetworkType()
        vn_obj_properties.set_vxlan_network_identifier(2000 + id)
        vn_obj_properties.set_forwarding_mode('l2_l3')
        vn_obj.set_virtual_network_properties(vn_obj_properties)
        vn_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType(subnet, subnetmask))]))
        vn_uuid = self._vnc_lib.virtual_network_create(vn_obj)
        return self._vnc_lib.virtual_network_read(id=vn_uuid)
    # end create_vn_with_subnets

    def create_rp(self, rp_name, action="accept", term_t='network-device'):
        rp = RoutingPolicy(name=rp_name, term_type=term_t)
        rp.set_routing_policy_entries(
            PolicyStatementType(
                term=[
                    PolicyTermType(
                        term_match_condition=TermMatchConditionType(),
                        term_action_list=TermActionListType(action=action))
                ])
        )
        rp_uuid = self._vnc_lib.routing_policy_create(rp)
        return self._vnc_lib.routing_policy_read(id=rp_uuid)
    # end create_rp

    def create_lr(self, lrname, vns, prs, vmis):
        lr_fq_name = ['default-domain', 'default-project', lrname]
        lr = LogicalRouter(fq_name=lr_fq_name, parent_type='project',
                           logical_router_type='vxlan-routing')
        for pr in prs:
            probj = self._vnc_lib.physical_router_read(id=pr.get_uuid())
            lr.add_physical_router(probj)
        for vn in vns:
            vminame = 'vmi-lr-to-vn' + vn.get_display_name()
            fq_name1 = ['default-domain', 'default-project', vminame]
            vmi = VirtualMachineInterface(fq_name=fq_name1,
                                          parent_type='project')
            vmi.set_virtual_network(vn)
            self._vnc_lib.virtual_machine_interface_create(vmi)
            vmis[vminame] = vmi
            lr.add_virtual_machine_interface(vmi)
        lr_uuid = self._vnc_lib.logical_router_create(lr)
        return lr, self._vnc_lib.logical_router_read(id=lr_uuid)
    # end create_lr

    def create_intrafabric_dci(self, dciname, src_lrname, srcvn_names,
                               srcrp_names, dstlrs_pr_names_map, dict_lrs,
                               dict_vns, dict_rps_created, dict_prs,
                               update_lr_Ref=False):
        dci_fq_name = ["default-global-system-config", dciname]
        dci = DataCenterInterconnect(
            fq_name=dci_fq_name, parent_type='global-system-config',
            data_center_interconnect_type='intra_fabric')
        dci.add_logical_router(dict_lrs[src_lrname])
        for vnname in srcvn_names:
            dci.add_virtual_network(dict_vns[vnname])
        for rpname in srcrp_names:
            dci.add_routing_policy(dict_rps_created[rpname])
        lrpr_list_obj = LogicalRouterPRListType()
        for dstlrname, prnames in dstlrs_pr_names_map.items():
            dci.add_logical_router(dict_lrs[dstlrname])
            dstlr_uuid = dict_lrs[dstlrname].get_uuid()
            dst_pr_list = []
            for prname in prnames:
                pruuid = dict_prs[prname].get_uuid()
                dst_pr_list.append(pruuid)
            lrpr_list_obj.add_logical_router_list(
                LogicalRouterPRListParams(
                    logical_router_uuid=dstlr_uuid,
                    physical_router_uuid_list=dst_pr_list))
        dci.set_destination_physical_router_list(lrpr_list_obj)
        if update_lr_Ref is True:
            # update refs for src lr
            for lrref in dci.logical_router_refs or []:
                if lrref['uuid'] == dict_lrs[src_lrname].get_uuid():
                    lrref['attr'] = 'source'
                    break
        return dci
    # end create_intrafabric_dci

    def _validate_dci_lr_refs_error(self, dci, srclr, update=False):
        func_call = self._vnc_lib.data_center_interconnect_create
        if update is True:
            func_call = self._vnc_lib.data_center_interconnect_update

        # - LR Refs: provide no src ref
        for lrref in dci.logical_router_refs or []:
            if lrref['uuid'] == srclr.get_uuid():
                lrref['attr'] = None
                break

        with ExpectedException(
                cfgm_common.exceptions.BadRequest,
                "No Source LR specified in logical_router_refs for "
                "intra_fabric type data_center_interconnect."):
            func_call(dci)
        if update is True:
            return

        # - LR Refs: provide multiple src ref
        for lrref in dci.logical_router_refs or []:
            lrref['attr'] = 'source'
        with ExpectedException(
                cfgm_common.exceptions.BadRequest,
                "More than one Source LR not allowed for intra_fabric "
                "type data_center_interconnect."):
            func_call(dci)

        # - LR Refs: provide single ref as src lr, no destination LR ref
        lrrefs = dci.logical_router_refs
        dci.set_logical_router(srclr)
        for lrref in dci.logical_router_refs or []:
            lrref['attr'] = 'source'
        with ExpectedException(
                cfgm_common.exceptions.BadRequest,
                "No Destination LR specified in logical_router_refs for "
                "intra_fabric type data_center_interconnect."):
            func_call(dci)

        # before returning update it with valid LR_refs
        dci.logical_router_refs = lrrefs
        for lrref in dci.logical_router_refs or []:
            if lrref['uuid'] == srclr.get_uuid():
                lrref['attr'] = 'source'
            else:
                lrref['attr'] = None
    # end _validate_dci_lr_refs_error

    def _validate_destination_pr_list_properties(self, dci, dict_lrs,
                                                 src_lrname, dst_lrname3):
        # validate destination_physical_router_list properties
        lr_list = dci.destination_physical_router_list.logical_router_list
        lruuid = lr_list[0].logical_router_uuid
        lr_list[0].logical_router_uuid = None
        with ExpectedException(
                cfgm_common.exceptions.BadRequest,
                "Invalid logical_router_uuid in "
                "destination_physical_router_list."):
            self._vnc_lib.data_center_interconnect_create(dci)

        lr_list[0].logical_router_uuid = dict_lrs[src_lrname].get_uuid()
        with ExpectedException(
                cfgm_common.exceptions.BadRequest,
                "%s logical_router_uuid exist in "
                "destination_physical_router_list is marked as a "
                " source LR." % dict_lrs[src_lrname].get_uuid()):
            self._vnc_lib.data_center_interconnect_create(dci)

        lr_list[0].logical_router_uuid = dict_lrs[dst_lrname3].get_uuid()
        with ExpectedException(
                cfgm_common.exceptions.BadRequest,
                "%s logical_router_uuid in destination_physical_router_list "
                "does not exist in logical_router_refs" %
                dict_lrs[dst_lrname3].get_uuid()):
            self._vnc_lib.data_center_interconnect_create(dci)

        lr_list[0].logical_router_uuid = lruuid
        prlist = lr_list[0].physical_router_uuid_list
        lr_list[0].physical_router_uuid_list = []
        with ExpectedException(
                cfgm_common.exceptions.BadRequest,
                "%s logical_router_uuid in destination_physical_router_list "
                "does not have  any physical_router" % lruuid):
            self._vnc_lib.data_center_interconnect_create(dci)

        lr_list[0].physical_router_uuid_list = prlist
        lruuid1 = lr_list[1].logical_router_uuid
        lr_list[1].logical_router_uuid = lruuid
        with ExpectedException(
                cfgm_common.exceptions.BadRequest,
                "%s logical_router_uuid in destination_physical_router_list "
                "specified more than once." % lruuid):
            self._vnc_lib.data_center_interconnect_create(dci)

        lr_list[1].logical_router_uuid = lruuid1
        lr_list = dci.destination_physical_router_list.logical_router_list
        dci.destination_physical_router_list.logical_router_list = []
        with ExpectedException(
                cfgm_common.exceptions.BadRequest,
                "No destination LR exist in "
                "destination_physical_router_list"):
            self._vnc_lib.data_center_interconnect_create(dci)

        for i in range(len(lr_list) - 1):
            dci.destination_physical_router_list.logical_router_list.append(
                lr_list[i])
        with ExpectedException(
                cfgm_common.exceptions.BadRequest,
                "destination LR list count %s in "
                "destination_physical_router_list does not match "
                "logical_router_Refs expected %s count" %
                ((len(lr_list) - 1), len(lr_list))):
            self._vnc_lib.data_center_interconnect_create(dci)

        dci.destination_physical_router_list.logical_router_list = lr_list
    # end _validate_destination_pr_list_properties

    def _validate_dci_rp_and_vn_refs(self, dci, srcrp_names,
                                     dict_rps_created):
        # validating RP and virtual networks negative case
        vn_refs = dci.virtual_network_refs
        for rpname in srcrp_names:
            dci.add_routing_policy(dict_rps_created[rpname])
        with ExpectedException(
                cfgm_common.exceptions.BadRequest,
                "Provide either routing_policy_refs or virtual_network_refs. "
                "Both has been provided for intra_fabric type "
                "data_center_interconnect"):
            self._vnc_lib.data_center_interconnect_create(dci)

        dci.virtual_network_refs = []
        dci.routing_policy_refs = []
        with ExpectedException(
                cfgm_common.exceptions.BadRequest,
                "Provide either routing_policy_refs or virtual_network_refs."
                " None of them provided for intra_fabric type"
                " data_center_interconnect"):
            self._vnc_lib.data_center_interconnect_create(dci)
        dci.virtual_network_refs = vn_refs
        # dci.routing_policy_refs = rp_refs
    # end _validate_dci_rp_and_vn_refs

    def _validate_master_lr_and_public_lr_cases(self, srcvn_name1,
                                                dst_lrname3, dict_lrs,
                                                dict_prs, dict_vmis,
                                                dict_vns, dict_rps_created):
        # prepare master_lr
        _, master_lr = self.create_lr(
            "master-LR", [dict_vns[srcvn_name1]], [dict_prs["PR1"]],
            dict_vmis)
        dict_lrs["master-LR"] = master_lr

        # set dstlr as public lr
        dict_lrs[dst_lrname3].set_logical_router_gateway_external(True)
        self._vnc_lib.logical_router_update(dict_lrs[dst_lrname3])
        dstlrs_pr_map1 = {}
        dstlrs_pr_map1[dst_lrname3] = ["PR3"]
        # create dci obj with master lr and public lr
        dci = self.create_intrafabric_dci(
            "dci_test2", "master-LR", [srcvn_name1], [], dstlrs_pr_map1,
            dict_lrs, dict_vns, dict_rps_created, dict_prs,
            update_lr_Ref=True)

        # Validate Negative dci master lr test case
        with ExpectedException(
                cfgm_common.exceptions.BadRequest,
                "master-LR as source logical router, destination LR "
                "%s must not have logical_router_gateway_external"
                " set to be True." % dict_lrs[dst_lrname3].display_name):
            self._vnc_lib.data_center_interconnect_create(dci)

        dict_lrs[dst_lrname3].set_logical_router_gateway_external(False)
        self._vnc_lib.logical_router_update(dict_lrs[dst_lrname3])
        with ExpectedException(
                cfgm_common.exceptions.BadRequest,
                "master-LR as source logical router, destination"
                " LR %s selected physical router uuid %s must "
                "exists in master-LR's physical routers list." %
                (dict_lrs[dst_lrname3].display_name,
                 dict_prs["PR3"].get_uuid())):
            self._vnc_lib.data_center_interconnect_create(dci)
    # end _validate_master_lr_and_public_lr_cases

    def test_dci_intrafabric(self):
        # prepare all dictionary for input params
        # dict_xx key: name, value is either db object or class object
        dict_rps_created = {}
        dict_vns = {}
        dict_lrs = {}
        dict_vmis = {}
        dict_prs = {"PR1": None, "PR2": None, "PR3": None, "PR4": None}

        # create 4 PR and single fabric
        self._create_prs(dict_prs)

        # create all 10 VNs and related object
        ipam_obj = self.create_vn_ipam(self.id())
        subnetmask = 24
        vn_starting_index = 21
        for i in range(vn_starting_index, vn_starting_index + 9):
            subnet = "%s.0.0.0" % i
            vn_name = self.make_vn_name(i)
            dict_vns[vn_name] = self.create_vn_with_subnets(
                i, vn_name, ipam_obj, subnet, subnetmask)

        # create one SRC LR with 2 PR, 3 VN
        vn_num = vn_starting_index
        src_lrname = self.make_lr_name(vn_num, vn_num + 1, vn_num + 2)
        srcvns = [dict_vns[self.make_vn_name(vn_num)],
                  dict_vns[self.make_vn_name(vn_num + 1)],
                  dict_vns[self.make_vn_name(vn_num + 2)]]
        srcvn_names = [self.make_vn_name(vn_num),
                       self.make_vn_name(vn_num + 2)]
        lrobj, dict_lrs[src_lrname] = self.create_lr(
            src_lrname, srcvns, [dict_prs["PR1"], dict_prs["PR2"]], dict_vmis)
        vn_num += 3

        # create RP
        srcrp_names = [self.make_rp_name(vn_num),
                       self.make_rp_name(vn_num + 2)]
        for rp in srcrp_names:
            dict_rps_created[rp] = self.create_rp(rp)

        # create 2 dst lrs
        vn_num += 2
        dst_lrname1 = self.make_lr_name(vn_num)
        dstvns1 = [dict_vns[self.make_vn_name(vn_num)]]
        _, dict_lrs[dst_lrname1] = self.create_lr(
            dst_lrname1, dstvns1, [dict_prs["PR2"]], dict_vmis)
        vn_num += 1
        dst_lrname2 = self.make_lr_name(vn_num)
        dstvns2 = [dict_vns[self.make_vn_name(vn_num)]]
        _, dict_lrs[dst_lrname2] = self.create_lr(
            dst_lrname2, dstvns2, [dict_prs["PR3"]], dict_vmis)
        vn_num += 1

        # create extra LR
        dst_lrname3 = self.make_lr_name(vn_num)
        dstvns3 = [dict_vns[self.make_vn_name(vn_num)]]
        _, dict_lrs[dst_lrname3] = self.create_lr(
            dst_lrname3, dstvns3, [dict_prs["PR3"]], dict_vmis)

        dstlrs_pr_map = {}
        dstlrs_pr_map[dst_lrname1] = ["PR2"]
        dstlrs_pr_map[dst_lrname2] = ["PR3"]

        # validate LR_refs negative test cases at create time
        dci = self.create_intrafabric_dci(
            "dci_test1", src_lrname, srcvn_names, [], dstlrs_pr_map,
            dict_lrs, dict_vns, {}, dict_prs, update_lr_Ref=True)
        self._validate_dci_lr_refs_error(dci, srclr=dict_lrs[src_lrname],
                                         update=False)
        # validate destination_physical_router_list properties
        self._validate_destination_pr_list_properties(dci, dict_lrs,
                                                      src_lrname, dst_lrname3)
        # validate Negative case for dci Routing policy and virtual networks
        self._validate_dci_rp_and_vn_refs(dci, srcrp_names, dict_rps_created)

        # validate master LR vs Public LR negative case
        srcvn_name1 = self.make_vn_name(vn_starting_index + 8)
        self._validate_master_lr_and_public_lr_cases(
            srcvn_name1, dst_lrname3, dict_lrs, dict_prs, dict_vmis,
            dict_vns, dict_rps_created)

        # create successfully dci with proper lr ref and
        dci = self.create_intrafabric_dci(
            "dci_test1", src_lrname, srcvn_names, [], dstlrs_pr_map,
            dict_lrs, dict_vns, dict_rps_created, dict_prs,
            update_lr_Ref=True)
        dci_uuid = self._vnc_lib.data_center_interconnect_create(dci)

        # Validate all negative case for update.
        dci.set_data_center_interconnect_type('inter_fabric')
        with ExpectedException(
                cfgm_common.exceptions.PermissionDenied,
                "Cannot change data_center_interconnect_type. Please"
                " specify data_center_interconnect_type as 'intra_fabric'"):
            self._vnc_lib.data_center_interconnect_update(dci)

        dci.set_data_center_interconnect_type('intra_fabric')
        self._validate_dci_lr_refs_error(dci, srclr=dict_lrs[src_lrname],
                                         update=True)
        dci = self._vnc_lib.data_center_interconnect_read(
            id=dci.get_uuid())

        # cleanup
        self._vnc_lib.data_center_interconnect_delete(id=dci_uuid)
        for lrname, lrobj in dict_lrs.items():
            self._vnc_lib.logical_router_delete(id=lrobj.get_uuid())
        for vminame, vmiobj in dict_vmis.items():
            self._vnc_lib.virtual_machine_interface_delete(
                id=vmiobj.get_uuid())
        for vnname, vnobj in dict_vns.items():
            self._vnc_lib.virtual_network_delete(
                fq_name=vnobj.get_fq_name())
        self._vnc_lib.network_ipam_delete(id=ipam_obj.uuid)
        for rpname, rpobj in dict_rps_created.items():
            self._vnc_lib.routing_policy_delete(id=rpobj.get_uuid())
        for prname, obj in dict_prs.items():
            self._vnc_lib.physical_router_delete(id=obj.get_uuid())
    # end test_dci_intrafabric

# end TestDataCenterInterconnect
