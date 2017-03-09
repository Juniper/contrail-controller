# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
Device Manager monitor logger
"""

from sandesh_common.vns.ttypes import Module
from cfgm_common.vnc_logger import ConfigServiceLogger

from db import BgpRouterDM
from sandesh.dm_introspect import ttypes as sandesh

class DeviceManagerLogger(ConfigServiceLogger):

    def __init__(self, args=None):
        module = Module.DEVICE_MANAGER
        module_pkg = "device_manager"
        self.context = "device_manager"
        super(DeviceManagerLogger, self).__init__(
                module, module_pkg, args)

    def sandesh_init(self):
        super(DeviceManagerLogger, self).sandesh_init()
        self._sandesh.trace_buffer_create(name="MessageBusNotifyTraceBuf",
                                          size=1000)

    def redefine_sandesh_handles(self):
        sandesh.BgpRouterList.handle_request = \
            self.sandesh_bgp_handle_request

    def sandesh_bgp_build(self, bgp_router):
        return sandesh.BgpRouter(name=bgp_router.name, uuid=bgp_router.uuid,
                                 peers=bgp_router.bgp_routers,
                                 physical_router=bgp_router.physical_router)

    def sandesh_bgp_handle_request(self, req):
        # Return the list of BGP routers
        resp = sandesh.BgpRouterListResp(bgp_routers=[])
        if req.name_or_uuid is None:
            for router in BgpRouterDM:
                sandesh_router = self.sandesh_bgp_build()
                resp.bgp_routers.extend(sandesh_router)
        else:
            router = BgpRouterDM.find_by_name_or_uuid(req.name_or_uuid)
            if router:
                sandesh_router = router.sandesh_bgp_build()
                resp.bgp_routers.extend(sandesh_router)
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
            for router in PhyscialRouterDM:
                sandesh_router = self.sandesh_pr_build()
                resp.physical_routers.extend(sandesh_router)
        else:
            router = PhysicalRouterDM.find_by_name_or_uuid(req.name_or_uuid)
            if router:
                sandesh_router = router.sandesh_pr_build()
                resp.physical_routers.extend(sandesh_router)
        resp.response(req.context())
    # end sandesh_pr_handle_request

# end DeviceManagerLogger
