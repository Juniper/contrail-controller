#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST

class BgpvpnST(ResourceBaseST):
    _dict = {}
    obj_type = 'bgpvpn'
    prop_fields = ['route_target_list', 'import_route_target_list',
                   'export_route_target_list']

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_networks = set()
        self.logical_routers = set()
        self.rt_list = set()
        self.import_rt_list = set()
        self.export_rt_list = set()
        self.update(obj)
        self.update_multiple_refs('virtual_network', self.obj)
        self.update_multiple_refs('logical_router', self.obj)

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if set(self.prop_fields) & set(changed):
            self.get_route_target_lists()
        return changed

    def delete_obj(self):
        self.update_multiple_refs('virtual_network', {})
        self.update_multiple_refs('logical_router', {})

    def get_route_target_lists(self):
        old_lists = (self.rt_list, self.import_rt_list, self.export_rt_list)
        rt_list = self.obj.get_route_target_list()
        if rt_list:
            self.rt_list = set(rt_list.get_route_target())
        else:
            self.rt_list = set()
        rt_list = self.obj.get_import_route_target_list()
        if rt_list:
            self.import_rt_list = set(rt_list.get_route_target())
        else:
            self.import_rt_list = set()
        rt_list = self.obj.get_export_route_target_list()
        if rt_list:
            self.export_rt_list = set(rt_list.get_route_target())
        else:
            self.export_rt_list = set()

        # if any RT exists in both import and export, just add it to rt_list
        self.rt_list |= self.import_rt_list & self.export_rt_list
        # if any RT exists in rt_list, remove it from import/export lists
        self.import_rt_list -= self.rt_list
        self.export_rt_list -= self.rt_list
        return old_lists != (self.rt_list, self.import_rt_list,
                             self.export_rt_list)

    def handle_st_object_req(self):
        resp = super(BgpvpnST, self).handle_st_object_req()
        resp.obj_refs = [
            self._get_sandesh_ref_list('virtual_network'),
            self._get_sandesh_ref_list('logical_router'),
        ]
        return resp
# end BgpvpnST