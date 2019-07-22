#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST


class PortTupleST(ResourceBaseST):
    _dict = {}
    obj_type = 'port_tuple'

    def __init__(self, name, obj=None):
        self.name = name
        self.service_instance = None
        self.virtual_machine_interfaces = set()
        self.update(obj)
        self.uuid = self.obj.uuid
        self.add_to_parent(self.obj)
        self.update_multiple_refs('virtual_machine_interface', self.obj)
    # end __init__

    def update(self, obj=None):
        self.obj = obj or self.read_vnc_obj(fq_name=self.name)
        return False
    # end update

    def delete_obj(self):
        self.update_multiple_refs('virtual_machine_interface', {})
        self.remove_from_parent()
    # end delete_obj

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
        resp = super(PortTupleST, self).handle_st_object_req()
        resp.obj_refs = [
            self._get_sandesh_ref_list('virtual_machine_interface'),
            self._get_sandesh_ref_list('service_instance'),
        ]
        return resp
    # end handle_st_object_req
# end PortTupleST
