#!/usr/bin/python

import traceback
import sys
from enum import Enum
import json

from job_manager.job_utils import JobVncApi
from vnc_api.exceptions import NoIdError

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
from filter_utils import FilterLog, _task_log, _task_done, \
    _task_error_log, _task_info_log, _task_debug_log
from fabric_filter_utils import FabricFilterUtils


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
    # map of the physical interface name to physical interface name giving
    # the topology
    topology_ref_map = dict()
    """
    Mapping of the fabric fq name to list of PR role info
    { "fabric_fqname": [
                            {
                                "device_fq_name": [
                                    "default-global-system-config",
                                    "qfx-10"
                                ],
                                "physical_role": "leaf",
                                "routing_bridging_roles": [ "CRB-Access" ]
                            }
                        ]
    }
    """
    pr_role_data = dict()

class MigrationStatus(Enum):
    SUCCESS = "SUCCESS"
    FAILURE = "FAILURE"

# ****************************************************************************


class FilterModule(object):
    dc = DataCache()
    fabric_utils = FabricFilterUtils()
    status = MigrationStatus.SUCCESS

    def filters(self):
        return {
            'migrate_fabric_data': self.migrate_fabric_data,
        }
    # end filters

    def _instantiate_filter_log_instance(self):
        FilterLog.instance("MigrateFabricDataFilter")
    # end _instantiate_filter_log_instance

    def migrate_fabric_data(self,job_ctxt):

        self._instantiate_filter_log_instance()
        _task_log("Starting data migration")
        error_msg = None
        try:
            # create local vnc api
            local_vnc_api = JobVncApi.vnc_init(job_ctxt)
            if local_vnc_api is None:
                msg = "Unable to init local VNC api"
                raise Exception(msg)

            # TODO validate job ctxt
            # job_input, job_template_fqname =
            # self.fabric_utils.validate_job_ctx(vnc_api, job_ctxt, True,
            #                                    is_migration=False)
            job_input = job_ctxt.get("job_input")
            job_template_fqname = job_ctxt.get("job_template_fqname")

            # create vnc api for remote server
            vnc_params = job_input.get("external_contrail_params")
            remote_job_ctxt = {
                "api_server_host": vnc_params.get("api_server_ip_list"),
                "vnc_api_init_params": {
                    "admin_user": vnc_params.get("admin_user"),
                    "admin_password": vnc_params.get("admin_password"),
                    "admin_tenant_name": vnc_params.get("admin_tenant_name"),
                    "api_server_port": vnc_params.get("api_server_port"),
                    "api_server_use_ssl": vnc_params.get("api_server_use_ssl")
                }
            }
            remote_vnc_api = JobVncApi.vnc_init(remote_job_ctxt)
            if remote_vnc_api is None:
                msg = "Unable to init remote VNC api"
                raise Exception(msg)

            local_fabric_fqnames = self._migrate_all_fabric(remote_vnc_api,
                                                            local_vnc_api,
                                                            job_template_fqname)

            # import the topology data
            self._import_topology_data(local_vnc_api)

            # Iterate through the fabrics and complete role assignment
            for fabric_fq_name in local_fabric_fqnames:
                try:
                    self._assign_roles(fabric_fq_name, local_vnc_api)
                except Exception as exception:
                    # log error, mark status and continue processing
                    msg = "Failed to assign roles for fabric %s %s " % \
                          (fabric_fq_name, repr(exception))
                    _task_error_log('%s\n%s' % (msg, traceback.format_exc()))
                    _task_done(msg)
                    self.status = MigrationStatus.FAILURE
        except Exception as exception:
            errmsg = str(exception)
            _task_error_log('%s\n%s' % (errmsg, traceback.format_exc()))
        finally:
            status = 'success' if self.status is  MigrationStatus.SUCCESS \
                else 'failure'
            return {
                'status': status,
                'error_msg': error_msg,
                'migration_log': FilterLog.instance().dump()
            }
        _task_done("Completed data migration with status %s " % status)
    # end migrate_data

    def _migrate_all_fabric(self, remote_vnc_api, local_vnc_api,
                            job_template_fqname):
        # read the fabric fq_names from the remote server
        fabric_list = list()
        try:
            result = remote_vnc_api.fabrics_list(fields=['fq_name'])
            if result is not None:
                fabric_list = result.get("fabrics")
        except Exception as exception:
            msg = "Failed to read fabrics from remote server"
            _task_error_log(msg)
            raise exception
        # iterate through - onboard and migrate fabric into local server
        local_fabric_fqnames = list()
        for fabric in fabric_list:
            _task_log("Migrating fabric %s " % fabric["fq_name"])
            try:
                # read fabric from remote server
                fabric_obj = remote_vnc_api.fabric_read(
                    fq_name=fabric["fq_name"])
                if fabric_obj is None:
                    raise Exception("Failed to read fabric %s "
                                    "from remote server" % fabric["fq_name"])

                # onboard fabric into local server
                _task_log("On-boarding fabric")

                job_input = self._build_onboard_fabric_input(remote_vnc_api,
                                                             fabric_obj)
                # onboard fabric local server
                local_fabric_obj = self.fabric_utils.onboard_fabric_obj(
                    local_vnc_api, job_input, False, job_template_fqname)
                if local_fabric_obj is not None:
                    local_fabric_fqnames.append(local_fabric_obj.fq_name)
                else:
                    raise Exception("Fabric object not created")
                _task_done()
                # migrate data under the fabric
                self._migrate_fabric(fabric_obj, local_fabric_obj,
                                     remote_vnc_api, local_vnc_api)
            except Exception as exception:
                # log error, mark status and continue processing other fabrics
                msg = "Failed to migrate fabric %s %s " % \
                      (fabric["fq_name"], repr(exception))
                _task_error_log('%s\n%s' % (msg, traceback.format_exc()))
                _task_done(msg)
                self.status = MigrationStatus.FAILURE
        return local_fabric_fqnames
    # end _migrate_all_fabric

    @staticmethod
    def _get_management_subnets(remote_vnc_api, uuid):
        """
        returns a list of dicts

        [{"cidr": "10.1.1.1/24"}, {"cidr": "10.1.1.2/24"}]
        """
        # TODO ipv6 support
        management_subnet_list = list()
        namespace = remote_vnc_api.fabric_namespace_read(id=uuid)
        if namespace.get_fabric_namespace_value() is not None and\
                namespace.get_fabric_namespace_value().get_ipv4_cidr()\
                is not None:
            subnet_list = \
                namespace.get_fabric_namespace_value().get_ipv4_cidr().\
                get_subnet()
            for subnet in subnet_list:
                management_subnet = \
                    {"cidr": subnet.get_ip_prefix() + "/" +\
                     str(subnet.get_ip_prefix_len())}
                management_subnet_list.append(management_subnet)

        return management_subnet_list
    # end _get_management_subnets

    @staticmethod
    def _get_subnet_ip_list(remote_vnc_api, namespace_uuid):
        """
            returns a list of ips
        """
        # TODO ipv6 support
        ip_subnet_list = list()
        namespace = remote_vnc_api.fabric_namespace_read(id=namespace_uuid)
        if namespace.get_fabric_namespace_value() is not None and\
                namespace.get_fabric_namespace_value().get_ipv4_cidr()\
                is not None:
            subnet_list = \
                namespace.get_fabric_namespace_value().get_ipv4_cidr().\
                get_subnet()
            for subnet in subnet_list:
                ip_subnet_list.append(
                    subnet.get_ip_prefix() + "/" +\
                    str(subnet.get_ip_prefix_len()))
        return ip_subnet_list
    # end _get_subnet_ip_list

    @staticmethod
    def _get_asn_range_list(remote_vnc_api, namespace_uuid):
        """
        returns a list of dicts
        [
                {
                    "asn_max": 65000,
                    "asn_min": 64000
                },
                {
                    "asn_max": 65100,
                    "asn_min": 65000
                }
        ]

        """
        namespace = remote_vnc_api.fabric_namespace_read(id=namespace_uuid)
        if namespace.get_fabric_namespace_value() is not None and\
                namespace.get_fabric_namespace_value().get_asn_ranges()\
                is not None:
            asn_range_list = \
                namespace.get_fabric_namespace_value().get_asn_ranges()
            asn_range_col = list()
            for asn_range in asn_range_list:
                asn_range_dict = dict()
                asn_range_dict["asn_max"] = asn_range.get_asn_max()
                asn_range_dict["asn_min"] = asn_range.get_asn_min()
                asn_range_col.append(asn_range_dict)
        return asn_range_col
    # end _get_asn_range_list

    def _get_node_profiles(self, remote_vnc_api, fabric):
        """
        returns a list of dicts

        [{"node_profile_name": "juniper-qfx5k"},
         {"node_profile_name": "juniper-qfx5k-lean"}]
        """
        node_profile_refs = fabric.get_node_profile_refs()
        node_profile_list = list()
        for node_profile_ref in node_profile_refs:
            node_profile_dict = dict()
            node_profile_dict["node_profile_name"] = \
                node_profile_ref.get("to")[-1]
            node_profile_list.append(node_profile_dict)
        return node_profile_list
    # end _get_node_profiles

    def _build_onboard_fabric_input(self, remote_vnc_api, fabric):
        # TODO device credentials
        management_subnets = None
        fabric_asn_pool = None
        loopback_subnets = None
        fabric_subnets = None

        # iterate through the fabric namespaces and collect the reqd input
        namespaces = fabric.get_fabric_namespaces()
        for namespace in namespaces:
            namespace_fq_name = namespace.get("to")
            namespace_uuid = namespace.get("uuid")
            namespace_type = namespace_fq_name[-1]
            if namespace_type == "management-subnets":
                management_subnets = \
                    self._get_management_subnets(remote_vnc_api,
                                                 namespace_uuid)
            elif namespace_type == "loopback-subnets":
                loopback_subnets = self._get_subnet_ip_list(remote_vnc_api,
                                                            namespace_uuid)
            elif namespace_type == "fabric-subnets":
                fabric_subnets = self._get_subnet_ip_list(remote_vnc_api,
                                                          namespace_uuid)
            elif namespace_type == "eBGP-ASN-pool":
                fabric_asn_pool = self._get_asn_range_list(remote_vnc_api,
                                                          namespace_uuid)
        node_profiles = self._get_node_profiles(remote_vnc_api, fabric)

        job_input_json = {
                "fabric_fq_name": fabric.fq_name,
                "device_auth": [
                    {
                        'username': 'root',
                        'password':'Embe1mpls'
                    }
                ],
                "fabric_asn_pool": fabric_asn_pool,
                "fabric_subnets": fabric_subnets,
                "loopback_subnets": loopback_subnets,
                "management_subnets": management_subnets,
                "node_profiles": node_profiles
        }
        _task_info_log("Onboarding input json: \n %s" % job_input_json)
        return job_input_json
    # end _build_onboard_fabric_input

    def _migrate_fabric(self, remote_fabric_obj, local_fabric_object,
                        remote_vnc_api, local_vnc_api):
        _task_log("Migrating physical routers")
        pr_refs = remote_fabric_obj.get_physical_router_back_refs()
        if pr_refs is not None:
            for pr_ref in pr_refs:
                try:
                    _task_log("Exporting physical router"
                              " %s" % pr_ref.get("to"))
                    # export PR
                    pr_uuid = pr_ref.get("uuid")
                    pr_cache = self._export_physical_router(
                        remote_vnc_api, pr_uuid, remote_fabric_obj.fq_name)
                    _task_done()

                    _task_log(
                        "Importing physical router")
                    # import PR
                    pr_obj = self._import_physical_router(
                        pr_cache, local_fabric_object, local_vnc_api)
                    _task_done()
                except Exception as exception:
                    # log error, mark status and continue processing other prs
                    msg = "Failed to migrate physical router %s %s " % \
                          (pr_ref.get("to"), repr(exception))
                    _task_error_log('%s\n%s' % (msg, traceback.format_exc()))
                    _task_done(msg)
                    self.status = MigrationStatus.FAILURE
            _task_done()
        else:
            _task_done("No physical routers found for fabric")
    # end _migrate_fabric

    def _export_physical_router(self, remote_vnc_api, pr_uuid, fabric_fqname):
        # read the PR device
        pr = remote_vnc_api.physical_router_read(id=pr_uuid)
        pr_cache = PhysicalRouterCache(pr)

        # read the ref to node profile
        node_profile_refs = pr.get_node_profile_refs()
        if node_profile_refs is not None and \
                len(node_profile_refs) > 0:
            pr_cache.node_profile_fq_name = \
                node_profile_refs[0].get("to")

        # read the role info
        self._read_pr_role_info(pr, fabric_fqname)

        # read the physical and logical interfaces data
        pr_cache = self._export_interface(pr, pr_cache,
                                          remote_vnc_api)
        _task_debug_log("Create PR JSON %s " % json.dumps(pr_cache.pr_json))
        return pr_cache
    # end _export_physical_router

    def _read_pr_role_info(self, pr, fabric_fqname):
        fabric_fqname_str = self._get_fq_name_str(fabric_fqname)
        routing_bridging_roles = pr.routing_bridging_roles
        rb_roles = list()
        if routing_bridging_roles is not None:
            rb_roles = routing_bridging_roles.rb_roles
        pr_role_dict = dict()
        pr_role_dict["device_fq_name"] = pr.fq_name
        pr_role_dict["physical_role"] = pr.get_physical_router_role()
        pr_role_dict["routing_bridging_roles"] = rb_roles

        role_list = self.dc.pr_role_data.get(fabric_fqname_str)
        if role_list is None:
            self.dc.pr_role_data[fabric_fqname_str] = [pr_role_dict]
        else:
            role_list.append(pr_role_dict)
        _task_debug_log("Role info for the PR %s " % json.dumps(pr_role_dict))
    # end _read_pr_role_info

    def _export_interface(self, pr, pr_cache, vnc_lib):
        _task_info_log("Exporting interfaces for PR %s " % pr.fq_name)
        physical_interface_list = pr.get_physical_interfaces()
        pi_cache_list = list()
        li_cache_list = list()
        if physical_interface_list is not None:
            for physical_interface_info in physical_interface_list:
                # read the physical interface
                pi_fq_name = physical_interface_info.get('to')
                _task_debug_log("Exporting interface %s " % pi_fq_name)
                pi = vnc_lib.physical_interface_read(
                    fq_name=pi_fq_name)
                pi_cache = PhysicalInterfaceCache(pi)
                pi_cache_list.append(pi_cache)

                # read the logical interface
                logical_interface_list = pi.get_logical_interfaces()
                if logical_interface_list is not None:
                    for li_info in logical_interface_list:
                        li_fq_name = li_info.get('to')
                        li = vnc_lib.logical_interface_read(fq_name=li_fq_name)
                        li_cache = LogicalInterfaceCache(li)
                        li_cache_list.append(li_cache)

                # read the refs to other physical interfaces
                remote_pi_ref_list = pi.get_physical_interface_refs()
                local_pi_fq_name_str = self._get_fq_name_str(pi_fq_name)
                if remote_pi_ref_list is not None:
                    for remote_pi_ref in remote_pi_ref_list:
                        self.dc.topology_ref_map[
                            local_pi_fq_name_str] = remote_pi_ref.get("to")

            pr_cache.physical_interface_list = pi_cache_list
            pr_cache.logical_interface_list = li_cache_list
        _task_info_log("Completed exporting interfaces")
        return pr_cache
    # end _export_interface

    def _import_physical_router(self, pr_cache, local_fabric, local_vnc_lib):
        pr_obj = self._create_pr(pr_cache.pr_json, local_vnc_lib)
        _task_info_log("Created PR")

        # add ref to fabric from PR
        self._create_fabric_ref(pr_obj, local_fabric.fq_name, local_vnc_lib)
        _task_debug_log("Created fabric ref")

        # create ref to the node profile object
        self._create_node_profile_ref(pr_obj, pr_cache, local_vnc_lib)
        _task_debug_log("Created node profile ref")

        # create physical interfaces
        self._create_physical_interfaces(
                local_vnc_lib, pr_cache.physical_interface_list)
        _task_info_log("Created physical interfaces")

        # create logical interfaces
        self._create_logical_interfaces(
                local_vnc_lib, pr_cache.logical_interface_list)
        _task_info_log("Created logical interfaces")
        return pr_obj
    # end _import_physical_router

    def _create_pr(self, pr_json, local_vnc_lib):

        # create/update PR
        new_pr_obj = None
        try:
            cls = JobVncApi.get_vnc_cls("physical_router")
            new_pr_obj = cls.from_dict(**pr_json)
            existing_obj = local_vnc_lib.physical_router_read(
                fq_name=pr_json.get('fq_name'))
            existing_obj_dict = local_vnc_lib.obj_to_dict(existing_obj)
            for key in pr_json:
                to_be_upd_value = pr_json[key]
                existing_value = existing_obj_dict.get(key)
                if to_be_upd_value != existing_value:
                    local_vnc_lib.physical_router_update(new_pr_obj)
                    break
        except NoIdError as exc:
            local_vnc_lib.physical_router_create(new_pr_obj)

        # read latest PR
        pr_obj = local_vnc_lib.physical_router_read(
            fq_name=pr_json.get('fq_name'))

        return pr_obj
    # end _create_pr

    @staticmethod
    def _create_node_profile_ref(pr_obj, pr_cache, local_vnc_lib):
        local_vnc_lib.ref_update(
            "physical_router", pr_obj.uuid, "node_profile", None,
            pr_cache.node_profile_fq_name, 'ADD')
    # end _create_node_profile_ref

    @staticmethod
    def _create_fabric_ref(pr_obj, fabric_fqname, local_vnc_lib):
        local_vnc_lib.ref_update(
            "physical_router", pr_obj.uuid, "fabric", None,
            fabric_fqname, 'ADD')
    # end _create_fabric_ref

    @staticmethod
    def _create_physical_interfaces(vnc_lib,
                                    physical_interfaces_payload):
        object_type = "physical_interface"

        for phy_interface_cache in physical_interfaces_payload:
            phy_interface_dict = phy_interface_cache.pi_json
            _task_debug_log("Importing physical interface "
                            "%s " % phy_interface_dict.get('fq_name'))
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
            except NoIdError as exc:
                vnc_lib.physical_interface_create(phy_interface_obj)
    # end _create_physical_interfaces

    @staticmethod
    def _create_logical_interfaces(vnc_lib,
                                   logical_interfaces_payload):
        object_type = "logical_interface"

        for log_interface_cache in logical_interfaces_payload:
            log_interface_dict = log_interface_cache.li_json
            _task_debug_log("Importing physical interface "
                            "%s " % log_interface_dict.get('fq_name'))
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
            except NoIdError as exc:
                vnc_lib.logical_interface_create(log_interface_obj)
    # end _create_logical_interfaces

    def _import_topology_data(self, local_vnc_lib):
        _task_log("Importing topology data")
        # iterate through the ref map and create the refs
        for ref in self.dc.topology_ref_map:
            pi_fq_name = ref.split(":")
            object_uuid = local_vnc_lib.fq_name_to_id("physical_interface",
                                                      pi_fq_name)
            local_vnc_lib.ref_update(
                "physical_interface", object_uuid, "physical_interface",
                None, self.dc.topology_ref_map.get(ref), 'ADD')
        _task_done()
    # end _import_topology_data

    def _assign_roles(self, fabric_fq_name, local_vnc_api):
        _task_log("Starting role assignment for fabric %s" % fabric_fq_name)
        # build the job ctxt for the assign role
        job_input = \
            self.dc.pr_role_data.get(self._get_fq_name_str(fabric_fq_name))

        if job_input is None:
            msg = "Role assignment data not found"
            raise Exception(msg)

        job_ctxt = {
            "job_input":{
                "role_assignments": job_input,
                "fabric_fq_name": fabric_fq_name
            }
        }
        import pdb;pdb.set_trace()
        result = self.fabric_utils.assign_roles(job_ctxt, local_vnc_api)
        if result.get("status") is 'failure':
            _task_error_log(result.get("assignment_log"))
            msg = "Role assignment result is Failure"
            raise Exception(msg)
    # end _assign_roles

    @staticmethod
    def _get_fq_name_str(fq_name):
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
            "migrate_fabric_data_template"
        ]
    }
    # end _mock_job_ctx


def __main__():
    migrate_data_filter = FilterModule()
    job_ctxt = _mock_job_ctx()
    migrate_data_filter.migrate_fabric_data(job_ctxt)
# end __main__


if __name__ == '__main__':
    __main__()