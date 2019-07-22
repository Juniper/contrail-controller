#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST
from schema_transformer.sandesh.st_introspect import ttypes as sandesh


class VirtualMachineST(ResourceBaseST):
    _dict = {}
    obj_type = 'virtual_machine'
    ref_fields = ['service_instance']
    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_machine_interfaces = set()
        self.service_instance = None
        self.update(obj)
        self.uuid = self.obj.uuid
        self.update_multiple_refs('virtual_machine_interface', self.obj)
    # end __init__

    def get_vmi_by_service_type(self, service_type):
        for vmi_name in self.virtual_machine_interfaces:
            vmi = ResourceBaseST.get_obj_type_map().get('virtual_machine_interface').get(vmi_name)
            if vmi and vmi.service_interface_type == service_type:
                return vmi
        return None

    def get_service_mode(self):
        if self.service_instance is None:
            return None
        si_obj = ResourceBaseST.get_obj_type_map().get('service_instance').get(self.service_instance)
        if si_obj is None:
            self._logger.error("service instance %s not found"
                               % self.service_instance)
            return None
        return si_obj.get_service_mode()
    # end get_service_mode

    def handle_st_object_req(self):
        resp = super(VirtualMachineST, self).handle_st_object_req()
        resp.obj_refs.extend([
            self._get_sandesh_ref_list('virtual_machine_interface'),
        ])
        resp.properties.extend([
            sandesh.PropList('service_mode', self.get_service_mode()),
        ])
        return resp
    # end handle_st_object_req
# end VirtualMachineST
