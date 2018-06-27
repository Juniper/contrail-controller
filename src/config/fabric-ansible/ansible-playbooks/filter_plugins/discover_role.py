#!/usr/bin/python
import logging
import sys, traceback
import argparse

from cfgm_common.exceptions import NoIdError
from vnc_api.vnc_api import VncApi

class FilterModule(object):
    @staticmethod
    def _init_logging():
        logger = logging.getLogger('RoleDiscoveryFilter')
        console_handler = logging.StreamHandler()
        console_handler.setLevel(logging.INFO)

        formatter = logging.Formatter(
            '%(asctime)s %(levelname)-8s %(message)s',
            datefmt='%Y/%m/%d %H:%M:%S'
        )
        console_handler.setFormatter(formatter)
        logger.addHandler(console_handler)

        return logger
    # end _init_logging

    @staticmethod
    def _init_vnc_api(job_ctx):
        return VncApi(
            auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
            auth_token=job_ctx.get('auth_token')
        )
    # end _init_vnc_api

    def __init__(self):
        self._logger = FilterModule._init_logging()
    # end __init__

    def filters(self):
        return {
            'discover_role': self.discover_role,
        }

    def discover_role(self, job_ctx):
        role_discovery_log = "\n"
        try:
            vnc_lib = FilterModule._init_vnc_api(job_ctx)

            # read the fabric object
            role_discovery_log += "Reading the fabric object ..."
            fabric_fqname = job_ctx.get('fabric_fqname')
            fab_fq_name = fabric_fqname.split(":")
            fabric_obj = vnc_lib.fabric_read(fq_name=fab_fq_name)
            role_discovery_log += "done\n"

            # get the list of node profile_uuids under the given fabric
            role_discovery_log += "Fetching the list of node_profiles under fabric ..."
            node_profiles_list = fabric_obj.get_node_profile_refs()
            node_profile_obj_uuid_list = self._get_object_uuid_list(node_profiles_list)
            role_discovery_log += "done\n"

            # get the list of prouter_uuids under the given fabric
            role_discovery_log += "Fetching the list of physical_routers under fabric ..."
            prouters_list = fabric_obj.get_physical_router_refs()
            prouter_obj_uuid_list = self._get_object_uuid_list(prouters_list)
            role_discovery_log += "done\n"

            # get corresponding hardware_refs information for all node_profiles under this fabric
            role_discovery_log += "Fetching hardware-refs of node_profiles under fabric " \
                                  "and creating a map of hardware and node-profiles ...."
            node_profiles_detail = vnc_lib.node_profiles_list(fields=['hardware_refs'],
                                                              obj_uuids=node_profile_obj_uuid_list)
            hw_np_map = self._hw_node_profile_map(node_profiles_detail)
            role_discovery_log += "done\n"

            # get the prouter details for all prouters under this fabric
            role_discovery_log += "Fetching prouter details of physical_routers under fabric " \
                                  "and creating a map of prouter and hardware ...."
            prouters_detail = vnc_lib.physical_routers_list(
                fields=['physical_router_vendor_name', 'physical_router_product_name'],
                obj_uuids=prouter_obj_uuid_list)
            prouter_hw_map = self._prouter_hw_map(prouters_detail)
            role_discovery_log += "done\n"

            prouter_np_list = self._prouter_np_map(hw_np_map, prouter_hw_map)
            #bulk ref-update between prouter and node-profiles
            role_discovery_log += "update physical_routers with appropriate node_profile refs ...\n"
            upd_resp_log = self._bulk_ref_update_pr_np(vnc_lib, prouter_np_list)
            role_discovery_log += upd_resp_log + "\n"  + "done\n"

            self._logger.info(role_discovery_log)
            return {
                'status': 'success',
                'fabric_uuid': fabric_obj.uuid,
                'role_discovery_log': role_discovery_log
            }
        except NoIdError as no_id_exc:
            self._logger.info(role_discovery_log)
            self._logger.error(str(no_id_exc))
            traceback.print_exc(file=sys.stdout)
            return {
                'status': 'failure',
                'error_msg': str(no_id_exc),
                'role_discovery_log': role_discovery_log
            }
        except Exception as ex:
            self._logger.info(role_discovery_log)
            self._logger.error(str(ex))
            traceback.print_exc(file=sys.stdout)
            return {
                'status': 'failure',
                'error_msg': str(ex),
                'role_discovery_log': role_discovery_log
            }

    def _get_object_uuid_list(self, object_list):
        obj_uuid_list = []

        for object in object_list:
            obj_uuid_list.append(object['uuid'])

        return obj_uuid_list

    def _bulk_ref_update_pr_np(self, vnc_lib, prouter_np_list):

        upd_resp_log = ""

        for prouter_np in prouter_np_list:
            obj_uuid = prouter_np[0]
            ref_uuid = prouter_np[1]
            obj_type = "physical_router"
            ref_type = "node_profile"

            vnc_lib.ref_update(obj_type, obj_uuid, ref_type,
                               ref_uuid, None, 'ADD')
            upd_resp_log += prouter_np[0] + "--->" + prouter_np[1] + "\n"

        return upd_resp_log


    '''

    This filter takes input as the node_profiles list with hardware_refs obtained from
    vnc_lib.node_profiles_list(fields = ['hardware_refs'], obj_uuids=[np_uuid1, np_uuid2,...])

    Sample O/P:
    {u'node-profiles': [{u'fq_name': [u'default-global-system-config', u'juniper-qfx5100'],
    u'parent_uuid': u'1cab83f3-0d19-494a-900a-096e223f8d1a',
    u'hardware_refs': [{u'to': [u'juniper-qfx5100-48s-6q'],
                        u'attr': None,
                        u'uuid': u'c8961d45-4338-4a72-a073-55e0b4678fe2'}],
    u'parent_type': u'global-system-config',
    u'href': u'http://10.155.75.181:8082/node-profile/d7131371-9477-4fee-ab62-31cbbec611d4',
    u'uuid': u'd7131371-9477-4fee-ab62-31cbbec611d4'}]}

    and returns a mapping of each hardware name to the node_profile that references to this hardware object.
    returns: {
                 hw_name1: {np_uuid: <node_profile_uuid1>},
                 hw_name2: {np_uuid: <node_profile_uuid2>},
                 hw_name3: {np_uuid: <node_profile_uuid2>},
                 ....
             }
    Later we can add other fields such as card-fqnames, card-ports to a particular hw_fqname.
    '''
    def _hw_node_profile_map(self, node_profiles_list):

        hw_np_map = {}
        np_list = node_profiles_list['node-profiles']

        for node_profile in np_list:
            hw_refs = node_profile['hardware_refs']
            for hw_ref in hw_refs:
                hw_fqname = hw_ref['to']

                # can add other params such as card-ports or names to
                # this hw_detail_obj

                hw_detail_obj = {'np_uuid': node_profile['uuid']}
                hw_np_map[hw_fqname[-1]] = hw_detail_obj

        return hw_np_map

    '''

    This filter takes input as the list of physical-routers with their vendor
    and product names and returns a map of physical-router uuid to hardware
    name. The input to this filter comes from running
    vnc_lib.physical_routers_list(fields = ['physical_router_vendor_name', 'physical_router_product_name'],
                                  obj_uuids=[pr_uuid1, pr_uuid2, ....]).

    Sample O/P:
    {u'physical-routers':
        [ {
           u'fq_name': [u'default-global-system-config', u'5b11-qfx5'],
           u'uuid': u'74e8b50c-a6f5-439f-a607-bb6352240ac2',
           u'physical_router_vendor_name': u'Juniper',
           u'parent_type': u'global-system-config',
           u'physical_router_product_name': u'qfx10002-36q',
           u'href': u'http://10.155.75.181:8082/physical-router/74e8b50c-a6f5-439f-a607-bb6352240ac2',
           u'parent_uuid': u'1cab83f3-0d19-494a-900a-096e223f8d1a'},
          {
           u'fq_name': [u'default-global-system-config', u'5b11-qfx1'],
           u'uuid': u'5a03c69c-70ac-4750-bd23-26f157a26e31',
           u'physical_router_vendor_name': u'Juniper',
           u'parent_type': u'global-system-config',
           u'physical_router_product_name': u'qfx5100-48s-6q',
           u'href': u'http://10.155.75.181:8082/physical-router/5a03c69c-70ac-4750-bd23-26f157a26e31',
           u'parent_uuid': u'1cab83f3-0d19-494a-900a-096e223f8d1a'}
        ]
    }

    returns: {
                 physical_router_uuid1: hw_name1,
                 physical_router_uuid2: hw_name1,
                 physical_router_uuid3: hw_name2,
                 .........
             }

    '''
    def _prouter_hw_map(self, prouters_list):

        prouter_hw_map = {}
        prouters = prouters_list['physical-routers']

        for prouter in prouters:
            hw_name = (prouter['physical_router_vendor_name'] + '-' + prouter['physical_router_product_name']).lower()
            prouter_hw_map[prouter['uuid']] = hw_name

        return prouter_hw_map

    '''

    This filter takes input from the other 2 map functions namely the hw_np_map
    and the prouter_hw_map and returns a list of prouter_uuid, np_uuid tuples
    for ref-update. At present the association of a node-profile to a physical router
    is solely based on the matching hardware names in node-profile and physical-router.

    returns: [
                (prouter_uuid1, np_uuid1),
                (prouter_uuid2, np_uuid1),
                (prouter_uuid3, np_uuid2),
                .......
             ]

    '''
    def _prouter_np_map(self, hw_np_map, prouter_hw_map):

        prouter_np_list = []

        for prouter_uuid in prouter_hw_map:
            hw_for_prouter = prouter_hw_map[prouter_uuid]
            np_for_hw = hw_np_map.get(hw_for_prouter)
            if np_for_hw:
                # can later extend to do other processing such
                # as card type checking
                np_uuid = np_for_hw.get('np_uuid')
                prouter_np_list.append((prouter_uuid, np_uuid))

        return prouter_np_list

