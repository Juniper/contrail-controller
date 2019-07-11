#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST
from vnc_api.gen.resource_client import RouteTarget

from cfgm_common.exceptions import NoIdError


class RouteTargetST(ResourceBaseST):
    _dict = {}
    obj_type = 'route_target'

    @classmethod
    def reinit(cls):
        for obj in cls.list_vnc_obj():
            try:
                if (obj.get_routing_instance_back_refs() or
                        obj.get_logical_router_back_refs()):
                    cls.locate(obj.get_fq_name_str(), obj)
                else:
                    cls.delete_vnc_obj(obj.get_fq_name_str())
            except Exception as e:
                cls._logger.error("Error in reinit for %s %s: %s" % (
                    cls.obj_type, obj.get_fq_name_str(), str(e)))
        for ri, val in cls._object_db._rt_cf.get_range():
            rt = val['rtgt_num']
            asn = ResourceBaseST.get_obj_type_map().get('global_system_config').get_autonomous_system()
            rt_key = "target:%s:%s" % (asn, rt)
            if rt_key not in cls:
                cls._object_db.free_route_target(ri, asn)

                # This is to handle upgrade scenarios.
                # In case we upgrade to a release containing support to 4 Byte ASN
                # Once all the RTs are recreated in ZK in their new path, delete
                # the old path for RTs in ZK
            cls._object_db.delete_route_target_directory('%s%s'
                % (cls._object_db._zk_path_pfx, "/id/bgp/route-targets"))
    # end reinit

    def __init__(self, rt_key, obj=None):
        self.name = rt_key
        try:
            self.obj = obj or self.read_vnc_obj(fq_name=[rt_key])
        except NoIdError:
            self.obj = RouteTarget(rt_key)
            self._vnc_lib.route_target_create(self.obj)
    # end __init__

    def update(self, obj=None):
        return False

    @classmethod
    def delete_vnc_obj(cls, key):
        try:
            cls._vnc_lib.route_target_delete(fq_name=[key])
        except NoIdError:
            pass
        cls._dict.pop(key, None)
    # end delete_vnc_obj
# end RoutTargetST
