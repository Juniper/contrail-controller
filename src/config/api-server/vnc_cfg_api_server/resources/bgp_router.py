#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import BgpRouter

from vnc_cfg_api_server.resources._resource_base import ResourceMixin
from vnc_cfg_api_server.resources.global_system_config import\
    GlobalSystemConfigServer


class BgpRouterServer(ResourceMixin, BgpRouter):
    @classmethod
    def _check_asn(cls, local_asn):
        # Read 4 byte asn flag from DB
        ok, result = GlobalSystemConfigServer.locate(
            fq_name=['default-global-system-config'],
            create_it=False,
            fields=['enable_4byte_as'])
        if not ok:
            return False, result

        enable_4byte_as_flag = result['enable_4byte_as']

        if enable_4byte_as_flag:
            # Now that 4byte AS is enabled, check if the local ASN
            # is between 1-0xffFFffFF
            if local_asn < 1 or local_asn > 0xFFFFFFFF:
                return False
        else:
            # Only 2 Byte AS allowed. The range should be
            # between 1-0xffFF
            if local_asn < 1 or local_asn > 0xFFFF:
                return False
        return True

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        bgp_router_prop = obj_dict.get('bgp_router_parameters')
        if bgp_router_prop:
            asn = bgp_router_prop.get('autonomous_system')
            if asn and asn != 'null':
                ok = cls._check_asn(asn)
                if not ok:
                    msg = "autonomous_system out of range"
                    return False, (400, msg)

            local_asn = bgp_router_prop.get('local_autonomous_system')
            if local_asn and local_asn != 'null':
                ok = cls._check_asn(local_asn)
                if not ok:
                    msg = "local_autonomous_system out of range"
                    return False, (400, msg)

            router_type = bgp_router_prop.get('router_type')
        else:
            asn = None
            router_type = None

        sub_cluster_ref = obj_dict.get('sub_cluster_refs')
        if sub_cluster_ref and asn:
            sub_cluster_obj = db_conn.uuid_to_obj_dict(
                obj_dict['sub_cluster_refs'][0]['uuid'])
            if asn != sub_cluster_obj['prop:sub_cluster_asn']:
                msg = "Subcluster asn and bgp asn should be same"
                return False, (400, msg)

        control_node_zone_ref = obj_dict.get('control_node_zone_refs')
        if control_node_zone_ref:
            if (len(control_node_zone_ref) > 1):
                msg = ("BgpRouter should refer only one control-node-zone")
                return False, (400, msg)
            if (router_type != 'control-node'):
                msg = ("BgpRouter type should be 'control-node' to refer "
                       "control-node-zone")
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
            ok, result = cls._validate_subcluster_dep(obj_dict, db_conn)
            if not ok:
                return ok, result

        router_type = None
        bgp_obj = None
        bgp_router_prop = obj_dict.get('bgp_router_parameters')
        if not bgp_router_prop:
            bgp_obj = db_conn.uuid_to_obj_dict(obj_dict['uuid'])
            bgp_router_prop = bgp_obj.get('prop:bgp_router_parameters')
        if bgp_router_prop:
            asn = bgp_router_prop.get('autonomous_system')
            if asn and asn != 'null':
                ok = cls._check_asn(asn)
                if not ok:
                    msg = "autonomous_system out of range"
                    return False, (400, msg)

            local_asn = bgp_router_prop.get('local_autonomous_system')
            if local_asn and local_asn != 'null':
                ok = cls._check_asn(local_asn)
                if not ok:
                    msg = "local_autonomous_system out of range"
                    return False, (400, msg)
            router_type = bgp_router_prop.get('router_type')
        control_node_zone_ref = obj_dict.get('control_node_zone_refs')
        if control_node_zone_ref:
            if (router_type != 'control-node'):
                msg = ("BgpRouter type should be 'control-node' to refer "
                       "control-node-zone")
                return False, (400, msg)
            if not bgp_obj:
                bgp_obj = db_conn.uuid_to_obj_dict(obj_dict['uuid'])
            cnz_ref_db = ([key for key in bgp_obj.keys()
                          if key.startswith('ref:control_node_zone')])
            if (len(cnz_ref_db) or len(control_node_zone_ref) > 1):
                msg = ("BgpRouter should refer only one control-node-zone")
                return False, (400, msg)

        return True, ''
