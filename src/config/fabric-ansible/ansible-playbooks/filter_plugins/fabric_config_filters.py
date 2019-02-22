#!/usr/bin/python

#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains code to support fabric device configuration operations
"""

import sys
import argparse
import os
from jinja2 import FileSystemLoader, Environment, ext
import yaml
import json
import shutil
import traceback

PLAYBOOK_BASE = 'opt/contrail/fabric_ansible_playbooks'

sys.path.append(PLAYBOOK_BASE+"/module_utils")
from filter_utils import FilterLog, _task_log, _task_done,\
     _task_error_log, _task_debug_log, _task_warn_log, vnc_bulk_get



class FilterModule(object):

    def filters(self):
        return {
            'render_fabric_config': self.render_fabric_config
        }
    # end filters

    def _load_role_to_feature_mapping(self):
        with open('./group_vars/all.yml') as f:
            group_vars = yaml.load(f)
        role_to_feature_mapping = group_vars['role_to_feature_mapping']
        self.role_to_feature_mapping = role_to_feature_mapping

    def _load_abstract_config(self):
        self.abstract_config_path = self.conf_dir+'/abstract_cfg.json'
        with open(self.abstract_config_path) as f:
            ab_cfg = f.read()
        self.abstract_config = json.loads(ab_cfg)

    def _prepare_build_env(self):
        self.final_config_path = self.conf_dir+'/final_config'
        self.combined_config_path = \
            self.final_config_path + '/combined_config.conf'
        shutil.rmtree(self.final_config_path, ignore_errors=True)

        if self.is_delete and os.path.exists(self.abstract_config_path):
            os.remove(self.abstract_config_path)

        os.mkdir(self.final_config_path)

    def _initialize(self):
        self.final_config = ""
        self.config_templates = []
        os.chdir(PLAYBOOK_BASE)

        input = self.job_ctx['job_input']
        self.input = input
        self.device_mgmt_ip = input['device_management_ip']
        self.additional_feature_params = input['additional_feature_params']
        self.is_delete = input['is_delete']
        self.manage_underlay = input['manage_underlay']

        self.conf_dir = './config/' + self.device_mgmt_ip
        self._load_abstract_config()
        device_info = self.abstract_config['system']
        self.device_info = device_info
        self.device_name = device_info['name']
        self.device_vendor = device_info['vendor_name'].lower()
        self.device_family = device_info['device_family'].lower()
        self.device_model = device_info['product_name']
        self.device_username = device_info['credentials']['user_name']
        self.device_password = device_info['credentials']['password']
        self.device_phy_role = device_info['physical_role']
        self.device_rb_roles = device_info.get('routing_bridging_roles', [])
        if not self.device_rb_roles:
            self.device_rb_roles = ['null']
        self._prepare_build_env()
        self._load_role_to_feature_mapping()

    def _get_feature_list(self):
        dev_feature_list = set()
        for rb_role in self.device_rb_roles:
            role = rb_role + '@' + self.device_phy_role
            role_features = self.role_to_feature_mapping.get(role, [])
            dev_feature_list |= set(role_features)
        self.dev_feature_list = list(dev_feature_list)

    def _get_feature_template(self, feature):
        feature_template_dir = './roles/cfg_' + feature + '/templates'
        template_list = os.listdir(feature_template_dir)

        # Find regexp templates first
        # TODO

        # Find vendor-family-feature templates
        vf_feature = '_'.join([self.device_vendor, self.device_family,
                               feature])
        file = vf_feature + '.j2'
        if file in template_list:
            return '/'.join([feature_template_dir, file]), vf_feature

        # Find vendor-feature templates
        vf_feature = '_'.join([self.device_vendor, feature])
        file = vf_feature + '.j2'
        if file in template_list:
            return '/'.join([feature_template_dir, file]), vf_feature

        # No templates found
        return None, None

    def _render_feature_config(self, feature, template, is_empty):
        file = self.device_vendor + '_feature_config.j2'
        file_loader = FileSystemLoader('./')
        env = Environment(loader=file_loader,
                          extensions = (ext.loopcontrols, ext.do))
        templ = env.get_template('templates/'+file)
        feature_config = templ.render(
            feature=feature,
            feature_template=template,
            device_abstract_config=self.abstract_config if not
                self.is_delete else None,
            input=self.input,
            device_mgmt_ip=self.device_mgmt_ip,
            additional_feature_params=self.additional_feature_params,
            is_delete=self.is_delete,
            manage_underlay=self.manage_underlay,
            cfg_group='__contrail_' + feature + '__',
            feature_empty=is_empty
        )
        self.final_config += feature_config

    def _render_config(self):
        feature_dir_list = \
            [name for name in os.listdir("./roles") if name.startswith("cfg_")]
        feature_list = [feature_dir[4:] for feature_dir in feature_dir_list]
        for feature in feature_list:
            feature_template, vf_feature = self._get_feature_template(feature)
            if not feature_template:
                continue
            if 'underlay' in feature and not self.manage_underlay:
                continue
            self._render_feature_config(
                feature, feature_template,
                False if feature in self.dev_feature_list else True
            )
        # Write to file
        with open(self.combined_config_path, 'w') as f:
            f.write(self.final_config)

        # Generate output to be used by playbook
        render_output = {
            'conf_dir': self.conf_dir,
            'combined_config': self.combined_config_path,
            'device_mgmt_ip': self.device_mgmt_ip,
            'device_vendor': self.device_vendor,
            'device_name': self.device_name,
            'device_username': self.device_username,
            'device_password': self.device_password,
            'is_delete': self.is_delete,
            'onboard_log': FilterLog.instance().dump(),
            'results': {},
            'status': "success"
        }
        return render_output

    def render_fabric_config(self, job_ctx):
        try:
            FilterLog.instance("FabricConfigFilter")
            self.job_ctx = job_ctx
            self._initialize()
            self._get_feature_list()
            return self._render_config()
        except Exception as ex:
            errmsg = "Unexpected error: %s\n%s" % (
                str(ex), traceback.format_exc()
            )
            _task_error_log(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
                'onboard_log': FilterLog.instance().dump()
            }


def _mock_abstract_config():
    return {
        "comment": "/* Contrail Generated Group Config */",
        "bgp": [
        {
            "comment": "/* overlay_bgp: BGP Router: vqfx1-bgp, UUID: 09dbd2bd-4171-46c1-9741-aa2c85ebe803 */",
            "peers": [
                {
                    "comment": "/* overlay_bgp: BGP Router: vqfx4-bgp, UUID: ec8d8fdb-edde-4c6d-9c54-16f794606d4e */",
                    "ip_address": "20.1.1.251",
                    "name": "20.1.1.251",
                    "autonomous_system": 64512
                }
            ],
            "families": [
                "inet-vpn",
                "inet6-vpn",
                "route-target",
                "evpn"
            ],
            "name": "_contrail_asn-64512",
            "ip_address": "20.1.1.252",
            "type_": "internal",
            "hold_time": 90,
            "autonomous_system": 64512
        }
        ],
        "system": {
            "device_family": "junos-qfx",
            "vendor_name": "Juniper",
            "tunnel_destination_networks": [
                {
                    "prefix": "10.0.2.0",
                    "prefix_len": 24
                }
            ],
            "name": "vqfx1",
            "uuid": "2f5f138b-49bf-4dae-8e66-0a348944c3ed",
            "loopback_ip_list": [
                "20.1.1.252"
            ],
            "encapsulation_priorities": [
                "VXLAN",
                "MPLSoUDP",
                "MPLSoGRE"
            ],
            "management_ip": "172.16.1.1",
            "routing_bridging_roles": [
                "CRB-Gateway", "DCI-Gateway"
            ],
            "credentials": {
                "authentication_method": "PasswordBasedAuthentication",
                "password": "c0ntrail123",
                "user_name": "root"
            },
            "tunnel_ip": "20.1.1.252",
            "product_name": "vqfx-10000",
            "physical_role": "spine"
        }
    }


def _mock_job_ctx(is_delete, manage_underlay):
    return {
            "api_server_host": "192.168.3.23",
            "auth_token": "",
            "cluster_id": "",
            "config_args": {
                "cassandra_ca_certs": "/etc/contrail/ssl/certs/ca-cert.pem",
                "cassandra_password": None,
                "cassandra_server_list": ["192.168.3.23:9161"],
                "cassandra_use_ssl": False,
                "cassandra_user": None,
                "cluster_id": "",
                "collectors": ["192.168.3.23:8086"],
                "fabric_ansible_conf_file": [
                    "/etc/contrail/contrail-keystone-auth.conf",
                    "/etc/contrail/contrail-fabric-ansible.conf"],
                "host_ip": "192.168.3.23",
                "zk_server_ip": "192.168.3.23:2181"
            },
            "current_task_index": 1,
            "db_init_params": {
                "cassandra_ca_certs": "/etc/contrail/ssl/certs/ca-cert.pem",
                "cassandra_password": None,
                "cassandra_server_list": ["192.168.3.23:9161"],
                "cassandra_use_ssl": False,
                "cassandra_user": None
            },
            "fabric_fqname": "default-global-system-config:fab01",
            "job_execution_id": "1550273845641_70275584-b110-4f15-ac7a-70d1ecf53c49",
            "job_input": {
                "additional_feature_params": {
                    "basic": {
                        "snmp": {
                            "communities": [{
                                "name": "public",
                                "readonly": True
                            }]
                        }
                    },
                    "underlay_ip_clos": {
                        "bgp_hold_time": 90
                    }
                },
                "device_management_ip": "172.16.1.1",
                "fabric_uuid": "dbae95a1-7647-4da9-9080-f70fbcb5f653",
                "is_delete": is_delete,
                "manage_underlay": manage_underlay
            },
            "job_template_fqname": ["default-global-system-config",
                                    "fabric_config_template"],
            "playbook_job_percentage": "95.0",
            "task_weightage_array": [20, 80],
            "total_task_count": 2,
            "unique_pb_id": "57fa5f57-4cdd-4b40-805f-d78decc9b066",
            "vnc_api_init_params": {
                "admin_password": "contrail123",
                "admin_tenant_name": "admin",
                "admin_user": "admin",
                "api_server_port": "8082",
                "api_server_use_ssl": False
            }
    }


def _parse_args():
    arg_parser = argparse.ArgumentParser(description='fabric filters tests')
    arg_parser.add_argument('-a', '--add_modify',
                            action='store_true', help='Add/modify fab config')
    arg_parser.add_argument('-d', '--delete',
                            action='store_true', help='Delete fabric config')
    arg_parser.add_argument('-m', '--manage_underlay',
                            action='store_true', help='Manage underlay')
    return arg_parser.parse_args()
# end _parse_args


def __main__():
    _parse_args()

    fab_filter = FilterModule()
    parser = _parse_args()
    results = {}
    if parser.add_modify:
        results = fab_filter.render_fabric_config(_mock_job_ctx(False, False))
    elif parser.delete:
        results = fab_filter.render_fabric_config(_mock_job_ctx(True, False))
    elif parser.manage_underlay:
        results = fab_filter.render_fabric_config(_mock_job_ctx(False, True))

    print results
# end __main__


if __name__ == '__main__':
    __main__()

