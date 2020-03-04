#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""This file contains code to support the hitless image upgrade feature."""

import argparse
from builtins import object
from builtins import str
import copy
from datetime import timedelta
import re
import sys
import traceback

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
# unit test
sys.path.append("../fabric-ansible/ansible-playbooks/module_utils")
from filter_utils import _task_error_log, FilterLog

from job_manager.job_utils import JobAnnotations, JobVncApi


ordered_role_groups = [
    ["leaf"],
    ["spine"],
    ["default"]
]

IMAGE_UPGRADE_DURATION = 30  # minutes


class FilterModule(object):

    critical_routing_bridging_roles = {
        "CRB-MCAST-Gateway",
        "DC-Gateway",
        "DCI-Gateway",
    }

    @staticmethod
    def _validate_job_ctx(job_ctx):
        if not job_ctx.get('fabric_fqname'):
            raise ValueError('Invalid job_ctx: missing fabric_fqname')
        job_input = job_ctx.get('job_input')
        if not job_input:
            raise ValueError('Invalid job_ctx: missing job_input')
        if not job_input.get('fabric_uuid'):
            raise ValueError('Invalid job_ctx: missing fabric_uuid')
        return job_input
    # end _validate_job_ctx

    def filters(self):
        return {
            'hitless_upgrade_plan': self.get_hitless_upgrade_plan,
            'hitless_next_batch': self.get_next_batch,
            'hitless_all_devices': self.get_all_devices,
            'hitless_device_info': self.get_device_info,
            'hitless_validate': self.validate_critical_roles
        }
    # end filters

    # Wrapper to call main routine
    def get_hitless_upgrade_plan(self, job_ctx, image_upgrade_list):
        try:
            FilterLog.instance("HitlessUpgradeFilter")
            self.job_input = FilterModule._validate_job_ctx(job_ctx)
            self.fabric_uuid = self.job_input['fabric_uuid']
            self.vncapi = JobVncApi.vnc_init(job_ctx)
            self.job_ctx = job_ctx
            self.ja = JobAnnotations(self.vncapi)
            self.advanced_parameters = self._get_advanced_params()
            self._cache_job_input()
            self.batch_limit = self.advanced_parameters.get(
                'bulk_device_upgrade_count')
            self.image_upgrade_list = image_upgrade_list
            upgrade_plan = self._get_hitless_upgrade_plan()
            return upgrade_plan
        except Exception as ex:
            errmsg = "Unexpected error: %s\n%s" % (
                str(ex), traceback.format_exc()
            )
            _task_error_log(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
            }
    # end get_hitless_upgrade_plan

    # Get any advanced parameters from job input to override defaults
    def _get_advanced_params(self):
        job_template_fqname = self.job_ctx.get('job_template_fqname')
        def_json = self.ja.generate_default_json(job_template_fqname)
        adv_params = def_json.get("advanced_parameters")
        job_input_adv_params = self.job_input.get('advanced_parameters', {})
        adv_params = self.ja.dict_update(adv_params, job_input_adv_params)
        return adv_params
    # end _get_advanced_params

    # Store the job input on the fabric object for UI to retrieve later
    def _cache_job_input(self):
        job_input = copy.deepcopy(self.job_input)
        job_input.update({"advanced_parameters": self.advanced_parameters})
        self.ja.cache_job_input(self.fabric_uuid,
                                self.job_ctx.get('job_template_fqname')[-1],
                                job_input)
    # end _cache_job_input

    # Read from Node Profile to determine whether the upgrade is hitless
    def _is_hitless_upgrade(self, device_obj):
        node_profile_refs = device_obj.get_node_profile_refs()
        if node_profile_refs:
            np_uuid = node_profile_refs[0].get('uuid')
            node_profile_obj = self.vncapi.node_profile_read(id=np_uuid)
            is_hitless = node_profile_obj.get_node_profile_hitless_upgrade()
            return is_hitless
        return True
    # end _is_hitless_upgrade

    # Main routine to generate an upgrade plan
    def _get_hitless_upgrade_plan(self):

        self.device_table, self.skipped_device_table = \
            self._generate_device_table()
        self.role_device_groups = self._generate_role_device_groups()
        self.vpg_table = self._generate_vpg_table()
        self._generate_buddy_lists()
        self.batches = self._generate_batches()
        self.report = self._generate_report()
        self.results = self._generate_results()

        upgrade_plan = {
            'image_upgrade_list': self.image_upgrade_list,
            'advanced_parameters': self.advanced_parameters,
            'device_table': self.device_table,
            'device_count': len(self.device_table),
            'skipped_device_table': self.skipped_device_table,
            'role_device_groups': self.role_device_groups,
            'vpg_table': self.vpg_table,
            'batches': self.batches,
            'report': self.report,
            'results': self.results,
            'status': "success"
        }
        return upgrade_plan
    # end _get_hitless_upgrade_plan

    # generate a table of device information
    def _generate_device_table(self):
        device_table = {}
        skipped_device_table = {}
        for image_entry in self.image_upgrade_list:
            image_uuid = image_entry.get('image_uuid')
            image_obj = self.vncapi.device_image_read(id=image_uuid)
            device_list = image_entry.get('device_list')
            for device_uuid in device_list:
                device_obj = self.vncapi.physical_router_read(id=device_uuid)
                routing_bridging_roles = device_obj.routing_bridging_roles
                if not routing_bridging_roles:
                    raise ValueError("Cannot find routing-bridging roles")
                rb_roles = routing_bridging_roles.get_rb_roles()
                is_hitless_upgrade = self._is_hitless_upgrade(device_obj)
                device_info = {
                    "basic": {
                        "device_fqname": device_obj.fq_name,
                        "device_vendor":
                            device_obj.physical_router_vendor_name,
                        "device_family":
                            device_obj.physical_router_device_family,
                        "device_product":
                            device_obj.physical_router_product_name,
                        "device_serial_number":
                            device_obj.physical_router_serial_number,
                        "device_management_ip":
                            device_obj.physical_router_management_ip,
                        "device_username":
                            device_obj.physical_router_user_credentials.
                            username,
                        "device_password": self._get_password(device_obj),
                        "device_image_uuid": image_uuid,
                        "device_hitless_upgrade": is_hitless_upgrade
                    },
                    'image_family': image_obj.device_image_device_family,
                    'image_version': image_obj.device_image_os_version,
                    'current_image_version':
                        device_obj.physical_router_os_version,
                    'name': device_obj.fq_name[-1],
                    'uuid': device_uuid,
                    'physical_role': device_obj.physical_router_role,
                    'rb_roles': rb_roles,
                    'role': self._determine_role(
                        device_obj.physical_router_role, rb_roles),
                    'err_msgs': [],
                    'vpg_info': {"vpg_list": [], "buddies": []},
                    'target_multihomed_interface': []
                }
                skip, reason = self._check_skip_device_upgrade(device_info)
                if skip:
                    if reason:
                        device_info['skip_reason'] = reason
                    skipped_device_table[device_uuid] = device_info
                else:
                    device_table[device_uuid] = device_info
        return device_table, skipped_device_table
    # end _generate_device_table

    # generate a simple table of roles with their corresponding devices
    def _generate_role_device_groups(self):
        # Group devices based on role. Use dict keyed by role name
        role_device_groups = {}
        for device_uuid, device_info in list(self.device_table.items()):
            role = device_info['role']
            if role not in role_device_groups:
                role_device_groups[role] = []
            role_device_groups[role].append(device_uuid)
        # Sort lists
        for role, group in list(role_device_groups.items()):
            group.sort()
        return role_device_groups
    # end _generate_role_device_groups

    # generate a table keyed by virtual port group uuid containing member
    # devices and their physical interfaces
    def _generate_vpg_table(self):
        vpg_table = {}
        vpg_refs = self.vncapi.virtual_port_groups_list(
            parent_id=self.fabric_uuid). get(
            'virtual-port-groups', [])
        for vpg_ref in vpg_refs:
            vpg_uuid = vpg_ref.get('uuid')
            vpg_table[vpg_uuid] = {"device_table": {}}
            vpg_dev_table = vpg_table[vpg_uuid]['device_table']
            vpg_obj = self.vncapi.virtual_port_group_read(id=vpg_uuid)
            vpg_table[vpg_uuid]['name'] = vpg_obj.fq_name[2]
            pi_refs = vpg_obj.get_physical_interface_refs() or []
            for pi_ref in pi_refs:
                pi_uuid = pi_ref.get('uuid')
                pi_obj = self.vncapi.physical_interface_read(id=pi_uuid)
                device_uuid = pi_obj.parent_uuid
                if device_uuid not in vpg_dev_table:
                    vpg_dev_table[device_uuid] = []
                    # If this is one of the devices to upgrade, append this
                    # vpg to the vpg_list for use later
                    if device_uuid in self.device_table:
                        device_info = self.device_table[device_uuid]
                        device_info['vpg_info']['vpg_list'].append(vpg_uuid)
                pi_entry = {"fq_name": pi_obj.fq_name, "uuid": pi_obj.uuid}
                vpg_dev_table[device_uuid].append(pi_entry)
                # Add interface name to multihomed list
                if device_uuid in self.device_table:
                    device_info = self.device_table[device_uuid]
                    if_name = pi_obj.fq_name[2]
                    if if_name not in \
                            device_info['target_multihomed_interface']:
                        device_info['target_multihomed_interface'].\
                            append(if_name)
        return vpg_table
    # end _generate_vpg_table

    # For each device, generate a list of devices which cannot be upgraded at
    # the same time because they are multi-homed to the same BMS
    def _generate_buddy_lists(self):
        for device_uuid, device_info in list(self.device_table.items()):
            vpg_info = self.device_table[device_uuid]['vpg_info']
            for vpg_uuid in vpg_info['vpg_list']:
                vpg_entry = self.vpg_table[vpg_uuid]
                vpg_dev_table = vpg_entry['device_table']
                for vpg_dev_uuid, pi_list in list(vpg_dev_table.items()):
                    if vpg_dev_uuid not in vpg_info['buddies'] and \
                            vpg_dev_uuid != device_uuid:
                        buddy_entry = self._get_buddy_entry(vpg_dev_uuid,
                                                            pi_list)
                        vpg_info['buddies'].append(buddy_entry)
    # end _generate_buddy_lists

    # Create entry for peer, including ip_addr, username, password
    def _get_buddy_entry(self, device_uuid, pi_list):
        if device_uuid in self.device_table or \
                device_uuid in self.skipped_device_table:
            if device_uuid in self.device_table:
                device_info = self.device_table[device_uuid]
            else:
                device_info = self.skipped_device_table[device_uuid]
            fq_name = device_info['basic']['device_fqname']
            mgmt_ip = device_info['basic']['device_management_ip']
            username = device_info['basic']['device_username']
            password = device_info['basic']['device_password']
            vendor = device_info['basic']['device_vendor']
            multihomed_interface_list = \
                device_info['target_multihomed_interface']
        else:
            device_obj = self.vncapi.physical_router_read(id=device_uuid)
            fq_name = device_obj.fq_name
            mgmt_ip = device_obj.physical_router_management_ip
            username = device_obj.physical_router_user_credentials.username
            password = self._get_password(device_obj)
            vendor = device_obj.physical_router_vendor_name,
            multihomed_interface_list = \
                self._get_multihomed_interface_list(pi_list)

        return {
            "uuid": device_uuid,
            "fq_name": fq_name,
            "name": fq_name[-1],
            "mgmt_ip": mgmt_ip,
            "username": username,
            "password": password,
            "vendor": vendor,
            "multihomed_interface_list": multihomed_interface_list
        }
    # end _get_buddy_entry

    # Get list of multihomed interface names
    def _get_multihomed_interface_list(self, pi_list):
        if_list = []
        for pi_entry in pi_list:
            if_name = pi_entry['fq_name'][-1]
            if if_name not in if_list:
                if_list.append(if_name)
        return if_list
    # end _get_multihomed_interface_list

    def _device_value_based_on_number_of_critical_roles(self, device_uuid):
        rb_roles = self.device_table[device_uuid].get('rb_roles')
        how_many_critical_roles = 0
        for rb_role in rb_roles:
            if rb_role in FilterModule.critical_routing_bridging_roles:
                how_many_critical_roles += 1
        return -how_many_critical_roles

    # Creates a dict: name of critical routing bridging role -> number of
    # occurences in all devices.
    def _calculate_devices_with_critical_routing_bridging_roles(self):
        self.critical_routing_bridging_roles_count = {}
        for critical_routing_bridging_role in\
                FilterModule.critical_routing_bridging_roles:
            self.critical_routing_bridging_roles_count[
                    critical_routing_bridging_role] = 0
        for device_uuid, device_info in list(self.device_table.items()):
            for routing_bridging_role in device_info.get('rb_roles'):
                if routing_bridging_role in\
                        FilterModule.critical_routing_bridging_roles:
                    self.critical_routing_bridging_roles_count[
                            routing_bridging_role] += 1

    # Assumes that critical_routing_bridging_roles_count has been initialized.
    def _calc_max_number_of_repr_of_critical_rb_roles_per_batch(self):
        self.max_number_of_repr_of_critical_rb_roles_per_batch = {}
        for role_name, number_of_occurences \
                in list(self.critical_routing_bridging_roles_count.items()):
            self.max_number_of_repr_of_critical_rb_roles_per_batch[role_name] \
                    = number_of_occurences / 2 + number_of_occurences % 2

    def _calculate_max_number_of_spines_updated_in_batch(self):
        number_of_spines = 0
        for device_uuid, device_info in list(self.device_table.items()):
            if device_info.get('physical_role') == 'spine':
                number_of_spines += 1
        self.max_number_of_spines_updated_in_batch = \
            number_of_spines / 2 + number_of_spines % 2

    def _calc_number_of_repr_of_critical_rb_roles_in_batch(self, batch):
        critical_routing_bridging_roles_count = {}
        for critical_routing_bridging_role in\
                FilterModule.critical_routing_bridging_roles:
            critical_routing_bridging_roles_count[
                    critical_routing_bridging_role] = 0
        for device_uuid in batch['device_list']:
            rb_roles = self.device_table[device_uuid].get('rb_roles')
            for rb_role in rb_roles:
                if rb_role in FilterModule.critical_routing_bridging_roles:
                    critical_routing_bridging_roles_count[rb_role] += 1
        return critical_routing_bridging_roles_count

    # If correct batch extended with device_uuid is still correct in regards
    # to vpg buddies, return True. Otherwise return False.
    def _check_vpg_buddies_in_batch(self, device_uuid, batch):
        # If this device shares a multi-homed vpg interface
        # with another device in this batch, return False.
        buddies = self._get_vpg_buddies(device_uuid)
        for buddy in buddies:
            if buddy['uuid'] in batch['device_list']:
                return False
        return True

    # If correct batch extended with device_uuid is still correct in regards
    # to number of spines in batch, return True. Otherwise return False.
    def _check_number_of_spines_in_batch(self, device_uuid, batch):
        device_info = self.device_table[device_uuid]
        physical_role = device_info.get('physical_role')
        if "spine" in physical_role:
            spines_in_batch = 0
            for device in batch['device_list']:
                device_role = self.device_table[device].get('physical_role')
                if "spine" in device_role:
                    spines_in_batch += 1
                    if (spines_in_batch + 1 >
                            self.max_number_of_spines_updated_in_batch):
                        return False
        return True

    # If correct batch extended with device_uuid is still correct in regards
    # to number of critical roles, return True. Otherwise return False.
    def _check_number_of_critical_rb_roles_in_batch(self, device_uuid, batch):
        device_info = self.device_table[device_uuid]
        rb_roles = device_info.get('rb_roles')
        critical_rb_roles_in_device = list(
                FilterModule.critical_routing_bridging_roles & set(rb_roles))
        if critical_rb_roles_in_device:
            critical_rb_roles_in_batch_count = self.\
                    _calc_number_of_repr_of_critical_rb_roles_in_batch(batch)
            for rb_role in critical_rb_roles_in_device:
                if critical_rb_roles_in_batch_count[rb_role] + 1 > self.\
                        max_number_of_repr_of_critical_rb_roles_per_batch[
                        rb_role]:
                    return False
        return True

    # It assumes that batch is correct and is not empty.
    def _check_if_device_can_be_added_to_the_batch(self, device_uuid, batch):
        return \
            self._check_vpg_buddies_in_batch(device_uuid, batch) and \
            self._check_number_of_spines_in_batch(device_uuid, batch) and \
            self._check_number_of_critical_rb_roles_in_batch(
                    device_uuid, batch)

    def _add_batch_index_to_device_info(self, batches):
        for batch in batches:
            for device_uuid in batch['device_list']:
                self.device_table[device_uuid]['batch_index'] = batches.index(
                    batch)

    def _add_device_to_the_batch(self, device_uuid, batch_load_list, batches):
        batch = {}
        loaded = False
        batch_full = False
        device_name = self.device_table[device_uuid].get('name')
        # Try to add device into an existing batch
        for batch in batch_load_list:
            safe = self._check_if_device_can_be_added_to_the_batch(
                device_uuid, batch)
            if safe:
                batch['device_list'].append(device_uuid)
                batch['device_names'].append(device_name)
                loaded = True
                # if the batch is full, move it to the master list
                if len(batch['device_list']) >= self.batch_limit:
                    batch_full = True
                break
        # if not loaded into a batch, generate a new batch
        if not loaded:
            idx = len(batch_load_list) + len(batches) + 1
            batch = {
                'name': "Batch " + str(idx),
                'device_list': [device_uuid],
                'device_names': [device_name]
            }
            batch_load_list.append(batch)
            # if the batch is full, move it to the master list
            if len(batch['device_list']) >= self.batch_limit:
                batch_full = True
        # if batch full, move from load list to master list
        if batch_full:
            batch_load_list.remove(batch)
            batches.append(batch)

    def _assign_devices_to_batches(self):
        batches = []
        for role_group in ordered_role_groups:
            # Batching is per-role-group (constraint 1).
            # TODO: Each role group contains just one role. So why do we need
            # role groups?
            batch_load_list = []
            for role in role_group:
                device_list = self.role_device_groups.get(role, [])
                for device_uuid in device_list:
                    self._add_device_to_the_batch(
                            device_uuid, batch_load_list, batches)
            # move remaining batches from the load list to the master list
            for batch in batch_load_list:
                batches.append(batch)
        return batches

    # Generate batches of devices that can be updated at once.
    #
    # Constraints:
    # 1. Two devices with the different physical_router_role can not be in the
    #    same batch.
    # 2. More than half (half + 0.5 for odd number) of spines can not be in the
    #    same batch.
    # 3. For each routing_bridging_role in {"CRB-MCAST-Gateway",
    #    "DC-Gateway", "DCI-Gateway"} no more than half (half + 0.5 for odd
    #    number) of devices with that role can be in the same batch.
    # 4. Two devices that share VPG can not be in the same batch.
    def _generate_batches(self):
        self._calculate_devices_with_critical_routing_bridging_roles()
        self._calc_max_number_of_repr_of_critical_rb_roles_per_batch()
        self._calculate_max_number_of_spines_updated_in_batch()
        batches = self._assign_devices_to_batches()
        self._add_batch_index_to_device_info(batches)
        return batches

    def _spill_device_details(self, device_name, device_info):
        details = ""
        basic = device_info['basic']
        vpg_info = device_info['vpg_info']
        batch_index = device_info.get('batch_index')
        batch_name = self.batches[batch_index]['name'] \
            if batch_index is not None else "N/A"
        details += "\n  - {}\n".format(device_name)
        details += \
            "    uuid             : {}\n"\
            "    vendor           : {}\n"\
            "    family           : {}\n"\
            "    product          : {}\n"\
            "    serial number    : {}\n"\
            "    management ip    : {}\n"\
            "    username         : {}\n"\
            "    password         : {}\n"\
            "    new image version: {}\n"\
            "    current image version: {}\n"\
            "    image family     : {}\n"\
            "    physical role    : {}\n"\
            "    routing bridging roles: {}\n"\
            "    role             : {}\n"\
            "    vpg list         : {}\n"\
            "    vpg peers        : {}\n"\
            "    batch            : {}\n"\
            "    is hitless?      : {}\n"\
            .format(
                device_info.get('uuid'),
                basic.get('device_vendor'),
                basic.get('device_family'),
                basic.get('device_product'),
                basic.get('device_serial_number'),
                basic.get('device_management_ip'),
                basic.get('device_username'),
                "** hidden **",  # basic.get('device_password'),
                device_info.get('image_version'),
                device_info.get('current_image_version'),
                device_info.get('image_family'),
                device_info.get('physical_role'),
                device_info.get('rb_roles'),
                device_info.get('role'),
                vpg_info.get('vpg_list'),
                [buddy['uuid'] for buddy in vpg_info.get('buddies')],
                batch_name,
                basic.get('device_hitless_upgrade'),
            )
        return details

    def _generate_report(self):
        report = ""

        # generate devices dict with key of device name
        devices = {}
        for device_uuid, device_info in list(self.device_table.items()):
            device_name = self.device_table[device_uuid]['name']
            devices[device_name] = self.device_table[device_uuid]

        # generate skipped devices dict with key of device name
        sdevices = {}
        for device_uuid, device_info in \
                list(self.skipped_device_table.items()):
            device_name = self.skipped_device_table[device_uuid]['name']
            sdevices[device_name] = self.skipped_device_table[device_uuid]

        # First dump summary
        report += "\n********** Summary *************\n"

        # Dump summary of batches
        total_time = str(
            timedelta(minutes=IMAGE_UPGRADE_DURATION * len(self.batches)))
        if len(self.batches) > 0:
            report += "\nTotal estimated " \
                      "duration is {}.\n".format(total_time)
            report += "\nNote that this time " \
                      "estimate may vary depending on " \
                      "network speeds and system capabilities.\n"
            report += "The following batches " \
                      "of devices will be upgraded in the order listed:\n"
            for batch in self.batches:
                report += "\n{}:\n".format(batch.get('name'))
                for device_name in batch.get('device_names', []):
                    device_info = devices[device_name]
                    current_version = \
                        device_info['current_image_version'] or ""
                    new_version = device_info['image_version']
                    hitless_upgrade = \
                        device_info['basic']['device_hitless_upgrade']
                    is_hitless = "" if hitless_upgrade else "(not hitless)"
                    workflow_info = self._check_for_downgrade(device_info)
                    report += "  {}  {} --> {}  {}{}\n".format(
                        device_name, current_version, new_version,
                        is_hitless, workflow_info)
        else:
            report += "\n   NO DEVICES TO UPGRADE!"

        report += "\n"

        # Dump summary of skipped devices
        if len(sdevices) > 0:
            report += "\nThe following devices will not be upgraded " \
                      "for the reasons listed:\n"
            for device_name, device_info in sorted(sdevices.items()):
                report += "\n  {} ({})".format(device_name,
                                               device_info.get
                                               ('skip_reason',
                                                "unknown reason"))

            report += "\n NOTE: \n Incompatible device-image platform with " \
                      "the same versions could also lead to a device being " \
                      "skipped for image upgrade. " \
                      "Please recheck the platform compatibility " \
                      "for the above skipped devices."

        # Now dump the details
        report += "\n******** Details ************\n"

        # Dump device info
        if len(devices) > 0:
            report += "\nDetailed information for the " \
                      "devices to be upgraded is listed below:\n"
            # Spill out sorted list
            for device_name, device_info in sorted(devices.items()):
                details = self._spill_device_details(device_name, device_info)
                report += details

        # Dump skipped device info
        if len(sdevices) > 0:
            report += "\nDetailed information for " \
                      "the devices to be skipped is listed below:\n"
            # Spill out sorted list
            for device_name, device_info in sorted(sdevices.items()):
                details = self._spill_device_details(device_name, device_info)
                report += details
        return report

    def _generate_results(self):
        return self.report

    # Get the current and next batch off the batch list and return
    def get_next_batch(self, job_ctx, upgrade_plan, device_uuid):
        try:
            return self._get_next_batch(upgrade_plan, device_uuid)
        except Exception as ex:
            errmsg = "Unexpected error attempting to " \
                     "get next batch: %s\n%s" %\
                     (str(ex), traceback.format_exc())
            _task_error_log(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
            }
    # end get_next_batch

    # Get the current and next batch off the batch list and return
    def _get_next_batch(self, upgrade_plan, device_uuid):
        c_idx, n_idx = None, None
        current_batch, next_batch = {}, {}
        batch_info = {
            'current': {
                'batch_name': None, 'batch_index': None, 'batch_devices': {}
            },
            'next': {
                'batch_name': None, 'batch_index': None, 'batch_devices': {}
            },
            'status': "success"
        }

        if device_uuid:
            device_info = upgrade_plan['device_table'].get(device_uuid)
            if device_info:
                c_idx = device_info['batch_index']
                n_idx = c_idx + 1
            else:
                return batch_info
        else:
            n_idx = 0

        if c_idx is not None:
            batch = upgrade_plan['batches'][c_idx]
            for device_uuid in batch['device_list']:
                current_batch[device_uuid] = \
                    upgrade_plan['device_table'][device_uuid]['basic']
            batch_info['current'] = {
                'batch_name': batch['name'],
                'batch_index': c_idx,
                'batch_devices': current_batch}

        if n_idx < len(upgrade_plan['batches']):
            batch = upgrade_plan['batches'][n_idx]
            for device_uuid in batch['device_list']:
                next_batch[device_uuid] = \
                    upgrade_plan['device_table'][device_uuid]['basic']
            batch_info['next'] = {
                'batch_name': batch['name'],
                'batch_index': n_idx,
                'batch_devices': next_batch}

        return batch_info
    # end _get_next_batch

    # Get list of all devices for use in test_run
    def get_all_devices(self, job_ctx, upgrade_plan):
        try:
            return self._get_all_devices(upgrade_plan)
        except Exception as ex:
            errmsg = "Unexpected error attempting " \
                     "to get all devices: %s\n%s" % \
                     (str(ex), traceback.format_exc())
            _task_error_log(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
            }
    # end get_all_devices

    # Get list of all devices for use in test_run
    def _get_all_devices(self, upgrade_plan):
        all_devices = {}
        device_table = upgrade_plan['device_table']
        batch_info = {
            'current': {
                'batch_name': None, 'batch_index': None, 'batch_devices': {}
            },
            'next': {
                'batch_name': 'all', 'batch_index': 0, 'batch_devices': {}
            },
            'status': "success"
        }
        for device_uuid, device_info in list(device_table.items()):
            all_devices[device_uuid] = device_table[device_uuid]['basic']
        batch_info['next']['batch_devices'] = all_devices
        return batch_info
    # end get_all_devices

    # Get info for a single device
    def get_device_info(self, job_ctx, device_uuid):
        try:
            FilterLog.instance("HitlessUpgradeFilter")
            self.job_input = FilterModule._validate_job_ctx(job_ctx)
            self.fabric_uuid = self.job_input['fabric_uuid']
            self.vncapi = JobVncApi.vnc_init(job_ctx)
            self.job_ctx = job_ctx
            self.ja = JobAnnotations(self.vncapi)
            self.advanced_parameters = self._get_advanced_params()
            self._cache_job_input()
            self.device_uuid = device_uuid
            device_info = self._get_device_info()
            return device_info
        except Exception as ex:
            errmsg = "Unexpected error getting device info: %s\n%s" % (
                str(ex), traceback.format_exc()
            )
            _task_error_log(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
            }
    # end get_device_info

    # Get device info used for maintenance mode activate
    def _get_device_info(self):
        self.device_table = self._generate_device_entry()
        self.skipped_device_table = {}
        self.vpg_table = self._generate_vpg_table()
        self._generate_buddy_lists()

        device_info = {
            'advanced_parameters': self.advanced_parameters,
            'device_table': self.device_table,
            'vpg_table': self.vpg_table,
            'status': "success"
        }
        return device_info
    # end _get_device_info

    # Validate whether fabric will be hitless when the given list of
    # devices go into maintenance mode
    def validate_critical_roles(self, job_ctx, device_uuid_list):
        try:
            FilterLog.instance("HitlessUpgradeFilter")
            self.job_input = FilterModule._validate_job_ctx(job_ctx)
            self.fabric_uuid = self.job_input['fabric_uuid']
            self.vncapi = JobVncApi.vnc_init(job_ctx)
            self.job_ctx = job_ctx
            self.ja = JobAnnotations(self.vncapi)
            self.advanced_parameters = self._get_advanced_params()
            self._cache_job_input()
            self.device_uuid_list = device_uuid_list
            results = self._validate_critical_roles()
            return results
        except Exception as ex:
            errmsg = "Unexpected error validating: %s\n%s" % (
                str(ex), traceback.format_exc()
            )
            _task_error_log(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
            }
    # end hitless_validate

    # Get device info used for maintenance mode activate
    def _validate_critical_roles(self):
        error_msg = ''
        critical_dev_list = []
        mm_dev_list = []
        dev_list = self.vncapi.physical_routers_list(
            fields=['fabric_refs', 'physical_role_refs',
                    'routing_bridging_roles', 'physical_router_managed_state'
                    ]).get('physical-routers', [])
        # Search through all devices in fabric and create a critical device
        # list of devices which are active and performing critical roles
        for dev in dev_list:
            if dev['uuid'] in self.device_uuid_list:
                mm_dev_list.append(dev)
                continue
            fabric_refs = dev.get('fabric_refs')
            if not fabric_refs:
                continue
            fabric_uuid = fabric_refs[0]['uuid']
            if fabric_uuid != self.fabric_uuid:
                continue
            managed_state = dev.get('physical_router_managed_state')
            if managed_state and managed_state != 'active':
                continue
            physical_role_refs = dev.get('physical_role_refs')
            if not physical_role_refs:
                continue
            physical_role = physical_role_refs[0]['to'][-1]
            if physical_role == 'spine':
                critical_dev_list.append(dev)
                continue
            routing_bridging_roles = dev.get('routing_bridging_roles')
            if routing_bridging_roles:
                rb_roles = routing_bridging_roles['rb_roles']
            else:
                rb_roles = []
            for rb_role in rb_roles:
                if rb_role in FilterModule.critical_routing_bridging_roles:
                    critical_dev_list.append(dev)
                    break

        # Make sure critical roles are present in critical devices
        missing_roles = set()
        for mm_dev in mm_dev_list:
            # check critical physical roles
            physical_role_refs = mm_dev.get('physical_role_refs')
            if not physical_role_refs:
                continue
            physical_role = physical_role_refs[0]['to'][-1]
            if physical_role == 'spine':
                found = self._find_critical_phy_role(
                    physical_role, critical_dev_list)
                if not found:
                    missing_roles.add(physical_role)

            # check critical routing-bridging roles
            routing_bridging_roles = mm_dev.get('routing_bridging_roles')
            if routing_bridging_roles:
                rb_roles = routing_bridging_roles['rb_roles']
            else:
                rb_roles = []
            for rb_role in rb_roles:
                if rb_role in FilterModule.critical_routing_bridging_roles:
                    found = self._find_critical_rb_role(
                        rb_role, critical_dev_list)
                    if not found:
                        missing_roles.add(rb_role)
        if missing_roles:
            error_msg = 'Fabric will not be hitless because these '\
                        'roles will no longer be deployed: '\
                        '{}'.format(list(missing_roles))

        if error_msg:
            results = {
                'error_msg': error_msg,
                'status': "failure"
            }
        else:
            results = {
                'error_msg': "Fabric is hitless",
                'status': "success"
            }
        return results
    # end _hitless_validate

    # Find a particular critical physical role in a list of devices
    def _find_critical_phy_role(self, crit_phy_role, dev_list):
        for dev in dev_list:
            physical_role_refs = dev.get('physical_role_refs')
            if not physical_role_refs:
                continue
            physical_role = physical_role_refs[0]['to'][-1]
            if physical_role == crit_phy_role:
                return True
        return False
    # end _find_critical_rb_role

    # Find a particular critical routing-bridging role in a list of devices
    def _find_critical_rb_role(self, crit_rb_role, dev_list):
        for dev in dev_list:
            routing_bridging_roles = dev.get('routing_bridging_roles')
            if routing_bridging_roles:
                rb_roles = routing_bridging_roles['rb_roles']
            else:
                rb_roles = []
            for rb_role in rb_roles:
                if crit_rb_role == rb_role:
                    return True
        return False
    # end _find_critical_rb_role

    # generate a single entry of device information
    def _generate_device_entry(self):
        device_table = {}
        device_obj = self.vncapi.physical_router_read(id=self.device_uuid)
        routing_bridging_roles = device_obj.routing_bridging_roles
        if not routing_bridging_roles:
            raise ValueError("Cannot find routing-bridging roles")
        rb_roles = routing_bridging_roles.get_rb_roles()
        is_hitless_upgrade = self._is_hitless_upgrade(device_obj)
        device_info = {
            "basic": {
                "device_fqname": device_obj.fq_name,
                "device_vendor":
                    device_obj.physical_router_vendor_name,
                "device_family":
                    device_obj.physical_router_device_family,
                "device_product":
                    device_obj.physical_router_product_name,
                "device_serial_number":
                    device_obj.physical_router_serial_number,
                "device_management_ip":
                    device_obj.physical_router_management_ip,
                "device_username":
                    device_obj.physical_router_user_credentials.username,
                "device_password":
                    self._get_password(device_obj),
                "device_hitless_upgrade": is_hitless_upgrade
            },
            'name': device_obj.fq_name[-1],
            'uuid': self.device_uuid,
            'physical_role': device_obj.physical_router_role,
            'rb_roles': rb_roles,
            'role': self._determine_role(
                device_obj.physical_router_role, rb_roles),
            'err_msgs': [],
            'vpg_info': {"vpg_list": [], "buddies": []},
            'target_multihomed_interface': []
        }
        device_table[self.device_uuid] = device_info
        return device_table
    # end _generate_device_entry

    # Get a list of all devices that share vpg groups with this device
    def _get_vpg_buddies(self, device_uuid):
        device_info = self.device_table[device_uuid]
        vpg_info = device_info['vpg_info']
        return vpg_info.get('buddies', [])
    # end _get_vpg_buddies

    # Get a single role for this device to be used in determining upgrade
    # ordering
    def _determine_role(self, physical_role, rb_roles):
        # Use physical role for now. If not in ordered table, use default
        for role_group in ordered_role_groups:
            for role in role_group:
                if physical_role == role:
                    return physical_role
        return "default"
    # end _determine_role

    # If old and new image versions match, don't upgrade
    def _check_skip_device_upgrade(self, device_info):
        if device_info['image_version'] == \
                device_info['current_image_version']:
            return True, "Upgrade image version matches current image version"
        return False, ""
    # end _check_skip_device_upgrade

    def _check_for_downgrade(self, device_info):
        new_image_int = int(re.sub(r"\D", "", device_info['image_version']))
        current_image_int = int(
            re.sub(
                r"\D",
                "",
                device_info['current_image_version']))
        if new_image_int > current_image_int:
            return ""
        else:
            return "(Image downgrade)"

    # Get device password
    def _get_password(self, device_obj):
        return JobVncApi.decrypt_password(
            encrypted_password=device_obj.physical_router_user_credentials.
            get_password(),
            pwd_key=device_obj.uuid)


def _parse_args():
    arg_parser = argparse.ArgumentParser(description='fabric filters tests')
    arg_parser.add_argument('-p', '--generate_plan',
                            action='store_true', help='Generate Upgrade Plan')
    arg_parser.add_argument('-b', '--next_batch',
                            action='store_true', help='Get Next Batch')
    arg_parser.add_argument('-a', '--all_devices',
                            action='store_true', help='Get All Devices')
    arg_parser.add_argument('-d', '--device_info',
                            action='store_true', help='Get Device Info')
    return arg_parser.parse_args()
