
from builtins import range
from builtins import str
import json
from pprint import pformat

from cfgm_common import _obj_serializer_all
from cfgm_common.exceptions import NoIdError
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from vnc_api.gen.resource_common import Node
from vnc_api.gen.resource_common import PortGroup

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class NodeServer(ResourceMixin, Node):
    @staticmethod
    def np_get_portmap(np_uuid, db_conn):
        ok, result = db_conn.dbe_read(obj_type='node-profile',
                                      obj_id=np_uuid,
                                      obj_fields=['hardware_refs'])
        if not ok:
            db_conn.config_log("node-profile read fail",
                               level=SandeshLevel.SYS_ERROR)
            return None

        ok, result = db_conn.dbe_read(
            obj_type='hardware',
            obj_id=result.get('hardware_refs')[0]['uuid'],
            obj_fields=['card_refs'])
        if not ok:
            db_conn.config_log("node-profile dbe_read of hardware failed",
                               level=SandeshLevel.SYS_ERROR)
            return None

        ok, result = db_conn.dbe_read(
            obj_type='card',
            obj_id=result.get('card_refs')[0]['uuid'])
        if not ok:
            db_conn.config_log("node-profile dbe_read of card failed",
                               level=SandeshLevel.SYS_ERROR)
            return None

        port_map = result.get('interface_map')['port_info']
        msg = ("np_get_portmap: %s", pformat(port_map))
        db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)
        return port_map

    @staticmethod
    def node_get_ports(obj_dict, db_conn):
        ok, result = db_conn.dbe_read(obj_type='node',
                                      obj_id=obj_dict['uuid'],
                                      obj_fields=['ports'])
        if not ok:
            return None

        if not result.get('ports'):
            return {}

        node_ports = {}
        for port in result['ports']:
            msg = ("np-node port: %s", pformat(port['uuid']))
            db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)
            ok, result = db_conn.dbe_read(obj_type='port',
                                          obj_id=port['uuid'])
            if not ok:
                return None

            msg = ("node_get_ports: %s", pformat(result))
            db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)
            port = {}
            port['uuid'] = result['uuid']
            port['fq_name'] = result['fq_name']
            port['name'] = result['fq_name'][-1]
            port['tag_ref'] = result.get('tag_refs', "")
            node_ports[result['fq_name'][-1]] = port

        return node_ports

    @staticmethod
    def remove_tag_refs(id, db_conn, node_ports):
        api_server = db_conn.get_api_server()
        for port in node_ports:
            for tag_ref in node_ports[port]['tag_ref']:
                api_server.internal_request_ref_update(
                    'port',
                    node_ports[port]['uuid'],
                    'DELETE',
                    'tag',
                    tag_ref['uuid'],
                    tag_ref['to'])
        return

    @staticmethod
    def remove_port_group_ref(db_conn, port_uuid, port_fq_name):
        api_server = db_conn.get_api_server()
        db_conn.config_log("Removing port-group refs",
                           level=SandeshLevel.SYS_DEBUG)
        ok, results = db_conn.dbe_read(obj_type='port',
                                       obj_id=port_uuid,
                                       obj_fields=['port_group_back_refs'])
        if not ok:
            msg = ("PORT-READ NOT OK")
            db_conn.config_log(str(msg), level=SandeshLevel.SYS_ERROR)
            return

        if not results.get('port_group_back_refs'):
            return

        msg = ("PORT-READ", pformat(results['port_group_back_refs']))
        db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)
        for pg in range(len(results.get('port_group_back_refs'))):
            pg_uuid = results['port_group_back_refs'][pg]['uuid']
            api_server.internal_request_ref_update('port-group',
                                                   pg_uuid,
                                                   'DELETE',
                                                   'port',
                                                   port_uuid,
                                                   port_fq_name)

    @staticmethod
    def process_node_profile(cls, id, fq_name, obj_dict, db_conn):
        msg = ("process_node_profile-obj-dict: %s", pformat(obj_dict))
        db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)
        api_server = db_conn.get_api_server()

        node_ports = cls.node_get_ports(obj_dict, db_conn)
        msg = ("ES-PORTS: %s", pformat(node_ports))
        db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)

        if obj_dict.get('node_profile_refs'):
            msg = ("process_node_profile: %s", pformat(obj_dict))
            db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)
            np_uuid = obj_dict['node_profile_refs'][0]['uuid']
            port_map = cls.np_get_portmap(np_uuid, db_conn)
            db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)

            port_groups = {}
            for port in port_map:
                port_labels = port.get('labels')
                port_name = port.get('name')
                if not (port_name and node_ports.get(port_name)):
                    continue
                db_conn.config_log("process NP " + str(port_labels),
                                   level=SandeshLevel.SYS_DEBUG)

                port_group_name = port.get('port_group')
                if port_group_name:
                    if not port_groups.get(port_group_name):
                        port_groups[port_group_name] = []
                    port_groups[port_group_name].append(port_name)

                for port_label in port_labels:
                    port_label_uuid = db_conn.fq_name_to_uuid(
                        'tag',
                        ['label=' + port_label])
                    db_conn.config_log("process NP uuid" + port_label_uuid,
                                       level=SandeshLevel.SYS_DEBUG)

                    api_server.internal_request_ref_update(
                        'port',
                        node_ports[port_name]['uuid'],
                        'ADD',
                        'tag',
                        port_label_uuid,
                        ['label=' + port_label])

            node_fq_name = db_conn.uuid_to_fq_name(obj_dict['uuid'])
            for pg in port_groups:
                create_port_group = False
                pg_fq_name = node_fq_name + [pg]
                db_conn.config_log("process NP port-groups 1 " +
                                   str(pg_fq_name),
                                   level=SandeshLevel.SYS_DEBUG)
                try:
                    pg_uuid = cls.db_conn.fq_name_to_uuid('port-group',
                                                          pg_fq_name)
                except NoIdError:
                    create_port_group = True

                if create_port_group:
                    pg_obj = PortGroup(parent_type='node',
                                       fq_name=pg_fq_name)
                    pg_dict = json.dumps(pg_obj, default=_obj_serializer_all)
                    ok, port_group_obj = api_server.internal_request_create(
                        'port-group',
                        json.loads(pg_dict))
                    if not ok:
                        db_conn.config_log("port-group creation failed",
                                           level=SandeshLevel.SYS_DEBUG)

                    pg_uuid = port_group_obj['port-group'].get('uuid')
                    msg = ("port-group-obj : %s", pformat(port_group_obj))
                    db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)
                    db_conn.config_log(str(pg_fq_name),
                                       level=SandeshLevel.SYS_DEBUG)

                for interface in port_groups[pg]:
                    port_fq_name = node_fq_name + [interface]
                    port_uuid = cls.db_conn.fq_name_to_uuid('port',
                                                            port_fq_name)
                    msg = ("ref_create for %s, UUID %s, fq_name %s", interface,
                           port_uuid, port_fq_name)
                    db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)
                    cls.remove_port_group_ref(db_conn, port_uuid, port_fq_name)
                    ok, result = api_server.internal_request_ref_update(
                        'port-group', pg_uuid, 'ADD',
                        'port', port_uuid, port_fq_name)
                    msg = ("ref-update status ", str(ok), ": result ",
                           pformat(result))
                    db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)
        elif 'node_profile_refs' in obj_dict:
            cls.remove_tag_refs(id, db_conn, node_ports)

    @classmethod
    def post_dbe_update(cls, id, fq_name, obj_dict, db_conn,
                        prop_collection_updates=None, ref_update=None, **kwargs):
        db_conn.config_log('NodeSever: post_dbe_update hit',
                           level=SandeshLevel.SYS_DEBUG)
        msg = ("Node-UPDATE: %s", pformat(obj_dict))
        db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)
        ok, result = db_conn.dbe_read(obj_type='node',
                                      obj_id=obj_dict['uuid'])
        msg = ("Node-UPDATE-result: %s", pformat(result))
        db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)
        cls.process_node_profile(cls, id, fq_name, obj_dict, db_conn)
        return True, ''

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        db_conn.config_log('NodeSever: post_dbe_create hit',
                           level=SandeshLevel.SYS_DEBUG)
        msg = ("Node-Create: %s", pformat(obj_dict))
        db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)
        cls.process_node_profile(cls, id, None, obj_dict, db_conn)
        return True, ''
