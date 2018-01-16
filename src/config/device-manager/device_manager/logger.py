# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
Device Manager monitor logger
"""

from sandesh_common.vns.ttypes import Module
from cfgm_common.vnc_logger import ConfigServiceLogger

from db import BgpRouterDM, PhysicalRouterDM, PhysicalInterfaceDM, \
     LogicalInterfaceDM, VirtualNetworkDM, RoutingInstanceDM, \
     VirtualMachineInterfaceDM
from sandesh.dm_introspect import ttypes as sandesh


class DeviceManagerLogger(ConfigServiceLogger):

    def __init__(self, args=None, http_server_port=None):
        module = Module.DEVICE_MANAGER
        module_pkg = "device_manager"
        self.context = "device_manager"
        super(DeviceManagerLogger, self).__init__(
                module, module_pkg, args, http_server_port)

    def sandesh_init(self, http_server_port=None):
        super(DeviceManagerLogger, self).sandesh_init(http_server_port)
        self._sandesh.trace_buffer_create(name="MessageBusNotifyTraceBuf",
                                          size=1000)

    def redefine_sandesh_handles(self):
        sandesh.BgpRouterList.handle_request = \
            self.sandesh_bgp_handle_request
        sandesh.PhysicalRouterList.handle_request = \
            self.sandesh_pr_handle_request
        sandesh.PhysicalInterfaceList.handle_request = \
            self.sandesh_pi_handle_request
        sandesh.LogicalInterfaceList.handle_request = \
            self.sandesh_li_handle_request
        sandesh.VirtualNetworkList.handle_request = \
            self.sandesh_vn_handle_request
        sandesh.VirtualMachineInterfaceList.handle_request = \
            self.sandesh_vmi_handle_request
        sandesh.RoutingInstanceList.handle_request = \
            self.sandesh_ri_handle_request

    def sandesh_bgp_build(self, bgp_router):
        return sandesh.BgpRouter(name=bgp_router.name, uuid=bgp_router.uuid,
                                 peers=bgp_router.bgp_routers,
                                 physical_router=bgp_router.physical_router)

    def sandesh_bgp_handle_request(self, req):
        # Return the list of BGP routers
        resp = sandesh.BgpRouterListResp(bgp_routers=[])
        if req.name_or_uuid is None:
            for router in BgpRouterDM.values():
                sandesh_router = self.sandesh_bgp_build(router)
                resp.bgp_routers.append(sandesh_router)
        else:
            router = BgpRouterDM.find_by_name_or_uuid(req.name_or_uuid)
            if router:
                sandesh_router = self.sandesh_bgp_build(router)
                resp.bgp_routers.append(sandesh_router)
        resp.response(req.context())
    # end sandesh_bgp_handle_request

    def sandesh_pr_build(self, pr):
        return sandesh.PhysicalRouter(name=pr.name, uuid=pr.uuid,
                                 bgp_router=pr.bgp_router,
                                 physical_interfaces=pr.physical_interfaces,
                                 logical_interfaces=pr.logical_interfaces,
                                 virtual_networks=pr.virtual_networks)

    def sandesh_pr_handle_request(self, req):
        # Return the list of PR routers
        resp = sandesh.PhysicalRouterListResp(physical_routers=[])
        if req.name_or_uuid is None:
            for router in PhysicalRouterDM.values():
                sandesh_router = self.sandesh_pr_build(router)
                resp.physical_routers.append(sandesh_router)
        else:
            router = PhysicalRouterDM.find_by_name_or_uuid(req.name_or_uuid)
            if router:
                sandesh_router = self.sandesh_pr_build(router)
                resp.physical_routers.append(sandesh_router)
        resp.response(req.context())
    # end sandesh_pr_handle_request

    def sandesh_pi_build(self, pi):
        return sandesh.PhysicalInterface(name=pi.name, uuid=pi.uuid,
                                 logical_interfaces=pi.logical_interfaces)

    def sandesh_pi_handle_request(self, req):
        # Return the list of PR routers
        resp = sandesh.PhysicalInterfaceListResp(physical_interfaces=[])
        if req.name_or_uuid is None:
            for pi in PhysicalInterfaceDM.values():
                sandesh_pi = self.sandesh_pi_build(pi)
                resp.physical_interfaces.append(sandesh_pi)
        else:
            pi = PhysicalInterfaceDM.find_by_name_or_uuid(req.name_or_uuid)
            if pi:
                sandesh_pi = self.sandesh_pi_build(pi)
                resp.physical_interfaces.append(sandesh_pi)
        resp.response(req.context())
    # end sandesh_pi_handle_request

    def sandesh_li_build(self, li):
        return sandesh.LogicalInterface(name=li.name, uuid=li.uuid,
                                 physical_interface=li.physical_interface,
                                 virtual_machine_interface=li.virtual_machine_interface)

    def sandesh_li_handle_request(self, req):
        # Return the list of PR routers
        resp = sandesh.LogicalInterfaceListResp(logical_interfaces=[])
        if req.name_or_uuid is None:
            for li in LogicalInterfaceDM.values():
                sandesh_li = self.sandesh_li_build(li)
                resp.logical_interfaces.append(sandesh_li)
        else:
            li = LogicalInterfaceDM.find_by_name_or_uuid(req.name_or_uuid)
            if li:
                sandesh_li = self.sandesh_li_build(li)
                resp.logical_interfaces.append(sandesh_li)
        resp.response(req.context())
    # end sandesh_li_handle_request

    def sandesh_vn_build(self, vn):
        return sandesh.VirtualNetwork(name=vn.name, uuid=vn.uuid,
                                 routing_instances=vn.routing_instances,
                                 virtual_machine_interfaces=vn.virtual_machine_interfaces,
                                 physical_routers=vn.physical_routers)

    def sandesh_vn_handle_request(self, req):
        # Return the vnst of PR routers
        resp = sandesh.VirtualNetworkListResp(virtual_networks=[])
        if req.name_or_uuid is None:
            for vn in VirtualNetworkDM.values():
                sandesh_vn = self.sandesh_vn_build(vn)
                resp.virtual_networks.append(sandesh_vn)
        else:
            vn = VirtualNetworkDM.find_by_name_or_uuid(req.name_or_uuid)
            if vn:
                sandesh_vn = self.sandesh_vn_build(vn)
                resp.virtual_networks.append(sandesh_vn)
        resp.response(req.context())
    # end sandesh_vn_handle_request

    def sandesh_vmi_build(self, vmi):
        return sandesh.VirtualMachineInterface(name=vmi.name, uuid=vmi.uuid,
                                 logical_interface=vmi.logical_interface,
                                 virtual_network=vmi.virtual_network)

    def sandesh_vmi_handle_request(self, req):
        # Return the set of VMIs
        resp = sandesh.VirtualMachineInterfaceListResp(virtual_machine_interfaces=[])
        if req.name_or_uuid is None:
            for vmi in VirtualMachineInterfaceDM.values():
                sandesh_vmi = self.sandesh_vmi_build(vmi)
                resp.virtual_machine_interfaces.append(sandesh_vmi)
        else:
            vmi = VirtualMachineInterfaceDM.find_by_name_or_uuid(req.name_or_uuid)
            if vmi:
                sandesh_vmi = self.sandesh_vmi_build(vmi)
                resp.virtual_machine_interfaces.append(sandesh_vmi)
        resp.response(req.context())
    # end sandesh_vmi_handle_request

    def sandesh_ri_build(self, ri):
        return sandesh.RoutingInstance(name=ri.name, uuid=ri.uuid,
                                 virtual_network=ri.virtual_network)

    def sandesh_ri_handle_request(self, req):
        # Return the set of VMIs
        resp = sandesh.RoutingInstanceListResp(routing_instances=[])
        if req.name_or_uuid is None:
            for ri in RoutingInstanceDM.values():
                sandesh_ri = self.sandesh_ri_build(ri)
                resp.routing_instances.append(sandesh_ri)
        else:
            ri = RoutingInstanceDM.find_by_name_or_uuid(req.name_or_uuid)
            if ri:
                sandesh_ri = self.sandesh_ri_build(ri)
                resp.routing_instances.append(sandesh_ri)
        resp.response(req.context())
    # end sandesh_ri_handle_request

# end DeviceManagerLogger
