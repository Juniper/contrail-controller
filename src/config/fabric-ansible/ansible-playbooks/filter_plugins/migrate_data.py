#!/usr/bin/python

import traceback
import sys

from job_manager.job_utils import JobVncApi
from vnc_api.exceptions import NoIdError

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
from filter_utils import FilterLog, _task_log, _task_done, \
    _task_error_log


# ******************************* Helper classes ******************************

class LogicalInterfaceCache(object):
    li_json = None

    def __init__(self, li):
        self.li_json = {
            "parent_type": li.parent_type,
            "fq_name": li.fq_name,
            "display_name": li.display_name,
            "logical_interface_type": li.logical_interface_type,
            "logical_interface_vlan_tag": li.logical_interface_vlan_tag
        }


class PhysicalInterfaceCache(object):
    pi_json = None

    def __init__(self, pi):
        mac_address_list = None
        if pi.physical_interface_mac_addresses is not None:
            mac_address_list = pi.physical_interface_mac_addresses.mac_address
        self.pi_json = {
            "parent_type": pi.parent_type,
            "fq_name": pi.fq_name,
            "display_name": pi.display_name,
            "physical_interface_port_id": pi.physical_interface_port_id,
            "physical_interface_mac_addresses": {
                "mac_address": mac_address_list
            }
        }


class PhysicalRouterCache(object):
    pr_json = None
    node_profile_fq_name = None
    physical_interface_list = list()
    logical_interface_list = list()

    def __init__(self, pr):
        user_name = None
        password = None
        if pr.physical_router_user_credentials is not None:
            user_name = pr.physical_router_user_credentials.username
            password = pr.physical_router_user_credentials.password

        self.pr_json = {
            "parent_type": pr.parent_type,
            "fq_name": pr.fq_name,
            "physical_router_management_ip": pr.physical_router_management_ip,
            "physical_router_vendor_name": pr.physical_router_vendor_name,
            "physical_router_product_name": pr.physical_router_product_name,
            "physical_router_device_family": pr.physical_router_device_family,
            "physical_router_vnc_managed": pr.physical_router_vnc_managed,
            "physical_router_user_credentials": {
                "username": user_name,
                "password": password
            },
            "physical_router_dataplane_ip": pr.physical_router_dataplane_ip,
            "physical_router_loopback_ip": pr.physical_router_loopback_ip
            }


class DataCache(object):
    # map of the fabric fq name to PR cache objects
    fabric_data_map = dict()
    # map of the physical interface name to physical interface name giving
    # the topology
    topology_ref_map = dict()
    # map of the physical router to the roles
    pr_role_map = dict()
    # ip allocation map from mac to ip
    mac_to_ip_map = dict()


    def add_pr_list(self, fabric_fq_name, pr_cache_list):
        self.fabric_data_map[fabric_fq_name] = pr_cache_list


class PhysicalRouterReport(object):
    # success/failure
    status = None
    error_msg = None
    pi_created_list = None
    pi_creation_failure_list = None
    li_created_list = None
    li_creation_failure_list = None


class FabricReport(object):
    # success/failure
    status = None
    error_msg = None
    pr_created_list = None
    pr_failure_list = None
    # pr fq name to PhysicalRouterReport map
    detailed_report_map = None


class DataImportReport(object):
    # success/failure
    status = None
    # fabric fq name to FabricReport map
    fabric_report_map = None


# ****************************************************************************


