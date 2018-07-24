#!/usr/bin/python
import logging
import sys
import traceback
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

    @staticmethod
    def _validate_job_ctx(job_ctx):
        """
        :param job_ctx: Dictionary
        :return:
        """
        if not job_ctx.get('fabric_fqname'):
            raise ValueError('Invalid job_ctx: missing fabric_fqname')
    # end _validate_job_ctx

    def __init__(self):
        self._logger = FilterModule._init_logging()
    # end __init__

    def filters(self):
        return {
            'discover_role': self.discover_role,
        }

    def discover_role(self, job_ctx, prouter_name, prouter_uuid,
                      prouter_vendor_name, prouter_product_name):

        role_discovery_log = "\n"

        try:
            FilterModule._validate_job_ctx(job_ctx)
            fabric_fqname = job_ctx.get('fabric_fqname')
            vnc_lib = FilterModule._init_vnc_api(job_ctx)

            # form the hardware fqname from prouter_vendor_name and
            # prouter_product_name
            hw_fq_name = [(prouter_vendor_name + '-' + prouter_product_name
                           ).lower()]

            # read the hardware object with this fq_name
            role_discovery_log += "Reading the hardware object ...."
            try:
                hw_obj = vnc_lib.hardware_read(fq_name=hw_fq_name)
            except NoIdError as no_id_exc:
                self._logger.info(role_discovery_log)
                self._logger.info("\nHardware Object not present in"
                                  "database: " + str(no_id_exc))
                traceback.print_exc(file=sys.stdout)
                self._logger.info("\nCompleting role discovery for device.. ")
                return {
                    'status': 'success',
                    'fabric_fqname': fabric_fqname,
                    'device_name': prouter_name,
                    'role_discovery_log': role_discovery_log
                }

            role_discovery_log += "done\n"

            # get all the node-profile back-refs for this hardware object
            role_discovery_log += "Getting all the node-profile back refs" \
                                  " for the hardware: %s" % hw_fq_name[-1]
            np_back_refs = hw_obj.get_node_profile_back_refs() or []
            role_discovery_log += "done\n"

            # get the fabric object fq_name to check if the node-profile
            # is in the same fabric
            role_discovery_log += "Fetching the fabric fq_name ..."
            fab_fq_name = fabric_fqname.split(":")
            role_discovery_log += "done\n"

            # read the fabric_object to get a list of node-profiles under
            # this fabric_object
            role_discovery_log += "Reading the fabric object ..."
            fabric_obj = vnc_lib.fabric_read(fq_name=fab_fq_name)
            role_discovery_log += "done\n"

            # get the list of node profile_uuids under the given fabric
            role_discovery_log += "Getting the list of node-profile-uuids" \
                                  " under this fabric object .... "
            node_profiles_list = fabric_obj.get_node_profile_refs() or []
            node_profile_obj_uuid_list = self._get_object_uuid_list(
                node_profiles_list)
            role_discovery_log += "done\n"

            # check to see which of the node-profile back refs are in the
            # present fabric. Assumption: at present there is only a single
            # node-profile that can match a hardware under the current
            # given fabric

            role_discovery_log += "Checking to see if any node-profile" \
                                  " is under given fabric .... \n"
            upd_resp = self._do_role_discovery(np_back_refs, prouter_name,
                                               vnc_lib, prouter_uuid,
                                               node_profile_obj_uuid_list)

            role_discovery_log += upd_resp + "\n" + "done\n"

            self._logger.info(role_discovery_log)
            return {
                'status': 'success',
                'fabric_fqname': fabric_fqname,
                'device_name': prouter_name,
                'role_discovery_log': role_discovery_log
            }
        except NoIdError as no_id_exc:
            self._logger.info(role_discovery_log)
            self._logger.error("Object not present in database: "
                               + str(no_id_exc))
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

        for object in object_list or []:
            obj_uuid_list.append(object['uuid'])

        return obj_uuid_list

    def _do_role_discovery(self, np_back_refs, prouter_name, vnc_lib,
                           prouter_uuid, node_profile_obj_uuid_list):
        upd_resp = ""
        for np_back_ref in np_back_refs:
            if np_back_ref['uuid'] in node_profile_obj_uuid_list:
                upd_resp += "Creating ref between %s and %s ...."\
                            % (prouter_name, np_back_ref['to'][-1])
                ref_upd_resp = vnc_lib.ref_update("physical_router",
                                                  prouter_uuid,
                                                  "node_profile",
                                                  np_back_ref['uuid'],
                                                  None, 'ADD')
                self._logger.info(ref_upd_resp)
                upd_resp += "done\n"
                # break
        return upd_resp


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


def _mock_pb_input_discover_role():

    return {
        "args": "{\"fabric_ansible_conf_file\": "
                "    [\"/etc/contrail/contrail-keystone-auth.conf\","
                "     \"/etc/contrail/contrail-fabric-ansible.conf\"],"
                " \"collectors\": [\"10.155.75.102:8086\"]}",
        "auth_token": "b71842739db94305a217f3b3cb36eb02",
        "device_family": "junos-qfx",
        "device_fqname": [
            "default-global-system-config",
            "5b11-qfx4"
        ],
        "device_id": "00b80c2e-53f8-4544-ad4c-da278cadd0e0",
        "device_management_ip": "10.87.69.122",
        "device_password": "Embe1mpls",
        "device_username": "root",
        "fabric_fq_name": "default-global-system-config:fab01",
        "job_execution_id": "a17a2586-4400-4413-88d5-99e36ba5003a",
        "job_template_fqname": [
            "default-global-system-config",
            "discover_role_template"
        ],
        "job_template_id": "bd80dd6e-23dd-49d6-a3d0-2bae8bdb04aa",
        "params": {
            "device_list": [
                "00b80c2e-53f8-4544-ad4c-da278cadd0e0"
            ]
        },
        "playbook_job_percentage": 95.0,
        "prev_pb_output": {},
        "vendor": "Juniper",
        "product_name": "QFX10002-36Q"
    }

# end _mock_pb_input_discover_role


def _parse_args():
    parser = argparse.ArgumentParser(description='fabric filters tests')
    parser.add_argument('-r', '--discover_role',
                        action='store_true',
                        help='discover role for physical routers')
    return parser.parse_args()
# end _parse_args


if __name__ == '__main__':

    results = None
    fabric_filter = FilterModule()
    parser = _parse_args()
    if parser.discover_role:
        pb_input = _mock_pb_input_discover_role()
        results = fabric_filter.discover_role(
            _mock_job_ctx_discover_role(),
            pb_input['device_fqname'][-1],
            pb_input['device_id'], pb_input['vendor'],
            pb_input['product_name'])
    print results