def _mock_job_ctx_discover_role():
    return {
        "auth_token": "",
        "config_args": {
            "collectors": [
                "10.155.75.102:8086"
            ],
            "fabric_ansible_conf_file": [
                "/etc/contrail/contrail-keystone-auth.conf",
                "/etc/contrail/contrail-fabric-ansible.conf"
            ]
        },
        "current_task_index": 1,
        "fabric_fqname": "default-global-system-config:fab01",
        "job_execution_id": "0859fe0b-dde0-470c-a4d9-af44a8480eb9",
        "job_input": {
            "fabric_uuid": "0ad8d7e9-e150-4dae-a333-be1c50b885f9"
        },
        "job_template_fqname": [
            "default-global-system-config",
            "discover_role_template"
        ],
        "playbook_job_percentage": "95.0",
        "task_weightage_array": None,
        "total_task_count": 1

    }
# end _mock_job_ctx_discover_role


def _parse_args():
    parser = argparse.ArgumentParser(description='fabric filters tests')
    parser.add_argument('-r', '--discover_role',
                        action='store_true', help='discover role for physical routers')
    return parser.parse_args()
# end _parse_args

if __name__ == '__main__':

    results = None
    fabric_filter = FilterModule()
    parser = _parse_args()
    if parser.discover_role:
        results = fabric_filter.discover_role(_mock_job_ctx_discover_role())
    print results
