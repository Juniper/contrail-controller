#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

from __future__ import absolute_import

from builtins import range

from gevent import monkey
from gevent import sleep
from vnc_api.vnc_api import Bgpvpn
from vnc_api.vnc_api import RouteTargetList

from .test_case import STTestCase
from .test_route_target import VerifyRouteTarget


monkey.patch_all()


class TestBgpvpnWithVirtualNetwork(STTestCase, VerifyRouteTarget):
    def test_associating_bgpvpn(self):
        # Create one virtual network
        vn = self.create_virtual_network('vn-%s' % self.id(), '10.0.0.0/24')
        # Create two bgpvpn with route targets
        for idx in range(2):
            bgpvpn = Bgpvpn('bgpvpn%d-%s' % (idx, self.id()))
            bgpvpn.set_route_target_list(
                RouteTargetList(['target:%d:0' % idx]))
            bgpvpn.set_import_route_target_list(
                RouteTargetList(['target:%d:1' % idx]))
            bgpvpn.set_export_route_target_list(
                RouteTargetList(['target:%d:2' % idx]))
            self._vnc_lib.bgpvpn_create(bgpvpn)
            vn.add_bgpvpn(bgpvpn)
            self._vnc_lib.virtual_network_update(vn)

        # Check bgpvpn's route targets are correctly associated to virtual
        # network's primary routing instances.
        # Check imported and exported route targets
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:0:0')
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:0:0')

        # Check imported route targets
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:0:1', exim='import')
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:0:1', exim='import')

        # Check exported route targets
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:0:2', exim='export')
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:0:2', exim='export')

    def test_route_target_removed_when_resource_deleted(self):
        # Create two virtual networks
        vn1 = self.create_virtual_network('vn1-%s' % self.id(), '10.0.0.0/24')
        vn2 = self.create_virtual_network('vn2-%s' % self.id(), '10.0.1.0/24')
        # Create one bgpvpn with route target
        bgpvpn = Bgpvpn('bgpvpn-%s' % self.id())
        rt_name = 'target:200:1'
        bgpvpn.set_route_target_list(RouteTargetList([rt_name]))
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

        # Remove last associated virtual network
        self._vnc_lib.virtual_network_delete(id=vn1.uuid)
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn1))
        # Check the bgpvpn's route target was also deleted
        self.check_rt_is_deleted(rt_name)

    def test_updating_bgpvpn(self):
        # Create one virtual network
        vn = self.create_virtual_network('vn-%s' % self.id(), '10.0.0.0/24')
        # Create one bgpvpn without any route target associated to the virtual
        # network
        bgpvpn = Bgpvpn('bgpvpn-%s' % self.id())
        bgpvpn_id = self._vnc_lib.bgpvpn_create(bgpvpn)
        bgpvpn = self._vnc_lib.bgpvpn_read(id=bgpvpn_id)
        vn.add_bgpvpn(bgpvpn)
        self._vnc_lib.virtual_network_update(vn)

        rt_name = 'target:3:1'

        # Check set/unset import and export route target
        bgpvpn.set_route_target_list(RouteTargetList([rt_name]))
        self._vnc_lib.bgpvpn_update(bgpvpn)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name)
        bgpvpn.set_route_target_list(RouteTargetList())
        self._vnc_lib.bgpvpn_update(bgpvpn)
        sleep(1)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, is_present=False)
        self.check_rt_is_deleted(rt_name)

        # Check set/unset import route target
        bgpvpn.set_import_route_target_list(RouteTargetList([rt_name]))
        self._vnc_lib.bgpvpn_update(bgpvpn)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, exim='import')
        bgpvpn.set_import_route_target_list(RouteTargetList())
        self._vnc_lib.bgpvpn_update(bgpvpn)
        sleep(1)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, is_present=False)
        self.check_rt_is_deleted(rt_name)

        # Check set/unset export route target
        bgpvpn.set_export_route_target_list(RouteTargetList([rt_name]))
        self._vnc_lib.bgpvpn_update(bgpvpn)
        sleep(1)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, exim='export')
        bgpvpn.set_export_route_target_list(RouteTargetList())
        self._vnc_lib.bgpvpn_update(bgpvpn)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, is_present=False)
        self.check_rt_is_deleted(rt_name)

    def test_route_target_overlapping(self):
        rt_name = 'target:4:1'
        # Create one virtual network and set a route target in its route
        # target list
        vn = self.create_virtual_network('vn-%s' % self.id(), '10.0.0.0/24')
        vn.set_route_target_list(RouteTargetList([rt_name]))
        self._vnc_lib.virtual_network_update(vn)
        # Create one bgpvpn, set the same route target in its route target list
        # and associate it to the virtual network
        bgpvpn = Bgpvpn('bgpvpn-%s' % self.id())
        bgpvpn.set_route_target_list(RouteTargetList([rt_name]))
        bgpvpn_id = self._vnc_lib.bgpvpn_create(bgpvpn)
        bgpvpn = self._vnc_lib.bgpvpn_read(id=bgpvpn_id)
        vn.add_bgpvpn(bgpvpn)
        self._vnc_lib.virtual_network_update(vn)

        # Check the route target is set on the virtual network's routing
        # instance
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name)

        # Remove the route target from the bgpvpn' route target list and check
        # the route target is still set on the virtual network's routing
        # instance
        bgpvpn.set_route_target_list(RouteTargetList())
        self._vnc_lib.bgpvpn_update(bgpvpn)
        sleep(1)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name)

        # Remove the route target from the virtual network's route target list
        # and check is no more associate to the routing instance
        vn.set_route_target_list(RouteTargetList())
        self._vnc_lib.virtual_network_update(vn)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, is_present=False)
        self.check_rt_is_deleted(rt_name)


