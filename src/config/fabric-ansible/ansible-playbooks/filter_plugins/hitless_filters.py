#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains code to support the hitless image upgrade feature
"""

import json
import traceback
import logging
import argparse
import yaml

from vnc_api.vnc_api import VncApi
#import sys
#sys.path.append("..")
#from test_hitless_filters import VncApi, mock_job_ctx, \
#    mock_image_upgrade_list,mock_upgrade_plan

ordered_role_groups = [
    ["CRB-Access@leaf", "ERB-UCAST-Gateway@leaf", "CRB-Gateway@leaf",
     "DC-Gateway@leaf"],
    ["null@spine", "CRB-Access@spine", "CRB-MCAST-Gateway@spine",
     "CRB-Gateway@spine", "Route-Reflector@spine", "DC-Gateway@spine"],
]


class FilterModule(object):

    @staticmethod
    def _init_logging():
        logger = logging.getLogger('HitlessUpgradeFilter')
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
        if not job_ctx.get('fabric_fqname'):
            raise ValueError('Invalid job_ctx: missing fabric_fqname')
        job_input = job_ctx.get('job_input')
        if not job_input:
            raise ValueError('Invalid job_ctx: missing job_input')
        if not job_input.get('fabric_uuid'):
            raise ValueError('Invalid job_ctx: missing fabric_uuid')
    # end _validate_job_ctx

    def __init__(self):
        self._logger = FilterModule._init_logging()
    # end __init__

    def filters(self):
        return {
            'hitless_upgrade_plan': self.get_hitless_upgrade_plan,
            'hitless_next_batch': self.get_next_batch,
        }
    # end filters

    # Wrapper to call main routine
    def get_hitless_upgrade_plan(self, job_ctx, image_upgrade_list,
                                 batch_limit=4):
        try:
            FilterModule._validate_job_ctx(job_ctx)
            self.vncapi = FilterModule._init_vnc_api(job_ctx)
            self.job_ctx = job_ctx
            self.batch_limit = batch_limit
            self.image_upgrade_list = image_upgrade_list
            self.fabric_uuid = job_ctx['job_input']['fabric_uuid']
            upgrade_plan = self._get_hitless_upgrade_plan()
            return upgrade_plan
        except Exception as ex:
            errmsg = "Unexpected error: %s\n%s" % (
                str(ex), traceback.format_exc()
            )
            self._logger.error(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
            }
    # end get_hitless_upgrade_plan

    # Main routine to create an upgrade plan
    def _get_hitless_upgrade_plan(self):

        self.device_table, self.skipped_device_table = \
            self._create_device_table()
        self.role_device_groups = self._create_role_device_groups()
        self.lag_table = self._create_lag_table()
        self._create_buddy_lists()
        self.batches = self._create_batches()
#        self.report = self._create_report()

        upgrade_plan = {
            'image_upgrade_list': self.image_upgrade_list,
            'device_table': self.device_table,
            'skipped_device_table': self.skipped_device_table,
            'role_device_groups': self.role_device_groups,
            'lag_table': self.lag_table,
            'batches': self.batches,
            'status': "success"
        }
        return upgrade_plan
    # end _get_hitless_upgrade_plan

    # Create a table of device information
    def _create_device_table(self):
        device_table = {}
        skipped_device_table = {}
        for image_entry in self.image_upgrade_list:
            image_uuid = image_entry.get('image_uuid')
            image_obj = self.vncapi.device_image_read(id=image_uuid)
            device_list = image_entry.get('device_list')
            for device_uuid in device_list:
                device_obj = self.vncapi.physical_router_read(id=device_uuid)
                device_info = {
                    "basic": {
                        "device_fqname": device_obj.fq_name,
                        "device_vendor": device_obj.physical_router_vendor_name,
                        "device_family": device_obj.physical_router_device_family,
                        "device_product": device_obj.physical_router_product_name,
                        "device_serial_number": device_obj.physical_router_serial_number,
                        "device_management_ip": device_obj.physical_router_management_ip,
                        "device_username": device_obj.physical_router_user_credentials.username,
                        "device_password": device_obj.physical_router_user_credentials.password,
                        "device_image_uuid": image_uuid
                    },
                    'image_family': image_obj.device_image_device_family,
                    'image_version': image_obj.device_image_os_version,
                    'name': device_obj.display_name,
                    'uuid': device_uuid,
                    'physical_role': device_obj.physical_router_role,
                    'rb_roles': device_obj.routing_bridging_roles.get_rb_roles(),
                    'role': self._determine_role(
                        device_obj.physical_router_role,
                        device_obj.routing_bridging_roles.get_rb_roles()),
                    'err_msgs': [],
                    'lag_info': {"lag_list": [], "buddies": []},
                }
                if self._skip_device_upgrade(device_uuid):
                    skipped_device_table[device_uuid] = device_info
                else:
                    device_table[device_uuid] = device_info
        return device_table, skipped_device_table
    # end _create_device_table

    # Create a simple table of roles with their corresponding devices
    def _create_role_device_groups(self):
        # Group devices based on role. Use dict keyed by role name
        role_device_groups = {}
        for device_uuid, device_info in self.device_table.iteritems():
            role = device_info['role']
            if role not in role_device_groups:
                role_device_groups[role] = []
            role_device_groups[role].append(device_uuid)
        return role_device_groups
    # end _create_role_device_groups

    # Create a table keyed by link aggregation group uuid containing member
    # devices and their physical interfaces
    def _create_lag_table(self):
        lag_table = {}
        lag_refs = self.vncapi.link_aggregation_groups_list \
            (parent_id=self.fabric_uuid).\
            get('link-aggregation-groups', [])
        for lag_ref in lag_refs:
            lag_uuid = lag_ref.get('uuid')
            lag_table[lag_uuid] = {"device_table": {}}
            lag_dev_table = lag_table[lag_uuid]['device_table']
            lag_obj = self.vncapi.link_aggregation_group_read(id=lag_uuid)
            lag_table[lag_uuid]['name'] = lag_obj.display_name
            pi_refs = lag_obj.get_physical_interface_refs() or []
            for pi_ref in pi_refs:
                pi_uuid = pi_ref.get('uuid')
                pi_obj = self.vncapi.physical_interface_read(id=pi_uuid)
                device_uuid = pi_obj.parent_uuid
                if device_uuid not in lag_dev_table:
                    lag_dev_table[device_uuid] = []
                    # If this is one of the devices to upgrade, append this
                    # LAG to the lag_list for use later
                    if device_uuid in self.device_table:
                        device_info = self.device_table[device_uuid]
                        device_info['lag_info']['lag_list'].append(lag_uuid)
                pi_entry = {"fq_name": pi_obj.fq_name, "uuid": pi_obj.uuid}
                lag_dev_table[device_uuid].append(pi_entry)
        return lag_table
    # end _create_lag_table

    # For each device, create a list of devices which cannot be upgraded at
    # the same time because they are multi-homed to the same BMS
    def _create_buddy_lists(self):
        for device_uuid, device_info in self.device_table.iteritems():
            lag_info = self.device_table[device_uuid]['lag_info']
            for lag_uuid in lag_info['lag_list']:
                lag_entry = self.lag_table[lag_uuid]
                lag_dev_table = lag_entry['device_table']
                for lag_dev_uuid, pi_list in lag_dev_table.iteritems():
                    if lag_dev_uuid not in lag_info['buddies'] and \
                                    lag_dev_uuid != device_uuid:
                        lag_info['buddies'].append(lag_dev_uuid)
    # end _create_buddy_lists

    # Use the ordered list of role groups and the role device groups to
    # create sets of batches. Also prevent two or more devices from being
    # included in the same batch if they share a multi-homed BMS
    def _create_batches(self):
        batches = []
        idx = 0
        for role_group in ordered_role_groups:
            # Batching is per-role-group
            batch_load_list = []
            for role in role_group:
                # Only allow 1 spine at a time for now
                batch_max = 1 if "@spine" in role else self.batch_limit
                device_list = self.role_device_groups.get(role, [])
                for device_uuid in device_list:
                    loaded = False
                    batch_full = False
                    device_name = self.device_table[device_uuid].get('name')
                    # Try to add device into an existing batch
                    for batch in batch_load_list:
                        buddies = self._get_lag_buddies(device_uuid)
                        safe = True
                        # If this device shares a multi-homed LAG interface
                        # with another device in this batch, try the next batch
                        for buddy in buddies:
                            if buddy in batch['device_list']:
                                safe = False
                                break
                        # If safe to do so, add this device to the batch
                        if safe:
                            batch['device_list'].append(device_uuid)
                            batch['device_names'].append(device_name)
                            loaded = True
                            # if the batch is full, move it to the master list
                            if len(batch['device_list']) >= batch_max:
                                batch_full = True
                            break
                    # if not loaded into a batch, create a new batch
                    if not loaded:
                        idx += 1
                        batch = {
                            'name': "batch_" + str(idx),
                            'device_list': [device_uuid],
                            'device_names': [device_name]
                        }
                        batch_load_list.append(batch)
                        # if the batch is full, move it to the master list
                        if len(batch['device_list']) >= batch_max:
                            batch_full = True
                    # if batch full, move from load list to master list
                    if batch_full:
                        batch_load_list.remove(batch)
                        batches.append(batch)
            # move remaining batches from the load list to the master list
            for batch in batch_load_list:
                batches.append(batch)
        # Add batch index to device info
        for batch in batches:
            for device_uuid in batch['device_list']:
                self.device_table[device_uuid]['batch_index'] = batches.index(batch)

        return batches
    # end _create_batches

    # Create user-readable YAML report
    def _create_report(self):
        report = {'summary': {}, 'details': {}}

        # Print summary of batches
        batches = self.batches
        for batch in batches:
            del(batch['device_list'])
        report['summary']['image_upgrade_batches'] = batches

        # print summary of skipped devices
        skipped = []
        for device_uuid, device_info in self.skipped_device_table.iteritems():
            skipped.append(self.device_table[device_uuid]['name'])
        report['summary']['skipped_devices'] = skipped

        devices = {}
        for device_uuid, device_info in self.device_table.iteritems():
            name = device_info['name']
            basic = device_info['basic']
            lag_info = device_info['lag_info']
            devices[name] = {
                'uuid': device_info.get('uuid'),
                'vendor': basic.get('device_vendor'),
                'family': basic.get('device_family'),
                'product': basic.get('device_product'),
                'serial_number': basic.get('device_serial_number'),
                'management_ip': basic.get('device_management_ip'),
                'username': basic.get('device_username'),
                'password': basic.get('device_password'),
                'image_version': device_info.get('image_version'),
                'image_family': device_info.get('image_family'),
                'physical_role': device_info.get('physical_role'),
                'routing_bridging_roles': device_info.get('rb_roles'),
                'role': device_info.get('role'),
                'lag_list': lag_info.get('lag_list'),
                'lag_peers': lag_info.get('buddies')
            }
        # print device details
        report['details']['device_info'] = devices
#        yaml_report = yaml.dump(report)
#        print yaml_report
    # end _create_report

    # Get the next batch off the batch list
    def get_next_batch(self, job_ctx, upgrade_plan, device_uuid):
        batch = {}
        if device_uuid:
            device_info = upgrade_plan['device_table'].get(device_uuid)
            if device_info:
                idx = device_info['batch_index'] + 1
            else:
                return {}
        else:
            idx = 0
        if idx < len(upgrade_plan['batches']):
            for device_uuid in upgrade_plan['batches'][idx]['device_list']:
                batch[device_uuid] = upgrade_plan['device_table'][device_uuid]['basic']
        return batch
    # end get_next_batch

    # Get a list of all devices that share LAG groups with this device
    def _get_lag_buddies(self, device_uuid):
        device_info = self.device_table[device_uuid]
        lag_info = device_info['lag_info']
        return lag_info.get('buddies', [])
    # end _get_lag_buddies

    # Get a single role for this device to be used in determining upgrade
    # ordering
    def _determine_role(self, physical_role, rb_roles):
        # For now, we simply take the first rb_role listed
        return rb_roles[0] + '@' + physical_role
    # end _determine_role

    # If old and new image versions match, don't upgrade
    def _skip_device_upgrade(self, device_uuid):
        # TODO hardcode for now. Read OS version from UVE or prouter object
        return False
    #end _skip_device_upgrade


def _parse_args():
    arg_parser = argparse.ArgumentParser(description='fabric filters tests')
    arg_parser.add_argument('-p', '--create_plan',
                            action='store_true', help='Create Upgrade Plan')
    arg_parser.add_argument('-b', '--next_batch',
                            action='store_true', help='Get Next Batch')
    return arg_parser.parse_args()

# testing
def __main__():
    parser = _parse_args()
    hitless_filter = FilterModule()

    if parser.create_plan:
        upgrade_plan = hitless_filter.get_hitless_upgrade_plan(mock_job_ctx,
                        mock_image_upgrade_list, batch_limit=4)
        print json.dumps(upgrade_plan)
    elif parser.next_batch:
        batch = hitless_filter.get_next_batch(mock_job_ctx, mock_upgrade_plan,
                                              '')
        print json.dumps(batch)

# end __main__


if __name__ == '__main__':
    __main__()
