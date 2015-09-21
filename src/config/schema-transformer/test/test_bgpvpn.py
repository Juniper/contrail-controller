#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.vnc_api import Bgpvpn
from vnc_api.vnc_api import RouteTargetList

from test_case import STTestCase
from test_route_target import VerifyRouteTarget


class TestBgpvpnWithVirtualNetwork(STTestCase, VerifyRouteTarget):
    def test_associate_bgpvpn_to_virtual_networks(self):
        # Create one virtual network
        vn = self.create_virtual_network('vn-%s' % self.id(), '10.0.0.0/24')
        # Create two bgpvpn with route targets
        bgpvpns = []
        for idx in range(2):
            bgpvpn = Bgpvpn('bgpvpn%d-%s' % (idx, self.id()))
            bgpvpn.set_bgpvpn_route_target_list(
                RouteTargetList(['target:%d:1' % idx]))
            bgpvpn.set_bgpvpn_import_route_target_list(
                RouteTargetList(['target:%d:2' % idx]))
            bgpvpn.set_bgpvpn_export_route_target_list(
                RouteTargetList(['target:%d:3' % idx]))
            bgpvpn_id = self._vnc_lib.bgpvpn_create(bgpvpn)
            bgpvpns.append(self._vnc_lib.bgpvpn_read(id=bgpvpn_id))

        # Associate network to bgpvpns and check bgpvpn's route targets are
        # correctly associated to virtual network's primary routing instances
        for idx, bgpvpn in enumerate(bgpvpns):
            vn.add_bgpvpn(bgpvpn)
            self._vnc_lib.virtual_network_update(vn)

        # Check imported and exported route targets
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:1:1')
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:2:1')

        # Check imported route targets
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:1:2', exim='import')
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:2:2', exim='import')

        # Check exported route targets
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:1:3', exim='export')
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:2:3', exim='export')

    def test_route_target_removed_when_virtual_network_deleted(self):
        # Create two virtual networks
        vn1 = self.create_virtual_network('vn1-%s' % self.id(), '10.0.0.0/24')
        vn2 = self.create_virtual_network('vn2-%s' % self.id(), '10.0.1.0/24')
        # Create one bgpvpn with route target
        bgpvpn = Bgpvpn('bgpvpn1-%s' % self.id())
        rt_name = 'target:1:1'
        bgpvpn.set_bgpvpn_route_target_list(RouteTargetList([rt_name]))
        bgpvpn_id = self._vnc_lib.bgpvpn_create(bgpvpn)
        bgpvpn = self._vnc_lib.bgpvpn_read(id=bgpvpn_id)
        # Associate bgpvpn to networks
        for vn in [vn1, vn2]:
            vn.add_bgpvpn(bgpvpn)
            self._vnc_lib.virtual_network_update(vn)

        # Check route target is correctly removed when no more networks use
        # them.
        # Remove one of the associated virtual networks
        self._vnc_lib.virtual_network_delete(id=vn2.uuid)
        self.check_ri_is_deleted(self.get_ri_name(vn2))
        # Check the bgpvpn's route target is still referenced by
        # virtual network's primary routing instance which are still associated
        self.check_rt_in_ri(self.get_ri_name(vn1), rt_name)

        # Remove last associated virtual networks
        # Check the bgpvpn's route target was also deleted
        self._vnc_lib.virtual_network_delete(id=vn1.uuid)
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn1))
        self.check_rt_is_deleted(rt_name)

    def test_update_bgpvpn_associated_to_virtual_network(self):
        # Create one virtual network
        vn = self.create_virtual_network('vn-%s' % self.id(), '10.0.0.0/24')
        # Create one bgpvpn without any route target associated to one virtual
        # network
        bgpvpn = Bgpvpn('bgpvpn1-%s' % self.id())
        bgpvpn_id = self._vnc_lib.bgpvpn_create(bgpvpn)
        bgpvpn = self._vnc_lib.bgpvpn_read(id=bgpvpn_id)
        # bgpvpn.add_virtual_network(vn)
        # self._vnc_lib.bgpvpn_update(bgpvpn)
        vn.add_bgpvpn(bgpvpn)
        self._vnc_lib.virtual_network_update(vn)

        rt_name = 'target:1:1'

        # Check add and delete import and export route target
        bgpvpn.set_bgpvpn_route_target_list(RouteTargetList([rt_name]))
        self._vnc_lib.bgpvpn_update(bgpvpn)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name)
        bgpvpn.set_bgpvpn_route_target_list(RouteTargetList())
        self._vnc_lib.bgpvpn_update(bgpvpn)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, is_present=False)
        self.check_rt_is_deleted(rt_name)

        # Check add and delete import route target
        bgpvpn.set_bgpvpn_import_route_target_list(RouteTargetList([rt_name]))
        self._vnc_lib.bgpvpn_update(bgpvpn)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, exim='import')
        bgpvpn.set_bgpvpn_import_route_target_list(RouteTargetList())
        self._vnc_lib.bgpvpn_update(bgpvpn)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, is_present=False)
        self.check_rt_is_deleted(rt_name)

        # Check add and delete export route target
        bgpvpn.set_bgpvpn_export_route_target_list(RouteTargetList([rt_name]))
        self._vnc_lib.bgpvpn_update(bgpvpn)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, exim='export')
        bgpvpn.set_bgpvpn_export_route_target_list(RouteTargetList())
        self._vnc_lib.bgpvpn_update(bgpvpn)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, is_present=False)
        self.check_rt_is_deleted(rt_name)

