#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import BgpRouter

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class BgpRouterServer(ResourceMixin, BgpRouter):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        sub_cluster_ref = obj_dict.get('sub_cluster_refs')
        bgp_router_prop = obj_dict.get('bgp_router_parameters')
        if bgp_router_prop:
            asn = bgp_router_prop.get('autonomous_system')
        else:
            asn = None
        if sub_cluster_ref and asn:
            sub_cluster_obj = db_conn.uuid_to_obj_dict(
                obj_dict['sub_cluster_refs'][0]['uuid'])
            if asn != sub_cluster_obj['prop:sub_cluster_asn']:
                msg = "Subcluster asn and bgp asn should be same"
                return False, (400, msg)
        return True, ''

    @staticmethod
    def _validate_subcluster_dep(obj_dict, db_conn):
        if 'sub_cluster_refs' in obj_dict:
            if len(obj_dict['sub_cluster_refs']):
                sub_cluster_obj = db_conn.uuid_to_obj_dict(
                    obj_dict['sub_cluster_refs'][0]['uuid'])
                sub_cluster_asn = sub_cluster_obj['prop:sub_cluster_asn']
            else:
                sub_cluster_asn = None
        else:
            bgp_obj = db_conn.uuid_to_obj_dict(obj_dict['uuid'])
            sub_cluster_ref = ([key for key in bgp_obj.keys()
                                if key.startswith('ref:sub_cluster')])
            if len(sub_cluster_ref):
                sub_cluster_uuid = sub_cluster_ref[0].split(':')[-1]
                sub_cluster_obj = db_conn.uuid_to_obj_dict(sub_cluster_uuid)
                sub_cluster_asn = sub_cluster_obj.get('prop:sub_cluster_asn')
            else:
                sub_cluster_asn = None
        if sub_cluster_asn:
            if obj_dict.get('bgp_router_parameters', {}).get(
                    'autonomous_system'):
                asn = obj_dict['bgp_router_parameters'].get(
                    'autonomous_system')
            else:
                bgp_obj = db_conn.uuid_to_obj_dict(obj_dict['uuid'])
                asn = bgp_obj['prop:bgp_router_parameters'].get(
                    'autonomous_system')
            if asn != sub_cluster_asn:
                msg = "Subcluster asn and bgp asn should be same"
                return False, (400, msg)
        return True, ''

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn,
                       prop_collection_updates=None, ref_update=None):
        if ('sub_cluster_refs' in obj_dict or
                (obj_dict.get('bgp_router_parameters') and
                 obj_dict['bgp_router_parameters'].get('autonomous_system'))):
            return cls._validate_subcluster_dep(obj_dict, db_conn)
        return True, ''
