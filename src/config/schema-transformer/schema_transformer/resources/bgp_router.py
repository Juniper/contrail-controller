#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST
from schema_transformer.sandesh.st_introspect import ttypes as sandesh
from vnc_api.gen.resource_xsd import AddressFamilies, BgpSessionAttributes
from vnc_api.gen.resource_xsd import BgpSession, BgpPeeringAttributes
from cfgm_common.exceptions import NoIdError, RefsExistError, BadRequest


class BgpRouterST(ResourceBaseST):
    _dict = {}
    obj_type = 'bgp_router'
    prop_fields = ['bgp_router_parameters']
    ref_fields = ['bgp_as_a_service', 'sub_cluster', 'physical_router']

    def __init__(self, name, obj=None):
        self.name = name
        self.asn = None
        self.bgp_as_a_service = None
        self.vendor = None
        self.identifier = None
        self.router_type = None
        self.source_port = None
        self.sub_cluster = None
        self.cluster_id = None
        self.physical_router = None
        self.update(obj)
        self.update_single_ref('bgp_as_a_service', self.obj)
    # end __init__

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'bgp_router_parameters' in changed:
            self.set_params(self.obj.get_bgp_router_parameters())
    # end update

    def delete_obj(self):
        self.update_single_ref('bgp_as_a_service', {})
        self.update_single_ref('physical_router', {})
        if self.router_type == 'bgpaas-client':
            self._object_db.free_bgpaas_port(self.source_port)
    # end delete_ref

    def is_cluster_id_changed(self, params):
        if ((self.cluster_id is None and params.cluster_id is not None)
            or (self.cluster_id is not None and params.cluster_id is None)):
            return True

        return False
    # end is_cluster_id_changed

    def set_params(self, params):
        self.vendor = (params.vendor or 'contrail').lower()
        self.identifier = params.identifier
        self.router_type = params.router_type
        self.source_port = params.source_port

        # to reduce the peering from full mesh to RR
        if self.is_cluster_id_changed(params):
            self.cluster_id = params.cluster_id
            self.update_full_mesh_to_rr_peering()

        if self.router_type not in ('bgpaas-client', 'bgpaas-server'):
            if self.vendor == 'contrail':
                self.update_global_asn(
                    ResourceBaseST.get_obj_type_map().\
			get('global_system_config').get_autonomous_system())
            else:
                self.update_autonomous_system(params.autonomous_system)
    # end set_params

    def update_global_asn(self, asn):
        if self.vendor != 'contrail' or self.asn == int(asn):
            return
        if self.router_type in ('bgpaas-client', 'bgpaas-server'):
            return
        router_obj = self.read_vnc_obj(fq_name=self.name)
        params = router_obj.get_bgp_router_parameters()
        if params.autonomous_system != int(asn):
            params.autonomous_system = int(asn)
            router_obj.set_bgp_router_parameters(params)
            self._vnc_lib.bgp_router_update(router_obj)
        self.update_autonomous_system(asn)
    # end update_global_asn

    def update_autonomous_system(self, asn):
        if self.asn == int(asn):
            return
        self.asn = int(asn)
        self.update_peering()
    # end update_autonomous_system

    def evaluate(self):
        if self.router_type == 'bgpaas-client':
            bgpaas = ResourceBaseST.get_obj_type_map().get('bgp_as_a_service').get(self.bgp_as_a_service)
            ret = self.update_bgpaas_client(bgpaas)
            if ret == -1:
                if bgpaas:
                    bgpaas.obj.del_bgp_router(self.obj)
                    try:
                        self._vnc_lib.bgp_as_a_service_update(bgpaas.obj)
                    except NoIdError:
                        pass
                vmis = self.obj.get_virtual_machine_interface_back_refs() or []
                for vmi in vmis:
                    try:
                        # remove bgp-router ref from vmi
                        self._vnc_lib.ref_update(obj_uuid=vmi['uuid'],
                                                 obj_type='virtual_machine_interface',
                                                 ref_uuid=self.obj.uuid,
                                                 ref_fq_name=self.obj.fq_name,
                                                 ref_type='bgp-router',
                                                 operation='DELETE')
                    except NoIdError:
                        pass
                try:
                    self._vnc_lib.bgp_router_delete(id=self.obj.uuid)
                    self.delete(self.name)
                except RefsExistError:
                    pass
            elif ret:
                self._vnc_lib.bgp_router_update(self.obj)
        elif self.router_type != 'bgpaas-server':
            self.update_peering()
    # end evaluate

    def update_bgpaas_client(self, bgpaas):
        if not bgpaas:
            return -1
        if bgpaas.bgpaas_shared:
            if (bgpaas.virtual_machine_interfaces and
                self.name in bgpaas.bgpaas_clients.values()):
                vmi_names = list(bgpaas.virtual_machine_interfaces)
                vmis = [ResourceBaseST.get_obj_type_map().get('virtual_machine_interface').get(vmi_name)
                        for vmi_name in vmi_names]
                vmi = vmis[0]
            elif self.name in bgpaas.bgpaas_clients.values():
                del bgpaas.bgpaas_clients[bgpaas.obj.name]
                return -1
            else:
                return -1
        else:
            for vmi_name, router in bgpaas.bgpaas_clients.items():
                if router == self.name:
                    break
            else:
                return -1
            if vmi_name not in bgpaas.virtual_machine_interfaces:
                del bgpaas.bgpaas_clients[vmi_name]
                return -1

            vmi = ResourceBaseST.get_obj_type_map().get('virtual_machine_interface').get(vmi_name)
            if vmi is None or vmi.virtual_network is None:
                del bgpaas.bgpaas_clients[vmi_name]
                return -1
            vn = ResourceBaseST.get_obj_type_map().get('virtual_network').get(vmi.virtual_network)
            if not vn or self.obj.get_parent_fq_name_str() != vn._default_ri_name:
                del bgpaas.bgpaas_clients[vmi_name]
                return -1
            vmi_names = [vmi_name]
            vmis = [vmi]

        update = False
        params = self.obj.get_bgp_router_parameters()
        if params.autonomous_system != int(bgpaas.autonomous_system):
            params.autonomous_system = int(bgpaas.autonomous_system)
            update = True
        ip = bgpaas.bgpaas_ip_address or vmi.get_primary_instance_ip_address()
        if params.address != ip:
            params.address = ip
            update = True
        if params.identifier != ip:
            params.identifier = ip
            update = True
        if bgpaas.bgpaas_suppress_route_advertisement:
            if params.gateway_address:
                params.gateway_address = None
                update = True
            if params.ipv6_gateway_address:
                params.ipv6_gateway_address = None
                update = True
        else:
            v4_gateway = vmi.get_v4_default_gateway()
            if params.gateway_address != v4_gateway:
                params.gateway_address = v4_gateway
                update = True
            if bgpaas.obj.bgpaas_ipv4_mapped_ipv6_nexthop:
                v6_gateway = vmi.get_ipv4_mapped_ipv6_gateway()
            else:
                v6_gateway = vmi.get_v6_default_gateway()
            if params.ipv6_gateway_address != v6_gateway:
                params.ipv6_gateway_address = v6_gateway
                update = True
        if update:
            self.obj.set_bgp_router_parameters(params)
        router_refs = self.obj.get_bgp_router_refs()
        if router_refs:
            peering_attribs = router_refs[0]['attr']
            if peering_attribs != bgpaas.peering_attribs:
                self.obj.set_bgp_router_list([router_refs[0]['to']],
                                             [bgpaas.peering_attribs])
                update = True

        old_refs = self.obj.get_virtual_machine_interface_back_refs() or []
        old_uuids = set([ref['uuid'] for ref in old_refs])
        new_uuids = set([vmi.obj.uuid for vmi in vmis])

        # add vmi->bgp-router link
        for vmi_id in new_uuids - old_uuids:
            self._vnc_lib.ref_update(obj_uuid=vmi_id,
                obj_type='virtual_machine_interface',
                ref_uuid=self.obj.uuid,
                ref_type='bgp_router',
                ref_fq_name=self.obj.get_fq_name_str(),
                operation='ADD')
        # remove vmi->bgp-router links for old vmi if any
        for vmi_id in old_uuids - new_uuids:
            self._vnc_lib.ref_update(obj_uuid=vmi_id,
                obj_type='virtual_machine_interface',
                ref_uuid=self.obj.uuid,
                ref_type='bgp_router',
                ref_fq_name=self.obj.get_fq_name_str(),
                operation='DELETE')
        if old_uuids != new_uuids:
            refs = [{'to': vmi.obj.fq_name, 'uuid': vmi.obj.uuid}
                       for vmi in vmis]
            self.obj.virtual_machine_interface_back_refs = refs
        return update
    # end update_bgpaas_client

    def _is_route_reflector_supported(self):
        cluster_rr_supported = False
        control_rr_supported = False
        for router in self._dict.values():
            if router.cluster_id:
                if router.router_type == 'control-node':
                    control_rr_supported = True
                else:
                    cluster_rr_supported = True
            if control_rr_supported and cluster_rr_supported:
                break
        return cluster_rr_supported, control_rr_supported
    # end _is_route_reflector_supported

    def skip_fabric_bgp_router_peering_add(self, router):

        phy_rtr_name = self.physical_router
        phy_rtr_peer_name = router.physical_router

        if phy_rtr_name and phy_rtr_peer_name:
            phy_rtr = ResourceBaseST.get_obj_type_map().get('physical_router').get(phy_rtr_name)
            fabric = phy_rtr.fabric

            phy_rtr_peer = ResourceBaseST.get_obj_type_map().get('physical_router').get(phy_rtr_peer_name)
            fabric_peer = phy_rtr_peer.fabric

            # Ignore peering if fabric of self-bgp-router and peer-bgp-router
            # are not the same
            if (fabric and fabric_peer and fabric != fabric_peer):
                return True

        return False
    # end skip_fabric_bgp_router_peering_add

    def skip_bgp_router_peering_add(self, router, cluster_rr_supported,
                                   control_rr_supported):
        # If there is no RR, always add peering in order to create full mesh.
        if not cluster_rr_supported and not control_rr_supported:
            return False

        # Always create peering between control-nodes until control-node can
        # be a route-reflector server (or bgp-router can support ermvpn afi)
        if (not control_rr_supported) and self.router_type == 'control-node' and \
                router.router_type == 'control-node':
            return False

        # Always create peeering between RRs in the same cluster for HA.
        if self.cluster_id and router.cluster_id:
            return self.cluster_id != router.cluster_id

        # Always create peering from/to route-reflector (server).
        if self.cluster_id or router.cluster_id:
            return self.skip_fabric_bgp_router_peering_add(router)

        # Only in this case can we opt to skip adding bgp-peering.
        return True
    # end skip_bgp_router_peering_add

    def update_full_mesh_to_rr_peering(self):
        for router in BgpRouterST.values():
            if router.name == self.name:
                continue
            router.update_peering()
    # end update_full_mesh_to_rr_peering

    def update_peering(self):
        if not ResourceBaseST.get_obj_type_map().\
                        get('global_system_config').get_ibgp_auto_mesh():
            return
        if self.router_type in ('bgpaas-server', 'bgpaas-client'):
            return

        fabric = None
        if self.physical_router:
            phy_rtr = ResourceBaseST.get_obj_type_map().get('physical_router').get(self.physical_router)
            fabric = phy_rtr.fabric

        global_asn = int(ResourceBaseST.get_obj_type_map().\
                        get('global_system_config').get_autonomous_system())
        # if it's a fabric or sub cluster bgp router, ignore
        # global asn check that we do to determine e-bgp router
        if (self.sub_cluster is None and fabric is None and
            self.asn != global_asn):
            return
        try:
            obj = self.read_vnc_obj(fq_name=self.name)
        except NoIdError as e:
            self._logger.error("NoIdError while reading bgp router "
                                   "%s: %s"%(self.name, str(e)))
            return

        cluster_rr_supported, control_rr_supported = \
                                     self._is_route_reflector_supported()
        peerings_set = set(':'.join(ref['to']) for ref in (obj.get_bgp_router_refs() or []))
        new_peerings_set = set()
        new_peerings_list = []
        new_peerings_attrs = []
        for router in self._dict.values():
            if router.name == self.name:
                continue
            if router.sub_cluster != self.sub_cluster:
                continue
            if router.router_type in ('bgpaas-server', 'bgpaas-client'):
                continue

            if self.skip_bgp_router_peering_add(router, cluster_rr_supported,
                                                control_rr_supported):
                continue

            af = AddressFamilies(family=[])
            bsa = BgpSessionAttributes(address_families=af)
            session = BgpSession(attributes=[bsa])
            attr = BgpPeeringAttributes(session=[session])
            new_peerings_set.add(router.name)
            router_fq_name = router.name.split(':')
            new_peerings_list.append(router_fq_name)
            new_peerings_attrs.append(attr)

        if new_peerings_set != peerings_set:
            try:
                obj.set_bgp_router_list(new_peerings_list, new_peerings_attrs)
                self._vnc_lib.bgp_router_update(obj)
            except NoIdError as e:
                self._logger.error("NoIdError while updating bgp router "
                                   "%s: %s"%(self.name, str(e)))
    # end update_peering

    def handle_st_object_req(self):
        resp = super(BgpRouterST, self).handle_st_object_req()
        resp.properties = [
            sandesh.PropList('asn', str(self.asn)),
            sandesh.PropList('vendor', self.vendor),
            sandesh.PropList('identifier', self.identifier),
        ]
        return resp
    # end handle_st_object_req
# end class BgpRouterST