class FilterModule(object):
    dc = DataCache()
    # map of fabric_fqname to fabric_report objects
    result = dict()
    # total status
    status = "success"
    err_message = None

    def filters(self):
        return {
            'migrate_data': self.migrate_data,
        }
    # end filters

    def _instantiate_filter_log_instance(self):
        FilterLog.instance("MigrateDataFilter")
    # end _instantiate_filter_log_instance

    def migrate_data(self, job_ctxt):
        self._instantiate_filter_log_instance()
        _task_log("Starting data migration")
        try:
            _task_log("Starting data export")
            self._export_data(job_ctxt)
            _task_done()

            _task_log("Starting data import")
            self._import_data(job_ctxt)
            _task_done()

            return {
                'status': self.status,
                'data_migration_log': FilterLog.instance().dump(),
                'data_migration_resp': self.result,
                'pr_role_info': self.dc.pr_role_map,
                'ip_allocation_info': self.dc.mac_to_ip_map
            }
        except Exception as ex:
            _task_error_log(str(ex))
            _task_error_log(traceback.format_exc())
            return {'status': 'failure',
                    'error_msg': str(ex),
                    'data_migration_log': FilterLog.instance().dump()}
        _task_done("Completed data migration")
    # end migrate_data

    def _export_data(self, job_ctxt):
        # initialize vnc client to source contrail setup
        vnc_params = job_ctxt.get("job_input").get("external_contrail_params")
        src_cc_job_ctxt = {
            "api_server_host": vnc_params.get("api_server_ip_list"),
            "vnc_api_init_params": {
                "admin_user": vnc_params.get("admin_user"),
                "admin_password": vnc_params.get("admin_password"),
                "admin_tenant_name": vnc_params.get("admin_tenant_name"),
                "api_server_port": vnc_params.get("api_server_port"),
                "api_server_use_ssl": vnc_params.get("api_server_use_ssl")
            }
        }
        vnc_lib = JobVncApi.vnc_init(src_cc_job_ctxt)

        if vnc_lib is None:
            raise Exception("Unable to create vnc client"
                            " for %s " % vnc_params.get("api_server_ip_list"))

        job_input = job_ctxt.get("job_input")
        if job_input is not None and job_input.get("fabric_list") is not None:
            fabric_list = job_input.get("fabric_list")
        else:
            raise Exception("Missing job_input in job_ctxt or fabric_list "
                            "is not present in the job_input")

        # iterate through the input fabric fqname list and export data
        for fabric_fq_name in fabric_list:
            self._export_fabric(fabric_fq_name.get(
                "fabric_fq_name"), vnc_lib)
    # end _export_data

    def _export_fabric(self, fabric_fq_name, vnc_lib):
        _task_log("Starting data export for fabric %s" % fabric_fq_name)
        fabric_report = FabricReport()
        fabric_report.status = "success"
        try:
            # read the fabric from db
            fabric = vnc_lib.fabric_read(fq_name=fabric_fq_name)
            if fabric is not None:
                pr_refs = fabric.get_physical_router_back_refs()
                pr_cache_list = list()
                if pr_refs is not None:
                    for pr_ref in pr_refs:
                        try:
                            _task_log("Starting data export"
                                      " for PR %s" % pr_ref.get("uuid"))
                            # read the PR device
                            pr_uuid = pr_ref.get("uuid")
                            pr = vnc_lib.physical_router_read(id=pr_uuid)
                            pr_cache = PhysicalRouterCache(pr)

                            # read the ref to node profile
                            node_profile_refs = pr.get_node_profile_refs()
                            if node_profile_refs is not None and \
                                    len(node_profile_refs) > 0:
                                pr_cache.node_profile_fq_name = \
                                    node_profile_refs[0].get("to")

                            # read the role info
                            self._read_pr_role_info(pr)

                            # read the physical and logical interfaces data
                            pr_cache = self._export_interface(pr, pr_cache,
                                                              vnc_lib)
                            pr_cache_list.append(pr_cache)
                            _task_done
                        except Exception as exc:
                            _task_error_log(str(exc))
                            _task_error_log(traceback.format_exc())
                            self.status = "failure"
                            fabric_report.status = "failure"
                            fabric_report.error_msg += \
                                " Exception while exporting PR " \
                                "%s %s" % (pr_ref.get("uuid"), str(exc))
                    self.dc.add_pr_list(self._get_fq_name_str(fabric_fq_name),
                                        pr_cache_list)
        except Exception as exc:
            _task_error_log(str(exc))
            _task_error_log(traceback.format_exc())
            self.status = "failure"
            fabric_report.status = "failure"
            fabric_report.error_msg += " Exception while exporting " \
                                      "fabric %s" \
                                      " %s" % (fabric_fq_name, str(exc))
        self.result[self._get_fq_name_str(fabric_fq_name)] = fabric_report
        _task_done()
    # end _export_fabric

    def _export_interface(self, pr, pr_cache, vnc_lib):
        physical_interface_list = pr.get_physical_interfaces()
        pi_cache_list = list()
        li_cache_list = list()
        if physical_interface_list is not None:
            for physical_interface_info in physical_interface_list:
                # read the physical interface
                pi_fq_name = physical_interface_info.get('to')
                pi = vnc_lib.physical_interface_read(
                    fq_name=pi_fq_name)
                pi_cache = PhysicalInterfaceCache(pi)
                pi_cache_list.append(pi_cache)

                mac_address_list = None
                if pi.physical_interface_mac_addresses is not None:
                    mac_address_list =\
                        pi.physical_interface_mac_addresses.mac_address

                # read the logical interface
                logical_interface_list = pi.get_logical_interfaces()
                if logical_interface_list is not None:
                    for li_info in logical_interface_list:
                        li_fq_name = li_info.get('to')
                        li = vnc_lib.logical_interface_read(fq_name=li_fq_name)
                        li_cache = LogicalInterfaceCache(li)
                        li_cache_list.append(li_cache)

                        # read the ip instance if present
                        self._read_iip(li, mac_address_list, vnc_lib)

                # read the refs to other physical interfaces
                remote_pi_ref_list = pi.get_physical_interface_refs()
                local_pi_fq_name_str = self._get_fq_name_str(pi_fq_name)
                if remote_pi_ref_list is not None:
                    for remote_pi_ref in remote_pi_ref_list:
                        self.dc.topology_ref_map[
                            local_pi_fq_name_str] = remote_pi_ref.get("to")

            pr_cache.physical_interface_list = pi_cache_list
            pr_cache.logical_interface_list = li_cache_list

        return pr_cache
    # end _export_interface

    def _read_pr_role_info(self, pr):
        physical_router_role = pr.get_physical_router_role()
        routing_bridging_roles = pr.routing_bridging_roles
        if routing_bridging_roles is not None:
            rb_roles = routing_bridging_roles.rb_roles
        role_json = {
            "physical_router_role": physical_router_role,
            "routing_bridging_roles": {
                "rb_roles": rb_roles
            }
        }
        self.dc.pr_role_map[self._get_fq_name_str(pr.fq_name)] = \
            role_json
    # end _read_pr_role_info

    def _read_iip(self, li, mac_address_list, vnc_lib):
        instance_ip_refs = li.get_instance_ip_back_refs()
        if instance_ip_refs is not None:
            for iip_ref in instance_ip_refs:
                iip_fqname = iip_ref.get("to")
                iip = vnc_lib.instance_ip_read(fq_name=iip_fqname)
                for mac_address in mac_address_list:
                    self.dc.mac_to_ip_map[mac_address] =\
                        iip.instance_ip_address
    # end _read_iip

    def _import_data(self, job_ctxt):
        local_vnc_lib = JobVncApi.vnc_init(job_ctxt)

        # iterate through the fabric list and import the data
        for fabric_fq_name in self.dc.fabric_data_map:
            _task_log("Starting to import fabric %s " % fabric_fq_name)
            fabric_report = self._create_physical_router(
                    local_vnc_lib, self.dc.fabric_data_map.get(fabric_fq_name),
                    fabric_fq_name)
            if fabric_report.status is "failure":
                self.result_status = fabric_report.status
            _task_done()

        # create refs between pi for topology discovery
        self._topology_discovery(local_vnc_lib, fabric_report)
    # end _import_data

    def _topology_discovery(self, local_vnc_lib, fabric_report):
        try:
            _task_log("Starting to import topology data")
            # iterate through the ref map and create the refs
            for ref in self.dc.topology_ref_map:
                pi_fq_name = ref.split(":")
                object_uuid = local_vnc_lib.fq_name_to_id("physical_interface",
                                                          pi_fq_name)
                local_vnc_lib.ref_update(
                    "physical_interface", object_uuid, "physical_interface",
                    None, self.dc.topology_ref_map.get(ref), 'ADD')
            _task_done()
        except Exception as exc:
            _task_error_log(str(exc))
            _task_error_log(traceback.format_exc())
            self.status = "failure"
            fabric_report.status = "failure"
            fabric_report.error_msg = \
                "Exception while importing topology data %s" % repr(exc)
    # end _topology_discovery

    def _create_physical_router(self, local_vnc_lib, pr_cache_list,
                                fabric_fqname_str):
        fabric_report = self.result[fabric_fqname_str]
        pr_created_list = list()
        pr_creation_failure = list()
        pr_report_map = dict()
        for pr_cache in pr_cache_list:
            pr_json = pr_cache.pr_json
            _task_log("Starting to import Physical Router"
                      " %s " % self._get_fq_name_str(pr_json.get('fq_name')))
            pr_report = PhysicalRouterReport()
            pr_report_map[self._get_fq_name_str(pr_json.get('fq_name'))] = \
                pr_report
            # create/update pr
            try:
                pr_obj = self._create_pr(pr_json, local_vnc_lib,
                                         pr_created_list)
            except Exception as exc:
                msg = "Failed to create/update the PR." + str(exc)
                _task_error_log(msg)
                _task_error_log(traceback.format_exc())
                pr_report.error_msg = msg
                pr_report.status = "failure"
                fabric_report.status = "failure"
                fabric_report.error_msg += msg
                pr_creation_failure.append({
                    "physical_router_name": pr_json['fq_name'][-1],
                    "failure_msg": msg
                })
                continue

            # add ref to fabric from PR
            fabric_fqname = fabric_fqname_str.split(":")
            self._create_fabric_ref(pr_obj, fabric_fqname, pr_report,
                                    local_vnc_lib, pr_creation_failure,
                                    pr_json)

            # create ref to the node profile object
            self._create_node_profile_ref(pr_obj, pr_cache, pr_report,
                                          local_vnc_lib, pr_creation_failure,
                                          pr_json)

            # create physical interfaces
            pr_report.pi_created_list, pr_report.pi_creation_failure_list = \
                self._create_physical_interfaces(
                    local_vnc_lib, pr_cache.physical_interface_list)

            # create logical interfaces
            pr_report.li_created_list, pr_report.li_creation_failure_list = \
                self._create_logical_interfaces(
                    local_vnc_lib, pr_cache.logical_interface_list)

            if (pr_report.li_creation_failure_list is not None and \
                len(pr_report.li_creation_failure_list) > 0) or\
                    (pr_report.pi_creation_failure_list is not None and
                     len(pr_report.pi_creation_failure_list) > 0):
                pr_report.status = "failure"

            if pr_report.status is "failure":
                fabric_report.status = pr_report.status
                fabric_report.error_msg += \
                    "Failure while import PR %s " % self._get_fq_name_str(
                        pr_json.get('fq_name'))
        _task_done()

        fabric_report.pr_created_list = pr_created_list
        fabric_report.pr_failure_list = pr_creation_failure
        fabric_report.detailed_report_map = pr_report_map

        return fabric_report
    # end _import_physical_router

    def _create_pr(self, pr_json, local_vnc_lib, pr_created_list):
        phy_router_obj = None
        pr_obj = None
        try:
            cls = JobVncApi.get_vnc_cls("physical_router")
            phy_router_obj = cls.from_dict(**pr_json)
            existing_obj = local_vnc_lib.physical_router_read(
                fq_name=pr_json.get('fq_name'))
            existing_obj_dict = local_vnc_lib.obj_to_dict(existing_obj)
            pr_obj = existing_obj
            for key in pr_json:
                to_be_upd_value = pr_json[key]
                existing_value = existing_obj_dict.get(key)
                if to_be_upd_value != existing_value:
                    local_vnc_lib.physical_router_update(
                        phy_router_obj)
                    break
            pr_created_list.append(pr_json['fq_name'][-1])
        except NoIdError as exc:
            local_vnc_lib.physical_router_create(phy_router_obj)
            pr_created_list.append(pr_json['fq_name'][-1])
        pr_obj = local_vnc_lib.physical_router_read(
            fq_name=pr_json.get('fq_name'))
        return pr_obj
    # end _create_physical_router

    def _create_node_profile_ref(self, pr_obj, pr_cache, pr_report,
                                 local_vnc_lib, pr_creation_failure, pr_json):
        try:
            local_vnc_lib.ref_update(
                "physical_router", pr_obj.uuid, "node_profile", None,
                pr_cache.node_profile_fq_name, 'ADD')
        except Exception as exc:
            msg = "Failed to create ref to node profile" + str(exc)
            _task_error_log(str(exc))
            _task_error_log(traceback.format_exc())
            pr_report.error_msg = msg
            pr_report.status = "failure"
            pr_creation_failure.append({
                "physical_router_name": pr_json['fq_name'][-1],
                "failure_msg": msg
            })
    # end _create_node_profile_ref

    def _create_fabric_ref(self, pr_obj, fabric_fqname, pr_report,
                           local_vnc_lib, pr_creation_failure, pr_json):
        try:
            local_vnc_lib.ref_update(
                "physical_router", pr_obj.uuid, "fabric", None,
                fabric_fqname, 'ADD')
        except Exception as exc:
            msg = "Failed to create ref to fabric" + str(exc)
            _task_error_log(str(exc))
            _task_error_log(traceback.format_exc())
            pr_report.error_msg = msg
            pr_report.status = "failure"
            pr_creation_failure.append({
                "physical_router_name": pr_json['fq_name'][-1],
                "failure_msg": msg
            })
    # end _create_fabric_ref

    def _create_physical_interfaces(self, vnc_lib,
                                    physical_interfaces_payload):
        object_type = "physical_interface"
        success_intfs_names = []
        phy_intf_failed_info = []

        for phy_interface_cache in physical_interfaces_payload:
            phy_interface_dict = phy_interface_cache.pi_json
            try:
                try:
                    cls = JobVncApi.get_vnc_cls(object_type)
                    phy_interface_obj = cls.from_dict(**phy_interface_dict)
                    existing_obj = vnc_lib.physical_interface_read(
                        fq_name=phy_interface_dict.get('fq_name'))
                    existing_obj_dict = vnc_lib.obj_to_dict(existing_obj)
                    for key in phy_interface_dict:
                        to_be_upd_value = phy_interface_dict[key]
                        existing_value = existing_obj_dict.get(key)
                        if to_be_upd_value != existing_value:
                            vnc_lib.physical_interface_update(
                                phy_interface_obj)
                            break
                    success_intfs_names.append(
                        phy_interface_dict['fq_name'][-1])
                except NoIdError as exc:
                    vnc_lib.physical_interface_create(phy_interface_obj)
                    success_intfs_names.append(
                        phy_interface_dict['fq_name'][-1])
            except Exception as exc:
                _task_error_log(str(exc))
                _task_error_log(traceback.format_exc())
                phy_intf_failed_info.append({
                    "phy_interface_name": phy_interface_dict['fq_name'][-1],
                    "failure_msg": str(exc)
                })
        return success_intfs_names, phy_intf_failed_info
        # end _create_physical_interfaces

    def _create_logical_interfaces(self, vnc_lib,
                                   logical_interfaces_payload):
        object_type = "logical_interface"
        success_intfs_names = []
        log_intf_failed_info = []

        for log_interface_cache in logical_interfaces_payload:
            log_interface_dict = log_interface_cache.li_json
            try:
                try:
                    cls = JobVncApi.get_vnc_cls(object_type)
                    log_interface_obj = cls.from_dict(**log_interface_dict)
                    existing_obj = vnc_lib.logical_interface_read(
                        fq_name=log_interface_dict.get('fq_name'))
                    existing_obj_dict = vnc_lib.obj_to_dict(existing_obj)
                    for key in log_interface_dict:
                        to_be_upd_value = log_interface_dict[key]
                        existing_value = existing_obj_dict.get(key)
                        if to_be_upd_value != existing_value:
                            vnc_lib.logical_interface_update(log_interface_obj)
                            break
                    success_intfs_names.append(
                        log_interface_dict['fq_name'][-1])
                except NoIdError as exc:
                    vnc_lib.logical_interface_create(log_interface_obj)
                    success_intfs_names.append(
                        log_interface_dict['fq_name'][-1])
            except Exception as exc:
                _task_error_log(str(exc))
                _task_error_log(traceback.format_exc())
                log_intf_failed_info.append({
                    "log_interface_name": log_interface_dict['fq_name'][-1],
                    "failure_msg": str(exc)
                })
        return success_intfs_names, log_intf_failed_info

    # end _create_logical_interfaces

    def _get_fq_name_str(self, fq_name):
        return ':'.join(map(str, fq_name))
    # end _get_fq_name_str