class TestBgpvpnWithLogicalRouter(STTestCase, VerifyRouteTarget):
    def test_associating_bgpvpn(self):
        # Create one logical with one interface on a virtual network
        lr, vns, _, _ = self.create_logical_router('lr-%s' % self.id())
        # We attached only one virtual network to the logical router
        vn = vns[0]
        # Create two bgpvpn with route targets
        for idx in range(2):
            bgpvpn = Bgpvpn('bgpvpn%d-%s' % (idx, self.id()))
            bgpvpn.set_route_target_list(
                RouteTargetList(['target:%d:0' % idx]))
            bgpvpn.set_import_route_target_list(
                RouteTargetList(['target:%d:1' % idx]))
            bgpvpn.set_export_route_target_list(
                RouteTargetList(['target:%d:2' % idx]))
            self._vnc_lib.bgpvpn_create(bgpvpn)
            lr.add_bgpvpn(bgpvpn)
            self._vnc_lib.logical_router_update(lr)

        # Check bgpvpn's route targets are correctly associated to virtual
        # network's primary routing instances of network attached to the
        # logical routers.
        # Check imported and exported route targets
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:0:0')
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:1:0')

        # Check imported route targets
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:0:1', exim='import')
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:1:1', exim='import')

        # Check exported route targets
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:0:2', exim='export')
        self.check_rt_in_ri(self.get_ri_name(vn), 'target:1:2', exim='export')

    def test_route_target_removed_when_resource_deleted(self):
        # Create two logical router with one virtual network attached
        lr1, vns1, _, _ = self.create_logical_router('lr1-%s' % self.id())
        lr2, vns2, _, _ = self.create_logical_router('lr2-%s' % self.id())
        # We attached only one virtual network per logical routers
        vn1 = vns1[0]
        vn2 = vns2[0]
        # Create one bgpvpn with route target
        bgpvpn = Bgpvpn('bgpvpn-%s' % self.id())
        rt_name = 'target:300:1'
        bgpvpn.set_route_target_list(RouteTargetList([rt_name]))
        bgpvpn_id = self._vnc_lib.bgpvpn_create(bgpvpn)
        bgpvpn = self._vnc_lib.bgpvpn_read(id=bgpvpn_id)
        # Associate bgpvpn to routers
        for lr in [lr1, lr2]:
            lr.add_bgpvpn(bgpvpn)
            self._vnc_lib.logical_router_update(lr)

        # Check route target is correctly removed when no more routers use
        # them.
        # Remove one of the associated logical router but keep attached virtual
        # network and its primary routing instance used to applied bgpvpn's
        # route target
        self._vnc_lib.logical_router_delete(id=lr2.uuid)
        # Chech that routing instance still there but the bgpvpn route target
        # were not anymore referenced
        self.check_vn_ri_state(self.get_ri_name(vn2))
        self.check_rt_in_ri(self.get_ri_name(vn2), rt_name, is_present=False)
        # Check the bgpvpn's route target is still referenced by
        # virtual network's primary routing instance which are still associated
        # to the first logical router
        self.check_rt_in_ri(self.get_ri_name(vn1), rt_name)

        # Remove last associated logical router
        self._vnc_lib.logical_router_delete(id=lr1.uuid)
        # Chech the routing instance still there but the bgpvpn route target
        # were not anymore referenced
        self.check_vn_ri_state(self.get_ri_name(vn1))
        self.check_rt_in_ri(self.get_ri_name(vn1), rt_name, is_present=False)
        # Check the bgpvpn's route target was also deleted
        self.check_rt_is_deleted(rt_name)

    def test_updating_bgpvpn(self):
        # Create one logical router with one private virtual network
        lr, vns, _, _ = self.create_logical_router('lr-%s' % self.id())
        # We attached only one virtual network to the logical router
        vn = vns[0]
        # Create one bgpvpn without any route target associated to the logical
        # router
        bgpvpn = Bgpvpn('bgpvpn-%s' % self.id())
        bgpvpn_id = self._vnc_lib.bgpvpn_create(bgpvpn)
        bgpvpn = self._vnc_lib.bgpvpn_read(id=bgpvpn_id)
        lr.add_bgpvpn(bgpvpn)
        self._vnc_lib.logical_router_update(lr)

        rt_name = 'target:3:1'

        # Check set/unset import and export route target
        bgpvpn.set_route_target_list(RouteTargetList([rt_name]))
        self._vnc_lib.bgpvpn_update(bgpvpn)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name)
        bgpvpn.set_route_target_list(RouteTargetList())
        self._vnc_lib.bgpvpn_update(bgpvpn)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, is_present=False)
        self.check_rt_is_deleted(rt_name)

        # Check set/unset import route target
        bgpvpn.set_import_route_target_list(RouteTargetList([rt_name]))
        self._vnc_lib.bgpvpn_update(bgpvpn)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, exim='import')
        bgpvpn.set_import_route_target_list(RouteTargetList())
        self._vnc_lib.bgpvpn_update(bgpvpn)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, is_present=False)
        self.check_rt_is_deleted(rt_name)

        # Check set/unset export route target
        bgpvpn.set_export_route_target_list(RouteTargetList([rt_name]))
        self._vnc_lib.bgpvpn_update(bgpvpn)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, exim='export')
        bgpvpn.set_export_route_target_list(RouteTargetList())
        self._vnc_lib.bgpvpn_update(bgpvpn)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, is_present=False)
        self.check_rt_is_deleted(rt_name)

    def test_route_target_overlapping(self):
        rt_name = 'target:4:1'
        # Create one logical router with one private virtual network and set a
        # route target in its configured route target list
        lr, vns, _, _ = self.create_logical_router('lr-%s' % self.id())
        # We attached only one virtual network to the logical router
        vn = vns[0]
        lr.set_configured_route_target_list(RouteTargetList([rt_name]))
        self._vnc_lib.logical_router_update(lr)
        # Create one bgpvpn, set the same route target in its route target list
        # and associate it to the logical router
        bgpvpn = Bgpvpn('bgpvpn-%s' % self.id())
        bgpvpn.set_route_target_list(RouteTargetList([rt_name]))
        bgpvpn_id = self._vnc_lib.bgpvpn_create(bgpvpn)
        bgpvpn = self._vnc_lib.bgpvpn_read(id=bgpvpn_id)
        lr.add_bgpvpn(bgpvpn)
        self._vnc_lib.logical_router_update(lr)

        # Check the route target is set on the virtual network's routing
        # instance
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name)

        # Remove the route target from the bgpvpn' route target list and check
        # the route target is still set on the virtual network's routing
        # instance
        bgpvpn.set_route_target_list(RouteTargetList())
        self._vnc_lib.bgpvpn_update(bgpvpn)
        sleep(1)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name)

        # Remove the route target from the logical router's configured route
        # target list and check is no more associate to the routing instance
        lr.set_configured_route_target_list(RouteTargetList())
        self._vnc_lib.logical_router_update(lr)
        self.check_rt_in_ri(self.get_ri_name(vn), rt_name, is_present=False)
        self.check_rt_is_deleted(rt_name)
