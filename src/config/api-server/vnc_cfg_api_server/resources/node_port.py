
from builtins import str
from pprint import pformat

from cfgm_common.exceptions import NoIdError
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from vnc_api.gen.resource_common import Port

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class PortServer(ResourceMixin, Port):
    @staticmethod
    def _create_pi_ref(obj_dict, db_conn):
        api_server = db_conn.get_api_server()

        db_conn.config_log('PortSever: create_pi_ref hit',
                           level=SandeshLevel.SYS_DEBUG)
        msg = ("OBJ_DICT %s", pformat(obj_dict))
        db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)

        if obj_dict.get('bms_port_info') \
           and obj_dict.get('bms_port_info').get('local_link_connection'):
            link_details = \
                obj_dict.get('bms_port_info').get('local_link_connection')
            switch_name = link_details.get('switch_info')
            port_id = link_details.get('port_id')
            if switch_name and port_id:
                port_id = port_id.replace(':', '_')
                pi_fq_name = ['default-global-system-config',
                              switch_name, port_id]
                pi_uuid = None
                try:
                    pi_uuid = db_conn.fq_name_to_uuid('physical_interface',
                                                      pi_fq_name)
                except NoIdError as e:
                    db_conn.config_log("Not PI found for " + switch_name +
                                       " => " + port_id + str(e),
                                       level=SandeshLevel.SYS_WARN)
                    pass
                if pi_uuid:
                    db_conn.config_log(pi_uuid, level=SandeshLevel.SYS_DEBUG)
                    port_fq_name = db_conn.uuid_to_fq_name(obj_dict['uuid'])
                    api_server.internal_request_ref_update(
                        'physical-interface',
                        pi_uuid, 'ADD',
                        'port',
                        obj_dict['uuid'],
                        port_fq_name)
                    db_conn.config_log("REF UPDATE DONE",
                                       level=SandeshLevel.SYS_DEBUG)

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        cls._create_pi_ref(obj_dict, db_conn)
        return True, ''

    @classmethod
    def post_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        api_server = db_conn.get_api_server()
        db_conn.config_log('PortSever: post_dbe_update hit',
                           level=SandeshLevel.SYS_DEBUG)
        msg = ("PORT-UPDATE: %s", pformat(obj_dict))
        db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)
        msg = ("PORT-UPDATE : %s", pformat(fq_name))
        db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)
        msg = ("PORT-UPDATE : %s", pformat(kwargs))
        db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)

        if obj_dict.get('bms_port_info') \
           and obj_dict.get('bms_port_info').get('local_link_connection'):
            ok, result = db_conn.dbe_read(
                obj_type='port',
                obj_id=obj_dict['uuid'],
                obj_fields=['physical_interface_back_refs'])
            msg = ("PORT-UPDATE BACK-REFS: %s", str(ok))
            if ok:
                msg = ("PORT-UPDATE BACK-REFS: %s", pformat(result))
                db_conn.config_log(str(msg), level=SandeshLevel.SYS_DEBUG)
                if result.get('physical_interface_back_refs'):
                    pi_uuid = result['physical_interface_back_refs'][0]['uuid']
                    port_fq_name = db_conn.uuid_to_fq_name(obj_dict['uuid'])
                    api_server.internal_request_ref_update(
                        'physical-interface',
                        pi_uuid, 'DELETE',
                        'port',
                        obj_dict['uuid'],
                        port_fq_name)

        cls._create_pi_ref(obj_dict, db_conn)

        return True, ''
