#!/usr/bin/python

import argparse
import sys
import traceback

from cfgm_common.exceptions import NoIdError
from filter_utils import _task_done, _task_error_log, _task_log, FilterLog

from job_manager.job_utils import JobVncApi


sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")


class FilterModule(object):

    @staticmethod
    def _validate_job_ctx(job_ctx):
        if not job_ctx.get('fabric_fqname'):
            raise ValueError('Invalid job_ctx: missing fabric_fqname')
    # end _validate_job_ctx

    def filters(self):
        return {
            'discover_role': self.discover_role,
        }
    # end filters

    def discover_role(self, job_ctx, prouter_name, prouter_uuid,
                      prouter_vendor_name, prouter_product_name):

        node_profile_refs = []

        try:
            FilterLog.instance("DiscoverRoleFilter", prouter_name)
            FilterModule._validate_job_ctx(job_ctx)
            fabric_fqname = job_ctx.get('fabric_fqname')
            vnc_lib = JobVncApi.vnc_init(job_ctx)

            # form the hardware fqname from prouter_vendor_name and
            # prouter_product_name
            hw_fq_name = [(prouter_vendor_name + '-' + prouter_product_name
                           ).lower()]

            # read the hardware object with this fq_name
            _task_log("Reading the hardware object")
            try:
                hw_obj = vnc_lib.hardware_read(
                    fq_name=hw_fq_name,
                    fields=['node_profile_back_refs'])
            except NoIdError as no_id_exc:
                _task_log("\nHardware Object not present in "
                          "database: " + str(no_id_exc))
                traceback.print_exc(file=sys.stdout)
                _task_log("Completing role discovery for device")
                _task_done()
                return {
                    'status': 'success',
                    'fabric_fqname': fabric_fqname,
                    'np_refs': node_profile_refs,
                    'device_name': prouter_name,
                    'role_discovery_log': FilterLog.instance().dump()
                }

            _task_done()

            # get all the node-profile back-refs for this hardware object
            _task_log("Getting all the node-profile back refs"
                      " for the hardware: %s" % hw_fq_name[-1])
            np_back_refs = hw_obj.get_node_profile_back_refs() or []
            _task_done()

            # get the fabric object fq_name to check if the node-profile
            # is in the same fabric
            _task_log("Fetching the fabric fq_name")
            fab_fq_name = fabric_fqname.split(":")
            _task_done()

            # read the fabric_object to get a list of node-profiles under
            # this fabric_object
            _task_log("Reading the fabric object")
            fabric_obj = vnc_lib.fabric_read(fq_name=fab_fq_name)
            _task_done()

            # get the list of node profile_uuids under the given fabric
            _task_log("Getting the list of node-profile-uuids"
                      " under this fabric object .... ")
            node_profiles_list = fabric_obj.get_node_profile_refs() or []
            node_profile_obj_uuid_list = self._get_object_uuid_list(
                node_profiles_list)
            _task_done()

            # check to see which of the node-profile back refs are in the
            # present fabric. Assumption: at present there is only a single
            # node-profile that can match a hardware under the current
            # given fabric

            _task_log("Checking to see if any node-profile"
                      " is under given fabric .... \n")
            upd_resp, node_profile_refs = self._do_role_discovery(
                np_back_refs,
                prouter_name,
                vnc_lib, prouter_uuid,
                node_profile_obj_uuid_list)

            _task_log(upd_resp + "\n")
            _task_done()

            return {
                'status': 'success',
                'np_refs': node_profile_refs,
                'fabric_fqname': fabric_fqname,
                'device_name': prouter_name,
                'role_discovery_log': FilterLog.instance().dump()
            }
        except NoIdError as no_id_exc:
            _task_error_log("Object not present in database: " + str(
                no_id_exc))
            traceback.print_exc(file=sys.stdout)
            return {
                'status': 'failure',
                'error_msg': str(no_id_exc),
                'role_discovery_log': FilterLog.instance().dump()
            }
        except Exception as ex:
            _task_error_log(str(ex))
            traceback.print_exc(file=sys.stdout)
            return {
                'status': 'failure',
                'error_msg': str(ex),
                'role_discovery_log': FilterLog.instance().dump()
            }
    # end discover_role

    def _get_object_uuid_list(self, object_list):
        obj_uuid_list = []

        for obj in object_list or []:
            obj_uuid_list.append(obj['uuid'])

        return obj_uuid_list
    # end _get_object_uuid_list

    def _do_role_discovery(self, np_back_refs, prouter_name, vnc_lib,
                           prouter_uuid, node_profile_obj_uuid_list):
        upd_resp = ""
        node_profile_refs = []

        for np_back_ref in np_back_refs:
            if np_back_ref['uuid'] in node_profile_obj_uuid_list:
                upd_resp += "Creating ref between %s and %s ....\n" \
                            % (prouter_name, np_back_ref['to'][-1])
                ref_upd_resp = vnc_lib.ref_update("physical_router",
                                                  prouter_uuid,
                                                  "node_profile",
                                                  np_back_ref['uuid'],
                                                  None, 'ADD')
                _task_log(ref_upd_resp)
                _task_done()
                node_profile_refs.append(np_back_ref['to'][-1])
        return upd_resp, node_profile_refs
    # end _do_role_discovery


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