# *********************************** Test ************************************

def _mock_job_ctx():
    return {
        "auth_token": "",
        "api_server_host": ["10.155.75.161"],
        "config_args": {
            "collectors": [
                "10.155.75.161:8086"
            ],
            "fabric_ansible_conf_file": [
                "/etc/contrail/contrail-keystone-auth.conf",
                "/etc/contrail/contrail-fabric-ansible.conf"
            ]
        },
        "job_execution_id": "c37b199a-effb-4469-aefa-77f531f77758",
        "job_input": {
            "fabric_list": [
                {
                    "fabric_fq_name": ["default-global-system-config", "fab01"]
                }
            ],
            "external_contrail_params": {
                "admin_user": "admin",
                "admin_password": "contrail123",
                "admin_tenant_name": "admin",
                "api_server_port": "8082",
                "api_server_use_ssl": False,
                "api_server_ip_list": ["10.155.75.171"]
            }
        },
        "job_template_fqname": [
            "default-global-system-config",
            "migrate_data_template"
        ]
    }
    # end _mock_job_ctx_delete_fabric


def __main__():
    migrate_data_filter = FilterModule()
    job_ctxt = _mock_job_ctx()
    migrate_data_filter.migrate_data(job_ctxt)
# end __main__


if __name__ == '__main__':
    __main__()