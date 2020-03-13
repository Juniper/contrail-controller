#!/usr/bin/python

from builtins import str
from builtins import object
from ansible import errors
import json
import glob
import os
import sys
import re
import traceback
import sys

CLI_GROUP = "__cli_contrail_group__"
try:
    PLAYBOOK_BASE = 'opt/contrail/fabric_ansible_playbooks'
    sys.path.append(PLAYBOOK_BASE + "/module_utils")
    from filter_utils import _task_done, _task_error_log, _task_log, FilterLog
except:
    sys.path.append("../fabric-ansible/ansible-playbooks/module_utils")
    PLAYBOOK_BASE = ("../fabric-ansible/ansible-playbooks")
    from filter_utils import _task_done, _task_error_log, _task_log, FilterLog

# from job_manager.job_utils import JobVncApi
from vnc_api.exceptions import NoIdError
from job_manager.job_utils import JobVncApi

# Modules to split string into a list - used by rollback compare role
def split_string(string, seperator=' '):
    try:
        return string.split(seperator)
    except Exception as e:
        raise errors.AnsibleFilterError(
            'split plugin error: %s, string=%s' % str(e), str(string))


def split_regex(string, seperator_pattern):
    try:
        return re.split(seperator_pattern, string)
    except Exception as e:
        raise errors.AnsibleFilterError('split plugin error: %s' % str(e))


