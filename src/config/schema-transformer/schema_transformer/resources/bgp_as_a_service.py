#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST
from vnc_api.gen.resource_xsd import BgpSession, BgpPeeringAttributes
from vnc_api.gen.resource_xsd import BgpRouterParams
from vnc_api.gen.resource_client import BgpRouter
from cfgm_common.exceptions import ResourceExhaustionError


class BgpAsAServiceST(ResourceBaseST):
    _dict = {}
    obj_type = 'bgp_as_a_service'
    prop_fields = ['bgpaas_ip_address', 'autonomous_system',
                   'bgpaas_session_attributes',
                   'bgpaas_suppress_route_advertisement',
                   'bgpaas_ipv4_mapped_ipv6_nexthop',
                   'bgpaas_shared']
    ref_fields = ['virtual_machine_interface', 'bgp_router']

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_machine_interfaces = set()
        self.bgp_routers = set()
        self.bgpaas_clients = {}
        self.bgpaas_ip_address = None
        self.autonomous_system = None
        self.bgpaas_session_attributes = None
        self.bgpaas_suppress_route_advertisement = None
        self.bgpaas_ipv4_mapped_ipv6_nexthop = None
        self.peering_attribs = None
        self.bgpaas_shared = False
        self.update(obj)
        self.set_bgpaas_clients()
    # end __init__

    def set_bgpaas_clients(self):
        if (self.bgpaas_shared and self.bgp_routers):
            bgpr = ResourceBaseST.get_obj_type_map().get('bgp_router').get(list(self.bgp_routers)[0])
            self.bgpaas_clients[self.obj.name] = bgpr.obj.get_fq_name_str()
        else:
            for bgp_router in self.bgp_routers:
                bgpr = ResourceBaseST.get_obj_type_map().get('bgp_router').get(bgp_router)
                if bgpr is None:
                    continue
                for vmi in self.virtual_machine_interfaces:
                    if vmi.split(':')[-1] == bgpr.obj.name:
                        self.bgpaas_clients[vmi] = bgpr.obj.get_fq_name_str()
                        break
    # end set_bgp_clients

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'bgpaas_session_attributes' in changed:
            session_attrib = self.bgpaas_session_attributes
            bgp_session = BgpSession()
            if session_attrib:
                bgp_session.attributes=[session_attrib]
            peering_attribs = BgpPeeringAttributes(session=[bgp_session])
            self.peering_attribs = BgpPeeringAttributes(session=[bgp_session])
        return changed
    # end update

    def evaluate(self):
        # If the BGP Service is shared, just create
        # one BGP Router.
        if self.obj.get_bgpaas_shared() == True:
            if set([self.obj.name]) - set(self.bgpaas_clients.keys()):
                self.create_bgp_router(self.name, shared=True)
        else:
            for name in (self.virtual_machine_interfaces -
                         set(self.bgpaas_clients.keys())):
                self.create_bgp_router(name)
    # end evaluate

    def create_bgp_router(self, name, shared=False):
        if shared:
            if not self.obj.bgpaas_ip_address:
                return
            vmi_refs = self.obj.get_virtual_machine_interface_refs()
            if not vmi_refs:
                return
            # all vmis will have link to this bgp-router
            vmis = [ResourceBaseST.get_obj_type_map().get('virtual_machine_interface').get(':'.join(ref['to']))
                       for ref in vmi_refs]
        else:
            # only current vmi will have link to this bgp-router
            vmi = ResourceBaseST.get_obj_type_map().get('virtual_machine_interface').get(name)
            if not vmi:
                self.virtual_machine_interfaces.discard(name)
                return
            vmis = [vmi]
        vn = ResourceBaseST.get_obj_type_map().get('virtual_network').get(vmis[0].virtual_network)
        if not vn:
            return
        ri = vn.get_primary_routing_instance()
        if not ri:
            return

        server_fq_name = ri.obj.get_fq_name_str() + ':bgpaas-server'
        server_router = ResourceBaseST.get_obj_type_map().get('bgp_router').get(server_fq_name)
        if not server_router:
            server_router = BgpRouter('bgpaas-server', parent_obj=ri.obj)
            params = BgpRouterParams(router_type='bgpaas-server')
            server_router.set_bgp_router_parameters(params)
            self._vnc_lib.bgp_router_create(server_router)
            ResourceBaseST.get_obj_type_map().get('bgp_router').locate(server_fq_name, server_router)
        else:
            server_router = server_router.obj

        bgpr_name = self.obj.name if shared else vmi.obj.name
        router_fq_name = ri.obj.get_fq_name_str() + ':' + bgpr_name
        bgpr = ResourceBaseST.get_obj_type_map().get('bgp_router').get(router_fq_name)
        create = False
        update = False
        src_port = None
        if not bgpr:
            bgp_router = BgpRouter(bgpr_name, parent_obj=ri.obj)
            create = True
            try:
                src_port = self._object_db.alloc_bgpaas_port(router_fq_name)
            except ResourceExhaustionError as e:
                self._logger.error("Cannot allocate BGPaaS port for %s:%s" % (
                    router_fq_name, str(e)))
                return
        else:
            bgp_router = self._vnc_lib.bgp_router_read(id=bgpr.obj.uuid)
            src_port = bgpr.source_port

        ip = self.bgpaas_ip_address or vmis[0].get_primary_instance_ip_address()
        params = BgpRouterParams(
            autonomous_system=int(self.autonomous_system) if self.autonomous_system else None,
            address=ip,
            identifier=ip,
            source_port=src_port,
            router_type='bgpaas-client')
        if bgp_router.get_bgp_router_parameters() != params:
            bgp_router.set_bgp_router(server_router, self.peering_attribs)
            update = True
        if create:
            bgp_router.set_bgp_router_parameters(params)
            bgp_router_id = self._vnc_lib.bgp_router_create(bgp_router)
            self.obj.add_bgp_router(bgp_router)
            self._vnc_lib.bgp_as_a_service_update(self.obj)
            bgpr = ResourceBaseST.get_obj_type_map().get('bgp_router').locate(router_fq_name)
        elif update:
            self._vnc_lib.bgp_router_update(bgp_router)
            bgp_router_id = bgp_router.uuid

        if shared:
            self.bgpaas_clients[self.obj.name] = router_fq_name
        else:
            self.bgpaas_clients[name] = router_fq_name
    # end create_bgp_router

# end class BgpAsAServiceST
