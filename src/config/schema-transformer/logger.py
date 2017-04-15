# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Schema Transformer monitor logger
"""

from sandesh_common.vns.ttypes import Module
from cfgm_common.vnc_logger import ConfigServiceLogger

from schema_transformer.config_db import DBBaseST, VirtualNetworkST,\
        RoutingInstanceST, ServiceChain
from schema_transformer.sandesh.st_introspect import ttypes as sandesh


class SchemaTransformerLogger(ConfigServiceLogger):

    def __init__(self, discovery, args=None, http_server_port=None):
        module = Module.SCHEMA_TRANSFORMER
        module_pkg = "schema_transformer"
        self.context = "to_bgp"
        super(SchemaTransformerLogger, self).__init__(
                discovery, module, module_pkg, args, http_server_port)

    def sandesh_init(self, http_server_port=None):
        super(SchemaTransformerLogger, self).sandesh_init(http_server_port)
        self._sandesh.trace_buffer_create(name="MessageBusNotifyTraceBuf",
                                          size=1000)

    def redefine_sandesh_handles(self):
        sandesh.VnList.handle_request = self.sandesh_vn_handle_request
        sandesh.RoutingInstanceList.handle_request = \
            self.sandesh_ri_handle_request
        sandesh.ServiceChainList.handle_request = \
            self.sandesh_sc_handle_request
        sandesh.StObjectReq.handle_request = \
            self.sandesh_st_object_handle_request

    def sandesh_ri_build(self, vn_name, ri_name):
        vn = VirtualNetworkST.get(vn_name)
        sandesh_ri_list = []
        for riname in vn.routing_instances:
            ri = RoutingInstanceST.get(riname)
            sandesh_ri = sandesh.RoutingInstance(name=ri.obj.get_fq_name_str())
            sandesh_ri.service_chain = ri.service_chain
            sandesh_ri.connections = list(ri.connections)
            sandesh_ri_list.append(sandesh_ri)
        return sandesh_ri_list
    # end sandesh_ri_build

    def sandesh_ri_handle_request(self, req):
        # Return the list of VNs
        ri_resp = sandesh.RoutingInstanceListResp(routing_instances=[])
        if req.vn_name is None:
            for vn in VirtualNetworkST:
                sandesh_ri = self.sandesh_ri_build(vn, req.ri_name)
                ri_resp.routing_instances.extend(sandesh_ri)
        elif req.vn_name in VirtualNetworkST:
            sandesh_ri = self.sandesh_ri_build(req.vn_name, req.ri_name)
            ri_resp.routing_instances.extend(sandesh_ri)
        ri_resp.response(req.context())
    # end sandesh_ri_handle_request

    def sandesh_vn_build(self, vn_name):
        vn = VirtualNetworkST.get(vn_name)
        sandesh_vn = sandesh.VirtualNetwork(name=vn_name)
        sandesh_vn.policies = vn.network_policys.keys()
        sandesh_vn.connections = list(vn.connections)
        sandesh_vn.routing_instances = vn.routing_instances
        if vn.acl:
            sandesh_vn.acl = vn.acl.get_fq_name_str()
        if vn.dynamic_acl:
            sandesh_vn.dynamic_acl = vn.dynamic_acl.get_fq_name_str()

        return sandesh_vn
    # end sandesh_vn_build

    def sandesh_vn_handle_request(self, req):
        # Return the list of VNs
        vn_resp = sandesh.VnListResp(vn_names=[])
        if req.vn_name is None:
            for vn in VirtualNetworkST:
                sandesh_vn = self.sandesh_vn_build(vn)
                vn_resp.vn_names.append(sandesh_vn)
        elif req.vn_name in VirtualNetworkST:
            sandesh_vn = self.sandesh_vn_build(req.vn_name)
            vn_resp.vn_names.append(sandesh_vn)
        vn_resp.response(req.context())
    # end sandesh_vn_handle_request

    def sandesh_sc_handle_request(self, req):
        sc_resp = sandesh.ServiceChainListResp(service_chains=[])
        if req.sc_name is None:
            for sc in ServiceChain.values():
                sandesh_sc = sc.build_introspect()
                sc_resp.service_chains.append(sandesh_sc)
        elif req.sc_name in ServiceChain:
            sandesh_sc = ServiceChain.get(req.sc_name).build_introspect()
            sc_resp.service_chains.append(sandesh_sc)
        sc_resp.response(req.context())
    # end sandesh_sc_handle_request

    def sandesh_st_object_handle_request(self, req):
        st_resp = sandesh.StObjectListResp(objects=[])
        obj_type_map = DBBaseST.get_obj_type_map()
        if req.object_type is not None:
            if req.object_type not in obj_type_map:
                return st_resp
            obj_cls_list = [obj_type_map[req.object_type]]
        else:
            obj_cls_list = obj_type_map.values()
        for obj_cls in obj_cls_list:
            id_or_name = req.object_id_or_fq_name
            if id_or_name:
                obj = obj_cls.get(id_or_name) or \
                        obj_cls.get_by_uuid(id_or_name)
                if obj is None:
                    continue
                st_resp.objects.append(obj.handle_st_object_req())
            else:
                for obj in obj_cls.values():
                    st_resp.objects.append(obj.handle_st_object_req())
        st_resp.response(req.context())
    # end sandesh_st_object_handle_request