class FilterModule(object):
    def filters(self):
        return {

            'create_cli_obj': self.create_cli_obj,
            'cli_sync': self.cli_sync,
            'split': split_string,
            'split_regex': split_regex,
            'cli_diff_list_update': self.cli_diff_list_update

        }

    # end filters

    def _instantiate_filter_log_instance(self, device_name):
        FilterLog.instance("Cli sync filter", device_name)

    # Function to update cli object with cli diff list info
    def create_cli_obj(self, job_ctx, device_mgmt_ip, device_name):
        directory_path = PLAYBOOK_BASE + "/manual_config/" + device_mgmt_ip
        file_list = glob.glob(directory_path + "/*.diff")
        if len(file_list) == 0:
            return "No new cli commits found. Proceeding..."
        else:
            object_type = 'cli_config'
            device_cli_obj_name = device_name + "_" + "cli_config"
            cli_config_payload = {
                "parent_type": "physical-router",
                "fq_name": [
                    "default-global-system-config",
                    device_name,
                    device_cli_obj_name
                ]
            }
            # try to update an existing object else create and then update with
            # diff info
            try:
                vnc_lib = JobVncApi.vnc_init(job_ctx)
                pr_fq_name = ["default-global-system-config", device_name]
                pr_obj = vnc_lib.physical_router_read(fq_name=pr_fq_name)
                pr_obj.set_physical_router_cli_commit_state("out_of_sync")
                vnc_lib.physical_router_update(pr_obj)
                try:
                    cls = JobVncApi.get_vnc_cls(object_type)
                    cli_obj_payload = cls.from_dict(**cli_config_payload)
                    existing_cli_obj = vnc_lib.cli_config_read(
                        fq_name=cli_config_payload.get('fq_name'))
                    updated_cli_obj = self._cli_config_obj_update(
                        job_ctx, None, existing_cli_obj, file_list)
                except NoIdError:
                    new_cli_uuid = vnc_lib.cli_config_create(cli_obj_payload)
                    updated_cli_obj = self._cli_config_obj_update(
                        job_ctx, new_cli_uuid, None, file_list)
            except Exception as exc:
                raise Exception("Database operation has failed with "
                                "exception: %s", str(exc))
            return updated_cli_obj

    # end create_cli_obj

    def _cli_config_obj_update(self, job_ctx, new_cli_uuid, existing_cli_dict,
                               file_list):
        cli_diff_info_list = []
        vnc_lib = JobVncApi.vnc_init(job_ctx)
        # Read cli obj if newly created else take the existing dict
        if new_cli_uuid is not None:
            cli_dict_to_update = vnc_lib.cli_config_read(id=new_cli_uuid)
        else:
            cli_dict_to_update = existing_cli_dict

        # Update object for every item in the filelist
        for file_item in file_list:
            filename = file_item.split("/")[-1]
            file_properties = filename.split("_")
            timestamp = file_properties[-2]
            date = file_properties[-3]
            username = file_properties[-4]
            # Open file to read contents
            file_handler = open(file_item, "r")
            config_changes = file_handler.read()
            if config_changes == "[]":
                continue
            cli_commit_dict = {
                "username": username,
                "time": date + " " + timestamp,
                "config_changes": config_changes
            }
            file_handler.close()
            cli_diff_info_list.append(cli_commit_dict)
            final_update_dict = {"commit_diff_info": cli_diff_info_list}
            cli_dict_to_update.set_commit_diff_list(final_update_dict)
        vnc_lib.cli_config_update(cli_dict_to_update)
        return cli_dict_to_update

    def cli_sync(self, job_ctx, prouter_name, pr_uuid, devices_cli,
                 device_mgmt_ip):
        # find the appropriate device info
        try:
            vnc_lib = JobVncApi.vnc_init(job_ctx)
            partial_accepted_config = ""
            action_to_be_performed = ""
            pr_commit_processed_list = []
            pr_cli_obj_raw = ""
            pr_commit_diff_list = []
            for dict_item in devices_cli:
                if dict_item["device_uuid"] == pr_uuid:
                    # Read the referenced cli object from PR
                    device_cli_objects = dict_item['cli_objects']
                    device_cli_objects = sorted(device_cli_objects,
                                                key = lambda i: i['time'])
                    cli_obj_name = prouter_name + "_" + "cli_config"
                    cli_fq_name = [
                        'default-global-system-config',
                        prouter_name,
                        cli_obj_name]
                    pr_cli_obj_raw = vnc_lib.cli_config_read(
                        fq_name=cli_fq_name)
                    pr_cli_obj = vnc_lib.obj_to_dict(pr_cli_obj_raw)
                    pr_commit_diff_list = \
                        pr_cli_obj["commit_diff_list"]["commit_diff_info"]
                    # Process each of the items, delete items from cli object
                    # if processed successfully.
                    for pr_commit_item in pr_commit_diff_list:
                        for cli_item in device_cli_objects:
                            if cli_item.get('time') == pr_commit_item.get(
                                    'time'):
                                pr_commit_processed_list.append(pr_commit_item)
                                action_to_be_performed = cli_item.get('status')
                                if action_to_be_performed == "accept":
                                    partial_accepted_config += \
                                        self._accept_routine(pr_commit_item, device_mgmt_ip)
                                else:
                                    self._reject_routine(pr_commit_item, device_mgmt_ip)

            # Process file one last time for any delete commands for accept
            # case
            file_path = PLAYBOOK_BASE + "/manual_config" "/" + \
                        device_mgmt_ip + "/approve_config.conf"
            if action_to_be_performed == "accept" and os.path.exists(file_path):
                complete_accepted_config = self._process_file_for_delete_commands(
                    partial_accepted_config, device_mgmt_ip)
                original_acc_config = pr_cli_obj_raw.get_accepted_cli_config()
                if original_acc_config is None:
                    original_acc_config = ""
                updated_acc_config = original_acc_config + complete_accepted_config
                pr_cli_obj_raw.set_accepted_cli_config(updated_acc_config)
                vnc_lib.cli_config_update(pr_cli_obj_raw)
            data_dict = {
                "pr_commit_diff_list": pr_commit_diff_list,
                "pr_commit_item": pr_commit_processed_list,
                "cli_fq_name": cli_fq_name
            }

            self.merge_files(device_mgmt_ip)
            return data_dict
        except Exception as ex:
            errmsg = "Unexpected error: %s\n%s" % (
                str(ex), traceback.format_exc()
            )
            _task_error_log(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,

            }

    # Routine to accept config. This does the following
    # 1.Generate config to delete the config first
    # 2.Generate config to recommit the deleted config to a seperate group
    # 3.Update the accept config field in the cli object to be used during RMA
    def _accept_routine(self, pr_commit_item,
                        device_mgmt_ip):
        contrail_cli_group = CLI_GROUP
        user_commited_config = pr_commit_item.get('config_changes')
        partial_accepted_config = self._accept_config(user_commited_config,
                                                      contrail_cli_group,
                                                      device_mgmt_ip)
        partial_accepted_config_final = self._process_file_for_group(partial_accepted_config,
                                                                     device_mgmt_ip)
        return partial_accepted_config_final

    # Routine to reject config
    # 1. Delete the committed configs
    def _reject_routine(self, pr_commit_item, device_mgmt_ip):
        user_commited_config = pr_commit_item.get('config_changes')
        self._reject_config(user_commited_config, device_mgmt_ip)

    # Delete all the processed commits
    def cli_diff_list_update(self, job_ctx, cli_fq_name,
                             total_commit_diff_list,
                             final_commit_processed_list):
        try:
            vnc_lib = JobVncApi.vnc_init(job_ctx)
            pr_cli_obj_raw = vnc_lib.cli_config_read(fq_name=cli_fq_name)
            for item in final_commit_processed_list:
                total_commit_diff_list.remove(item)
            updated_dict = {"commit_diff_info": total_commit_diff_list}
            pr_cli_obj_raw.set_commit_diff_list(updated_dict)
            vnc_lib.cli_config_update(pr_cli_obj_raw)
            if len(total_commit_diff_list) == 0:
                return True
        except Exception as ex:
            errmsg = "Unexpected error: %s\n%s" % (
                str(ex), traceback.format_exc()
            )
            _task_error_log(errmsg)

    def _process_file_for_group(self, partial_accepted_config, device_mgmt_ip):
        path_to_file = PLAYBOOK_BASE + "/manual_config" "/" + \
                       device_mgmt_ip + "/approve_config.conf"
        fp = ""
        try:
            fp = open(path_to_file, "a+")
            contents = fp.read()
            if CLI_GROUP in contents:
                line = 'set apply-groups __cli_contrail_group__'
                fp.write(line + "\n")
                partial_accepted_config += line + "\n"
        finally:
            fp.close()
        return partial_accepted_config

    # Merge the accept and the delete into one file
    def merge_files(self, device_mgmt_ip):
        path_to_files = PLAYBOOK_BASE + "/manual_config" \
                                        "/" + device_mgmt_ip
        filenames = ['/approve_config.conf', '/reject_config.conf']
        with open(path_to_files + '/final_config.conf', 'w') as outfile:
            for fname in filenames:
                full_filename = path_to_files + fname
                if os.path.exists(full_filename):
                    with open(full_filename) as infile:
                        for line in infile:
                            outfile.write(line)
                else:
                    line = 'set groups __contrail_basic__'
                    outfile.write(line + "\n")

    def _process_file_for_delete_commands(self, partial_acc_config,
                                          device_mgmt_ip):
        # Save the delete commands as well for accept case
        path_to_file = PLAYBOOK_BASE + "/manual_config" "/" + \
            device_mgmt_ip + "/approve_config.conf"

        with open(path_to_file) as fp:
            line = fp.readline()
            prev_line = line
            while prev_line:
                next_line = fp.readline()
                if next_line:
                    if "delete" in next_line and "delete" in prev_line:
                        partial_acc_config += prev_line
                        prev_line = next_line
                    else:
                        prev_line = next_line
                else:
                    if "delete" in prev_line:
                        partial_acc_config += prev_line
                    return partial_acc_config

    def final_set_command_approve_case(self, lcommands, leaf, device_mgmt_ip,
                                       accept_config):

        # Consecutively keep writing all the accept config to a new final
        # config file used to push config down to device later
        command = ' '.join(lcommands)
        path_to_file = PLAYBOOK_BASE + "/manual_config" "/" + \
            device_mgmt_ip + "/approve_config.conf"
        final_command = command + " " + leaf
        file_to_write = ""
        try:
            file_to_write = open(path_to_file, "a+")
            file_to_write.write(final_command + "\n")
        finally:
            file_to_write.close()
        if (CLI_GROUP in final_command):
            accept_config += final_command + "\n"
            return accept_config
        return accept_config

    def final_set_command_reject_case(self, lcommands, leaf, device_mgmt_ip):
        command = ' '.join(lcommands)
        path_to_file = PLAYBOOK_BASE + "/manual_config" "/" + \
            device_mgmt_ip + "/reject_config.conf"
        final_command = command + " " + leaf
        file_to_write = ""
        try:
            file_to_write = open(path_to_file, "a+")
            file_to_write.write(final_command + "\n")
        finally:
            file_to_write.close()

    # Process the config diff that have been accepted
    def _accept_config(self, config, group, device_mgmt_ip):
        accept_config = ""
        l = config.split('\n')
        for i in range(len(l)):
            elem = l[i]
            # Assign root element in []
            if (elem.startswith('[')):
                root_elem = re.sub(r'\[edit(\s|\])', '', elem).replace(']', '')
                root_elem = root_elem.split(" ")
                line_out = ['set']
                line_out.extend(root_elem)
                # ignore changes in Contrail managed groups, not __cli_contrail_group_
                restricted_group = '__contrail_' in str(root_elem)

            # Lines active or inactive
            # Not possible to move configuration to a new group as diff doe
            # snot return fil
            if (elem.startswith('!')) and (not restricted_group):
                clean_elem = elem.strip('! { ... }')
                if ("inactive" in clean_elem):
                    clean_elem = clean_elem.replace("inactive: ", "")
                    linactive = list(line_out)
                    linactive[0] = "deactivate"
                    accept_config = self.final_set_command_approve_case(
                        linactive, clean_elem,
                        device_mgmt_ip, accept_config)

                elif ("active" in clean_elem):
                    clean_elem = clean_elem.replace("active: ", "")
                    linactive = list(line_out)
                    linactive[0] = "activate"
                    accept_config = self.final_set_command_approve_case(
                        linactive, clean_elem,
                        device_mgmt_ip, accept_config)

            # Lines added to configuration with +
            if (elem.startswith('+')) and (not restricted_group):
                clean_elem = elem.strip('+{ ')
                if (';' in clean_elem ) and ('apply-groups' not in clean_elem):
                    if group:
                        # first added comment have to be deleted before being added to correct group provide by user via value "group"
                        line_out[0] = "delete"
                        # if old command contained [], items in bracket need due junos limitation
                        if '[' and ']' in clean_elem:
                            strip_chars = [';', '[', ']']
                            new_clean_elem = clean_elem.translate(None, ''.join(strip_chars))
                            new_clean_elem = new_clean_elem.split(' ')
                            line_out.append(new_clean_elem[0])
                            for k in new_clean_elem:
                                if (k not in new_clean_elem[0]):
                                    accept_config = self.final_set_command_approve_case(line_out, k, device_mgmt_ip, accept_config)
                            line_out.pop()
                        else:
                            accept_config = self.final_set_command_approve_case(line_out, clean_elem.split(";")[0], device_mgmt_ip, accept_config)
                        group_out = line_out[:]
                        # check if old command was part of groups, if not add
                        # groups
                        if ("groups" in group_out):
                            group_out[0] = "set"
                            group_out[2] = group
                        else:
                            group_tmp = 'set groups ' + group
                            group_out[0] = group_tmp
                        accept_config = self.final_set_command_approve_case(group_out, clean_elem.split(";")[0], device_mgmt_ip, accept_config)
                    else:
                        accept_config = self.final_set_command_approve_case(line_out, clean_elem.split(";")[0], device_mgmt_ip, accept_config)
                elif 'apply-groups' in clean_elem:
                    line_out = ['set']
                    accept_config = self.final_set_command_approve_case(line_out, clean_elem.split(";")[0], device_mgmt_ip, accept_config)
                elif clean_elem == '}':  # Up one level remove parent
                    line_out.pop()
                else:
                    line_out.append(clean_elem)

            # Lines added to configuration with -
            if (elem.startswith('-')) and (not restricted_group):
                clean_elem = elem.strip('-{ ')
                line_out = list(line_out)
                line_out[0] = "delete"
                if ';' in clean_elem:
                    # if old command contained [], items in bracket need due junos limitation deleted individually
                    # delete apply-groups [ TEST TEST2 TEST3 ] does not work, only set does
                    if '[' and ']' in clean_elem:
                        strip_chars = [';', '[', ']']
                        new_clean_elem = clean_elem.translate(None, ''.join(strip_chars))
                        new_clean_elem = new_clean_elem.split(' ')
                        line_out.append(new_clean_elem[0])
                        for k in new_clean_elem:
                            if (k not in new_clean_elem[0]):
                                accept_config = self.final_set_command_approve_case(line_out, k, device_mgmt_ip, accept_config)
                        line_out.pop()
                    else:
                        accept_config = self.final_set_command_approve_case(line_out, clean_elem.split(";")[0], device_mgmt_ip, accept_config)
                elif clean_elem == '}':  # Up one level remove parent
                    line_out.pop()
                else:
                    line_out.append(clean_elem)
            _task_done()
        return accept_config

    # Process config diffs that have been rejected
    def _reject_config(self, config, device_mgmt_ip):
        reject_case = True
        root_elem = ['']
        l = config.split('\n')
        line_out = ['delete']
        for i in range(len(l)):
            elem = l[i]
            # Assign root element in []
            if (elem.startswith('[')):
                root_elem = re.sub(r'\[edit(\s|\])', '', elem).replace(']', '')
                line_out = ['delete']
                line_out.append(root_elem)
                # ignore changes in Contrail managed groups, not __cli_contrail_group__
                restricted_group = '__contrail_' in str(root_elem)

            # Lines added to configuration with +
            if (elem.startswith('!')) and (not restricted_group):
                clean_elem = elem.strip('! { ... }')
                if ("inactive" in clean_elem):
                    clean_elem = clean_elem.replace("inactive: ", "")
                    linactive = list(line_out)
                    linactive[0] = "activate"
                    self.final_set_command_reject_case(linactive, clean_elem, device_mgmt_ip)
                elif ("active" in clean_elem):
                    clean_elem = clean_elem.replace("active: ", "")
                    linactive = list(line_out)
                    linactive[0] = "deactivate"
                    self.final_set_command_reject_case(linactive, clean_elem, device_mgmt_ip)

            # Lines added to configuration with +
            if (elem.startswith('+')) and (not restricted_group):
                clean_elem = elem.strip('+{ ')
                line_out[0] = "delete"
                if ';' in clean_elem:
                    if '[' and ']' in clean_elem:
                        strip_chars = [';', '[', ']']
                        new_clean_elem = clean_elem.translate(None, ''.join(strip_chars))
                        new_clean_elem = new_clean_elem.split(' ')
                        line_out.append(new_clean_elem[0])
                        for k in new_clean_elem:
                            if (k not in new_clean_elem[0]):
                                self.final_set_command_reject_case(line_out, k, device_mgmt_ip)
                        line_out.pop()
                    else:
                        self.final_set_command_reject_case(line_out, clean_elem.split(";")[0], device_mgmt_ip)
                elif clean_elem == '}':  # Up one level remove parent
                    line_out.pop()
                else:
                    line_out.append(clean_elem)

            # Lines added to configuration with -
            if (elem.startswith('-')) and (not restricted_group):
                clean_elem = elem.strip('-{ ')
                root_elem = ''
                line_out = list(line_out)
                line_out[0] = "set"
                if ';' in clean_elem:
                    self.final_set_command_reject_case(line_out, clean_elem.split(";")[0], device_mgmt_ip)
                elif clean_elem == '}':  # Up one level remove parent
                    line_out.pop()
                else:
                    line_out.append(clean_elem)

                    # end _reject_config
                    # end cli filter
