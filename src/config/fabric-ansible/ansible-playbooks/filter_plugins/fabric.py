#!/usr/bin/python
#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation for fabric related Ansible filter plugins
"""
import logging
import sys

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
from filter_utils import FilterLog, _task_log, _task_done,\
    _task_error_log, _task_debug_log, _task_warn_log
from fabric_filter_utils import FabricFilterUtils


class FilterModule(object):
    """Fabric filter plugins"""
    fabric_filter_utils = FabricFilterUtils()

    def filters(self):
        """Fabric filters"""
        return {
            'onboard_fabric': self.onboard_fabric,
            'onboard_existing_fabric': self.onboard_brownfield_fabric,
            'delete_fabric': self.delete_fabric,
            'delete_devices': self.delete_fabric_devices,
            'assign_roles': self.assign_roles
        }
    # end filters

    def onboard_fabric(self, job_ctxt):
        return self.fabric_filter_utils.onboard_fabric(job_ctxt)

    def onboard_brownfield_fabric(self, job_ctxt):
        return self.fabric_filter_utils.onboard_brownfield_fabric(job_ctxt)

    def delete_fabric(self, job_ctxt):
        return self.fabric_filter_utils.delete_fabric(job_ctxt)

    def delete_fabric_devices(self, job_ctxt):
        return self.fabric_filter_utils.delete_fabric_devices(job_ctxt)

    def assign_roles(self, job_ctxt):
        return self.fabric_filter_utils.assign_roles(job_ctxt)

# ***************** tests *****************************************************
def _mock_job_ctx_onboard_fabric():
    return {
        "auth_token": "",
        "config_args": {
            "collectors": [
                "10.155.75.181:8086"
            ],
            "fabric_ansible_conf_file": [
                "/etc/contrail/contrail-keystone-auth.conf",
                "/etc/contrail/contrail-fabric-ansible.conf"
            ]
        },
        "job_execution_id": "c37b199a-effb-4469-aefa-77f531f77758",
        "job_input": {
            "fabric_fq_name": ["default-global-system-config", "fab01"],
            "device_auth": {
                "root_password": "Embe1mpls"
            },
            "fabric_asn_pool": [
                {
                    "asn_max": 65000,
                    "asn_min": 64000
                },
                {
                    "asn_max": 65100,
                    "asn_min": 65000
                }
            ],
            "fabric_subnets": [
                "30.1.1.1/24"
            ],
            "loopback_subnets": [
                "20.1.1.1/24"
            ],
            "management_subnets": [
                {"cidr": "10.1.1.1/24", "gateway": "10.1.1.1"}
            ],
            "node_profiles": [
                {
                    "node_profile_name": "juniper-qfx5k"
                }
            ],
            "device_count": 5
        },
        "job_template_fqname": [
            "default-global-system-config",
            "fabric_onboard_template"
        ]
    }
# end _mock_job_ctx_onboard_fabric


def _mock_job_ctx_onboard_brownfield_fabric():
    return {
        "auth_token": "",
        "config_args": {
            "collectors": [
                "10.155.75.181:8086"
            ],
            "fabric_ansible_conf_file": [
                "/etc/contrail/contrail-keystone-auth.conf",
                "/etc/contrail/contrail-fabric-ansible.conf"
            ]
        },
        "job_execution_id": "c37b199a-effb-4469-aefa-77f531f77758",
        "job_input": {
            "fabric_fq_name": ["default-global-system-config", "fab01"],
            "device_auth": [{
                "username": "root",
                "password": "Embe1mpls"
            }],
            "management_subnets": [
                {"cidr": "10.1.1.1/24"}
            ],
            "overlay_ibgp_asn": 64600,
            "node_profiles": [
                {
                    "node_profile_name": "juniper-qfx5k"
                }
            ]
        },
        "job_template_fqname": [
            "default-global-system-config",
            "existing_fabric_onboard_template"
        ]
    }
# end _mock_job_ctx_onboard_fabric


def _mock_job_ctx_delete_fabric():
    return {
        "auth_token": "",
        "config_args": {
            "collectors": [
                "10.155.75.181:8086"
            ],
            "fabric_ansible_conf_file": [
                "/etc/contrail/contrail-keystone-auth.conf",
                "/etc/contrail/contrail-fabric-ansible.conf"
            ]
        },
        "job_execution_id": "c37b199a-effb-4469-aefa-77f531f77758",
        "job_input": {
            "fabric_fq_name": ["default-global-system-config", "fab01"]
        },
        "job_template_fqname": [
            "default-global-system-config",
            "fabric_deletion_template"
        ]
    }
# end _mock_job_ctx_delete_fabric


def _mock_job_ctx_delete_devices():
    return {
        "auth_token": "",
        "config_args": {
            "collectors": [
                "10.155.75.181:8086"
            ],
            "fabric_ansible_conf_file": [
                "/etc/contrail/contrail-keystone-auth.conf",
                "/etc/contrail/contrail-fabric-ansible.conf"
            ]
        },
        "job_execution_id": "c37b199a-effb-4469-aefa-77f531f77758",
        "job_input": {
            "fabric_fq_name": ["default-global-system-config", "fab01"],
            "devices": ['DK588', 'VF3717350117']
        },
        "job_template_fqname": [
            "default-global-system-config",
            "device_deletion_template"
        ]
    }
# end _mock_job_ctx_delete_fabric


def _mock_job_ctx_assign_roles():
    return {
        "auth_token": "",
        "config_args": {
            "collectors": [
                "10.155.75.181:8086"
            ],
            "fabric_ansible_conf_file": [
                "/etc/contrail/contrail-keystone-auth.conf",
                "/etc/contrail/contrail-fabric-ansible.conf"
            ]
        },
        "job_execution_id": "c37b199a-effb-4469-aefa-77f531f77758",
        "job_input": {
            "fabric_fq_name": ["default-global-system-config", "fab01"],
            "role_assignments": [
                {
                    "device_fq_name": [
                        "default-global-system-config",
                        "DK588"
                    ],
                    "physical_role": "spine",
                    "routing_bridging_roles": ["CRB-Gateway"]
                },
                {
                    "device_fq_name": [
                        "default-global-system-config",
                        "VF3717350117"
                    ],
                    "physical_role": "leaf",
                    "routing_bridging_roles": ["CRB-Access"]
                }
            ]
        },
        "job_template_fqname": [
            "default-global-system-config",
            "role_assignment_template"
        ]
    }
# end _mock_job_ctx_delete_fabric


def _mock_supported_roles():
    return {
        "juniper-qfx5100-48s-6q": [
            "CRB-Access@leaf",
            "null@spine"
        ],
        "juniper-qfx10002-72q": [
            "null@spine",
            "CRB-Gateway@spine",
            "DC-Gateway@spine",
            "DCI-Gateway@spine",
            "CRB-Access@leaf",
            "CRB-Gateway@leaf",
            "DC-Gateway@leaf"
            "DCI-Gateway@leaf"
        ]
    }
# end _mock_supported_roles


def _parse_args():
    arg_parser = argparse.ArgumentParser(description='fabric filters tests')
    arg_parser.add_argument('-c', '--create_fabric',
                            action='store_true', help='Onbaord fabric')
    arg_parser.add_argument('-ce', '--create_existing_fabric',
                            action='store_true', help='Onbaord existing fabric')
    arg_parser.add_argument('-df', '--delete_fabric',
                            action='store_true', help='Delete fabric')
    arg_parser.add_argument('-dd', '--delete_devices',
                            action='store_true', help='Delete devices')
    arg_parser.add_argument('-a', '--assign_roles',
                            action='store_true', help='Assign roles')
    return arg_parser.parse_args()
# end _parse_args


def __main__():
    _parse_args()

    fabric_filter = FilterModule()
    parser = _parse_args()
    results = {}
    if parser.create_fabric:
        results = fabric_filter.onboard_fabric(_mock_job_ctx_onboard_fabric())
    elif parser.create_existing_fabric:
        results = fabric_filter.onboard_brownfield_fabric(
            _mock_job_ctx_onboard_brownfield_fabric()
        )
    elif parser.delete_fabric:
        results = fabric_filter.delete_fabric(_mock_job_ctx_delete_fabric())
    elif parser.delete_devices:
        results = fabric_filter.delete_fabric_devices(
            _mock_job_ctx_delete_devices()
        )
    elif parser.assign_roles:
        results = fabric_filter.assign_roles(_mock_job_ctx_assign_roles())

    print results
# end __main__


if __name__ == '__main__':
    __main__()
