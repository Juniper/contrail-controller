#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common.vnc_db import DBBase
from schema_transformer.sandesh.st_introspect import ttypes as sandesh
from schema_transformer.utils import _pp_json_object


class ResourceBaseST(DBBase):
    obj_type = __name__
    _indexed_by_name = True
    ref_fields = []
    prop_fields = []

    def update(self, obj=None):
        return self.update_vnc_obj(obj)

    def delete_obj(self):
        for ref_field in self.ref_fields:
            self.update_refs(ref_field, {})

    def evaluate(self):
        # Implement in the derived class
        pass

    @classmethod
    def reinit(cls):
        for obj in cls.list_vnc_obj():
            try:
                cls.locate(obj.get_fq_name_str(), obj)
            except Exception as e:
                cls._logger.error("Error in reinit for %s %s: %s" % (
                    cls.obj_type, obj.get_fq_name_str(), str(e)))
    # end reinit

    @classmethod
    def get_obj_type_map(cls):
        module_base = [x for x in DBBase.__subclasses__()
                       if 'schema_transformer' in x.obj_type]
        return dict((x.obj_type, x) for x in module_base[0].__subclasses__())

    def handle_st_object_req(self):
        st_obj = sandesh.StObject(object_type=self.obj_type,
                                  object_fq_name=self.name)
        try:
            st_obj.object_uuid = self.obj.uuid
        except AttributeError:
            pass
        st_obj.obj_refs = []
        for field in self.ref_fields:
            if self._get_sandesh_ref_list(field):
                st_obj.obj_refs.append(self._get_sandesh_ref_list(field))

        st_obj.properties = [
            sandesh.PropList(field,
                             _pp_json_object(getattr(self, field)))
            for field in self.prop_fields
            if hasattr(self, field)]
        return st_obj

    def _get_sandesh_ref_list(self, ref_type):
        try:
            ref = getattr(self, ref_type)
            refs = [ref] if ref else []
        except AttributeError:
            try:
                refs = getattr(self, ref_type + 's')
                if isinstance(refs, dict):
                    refs = refs.keys()
            except AttributeError:
                return
        return sandesh.RefList(ref_type, refs)
# end ResourceBaseST
