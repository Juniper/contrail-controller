#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains generic plugin implementation for juniper devices
"""

from ncclient import manager
from ncclient.xml_ import new_ele
import time
import datetime
from cStringIO import StringIO
from dm_utils import DMUtils
from device_conf import DeviceConf
from dm_utils import PushConfigState
from db import PhysicalInterfaceDM
from db import LogicalInterfaceDM
from db import VirtualMachineInterfaceDM
from device_api.juniper_common_xsd import *
import abc

class JuniperConf(DeviceConf):
    _vendor = "juniper"
    # mapping from contrail family names to junos
    _FAMILY_MAP = {
        'route-target': '',
        'inet-vpn': FamilyInetVpn(unicast=''),
        'inet6-vpn': FamilyInet6Vpn(unicast=''),
        'e-vpn': FamilyEvpn(signaling='')
    }

    @classmethod
    def register(cls, plugin_info):
        common_params = { "vendor": cls._vendor }
        plugin_info.update(common_params)
        return super(JuniperConf, cls).register(plugin_info)
    # end register

    def __init__(self):
        self._nc_manager = None
        self.user_creds = self.physical_router.user_credentials
        self.management_ip = self.physical_router.management_ip
        self.timeout = 10
        self.push_config_state = PushConfigState.PUSH_STATE_INIT
        super(JuniperConf, self).__init__()
    # end __init__

    def update(self):
        if not self.are_creds_modified():
            return
        self.user_creds = self.physical_router.user_credentials
        self.management_ip = self.physical_router.management_ip
        if self.is_connected():
            self.device_disconnect()
            self.device_connect()
    # ene update

    def get_commit_stats(self):
        return self.commit_stats
    # end get_commit_stats

    def retry(self):
        if self.push_config_state == PushConfigState.PUSH_STATE_RETRY:
            return True
        return False
    # end retry

    def are_creds_modified(self):
        user_creds = self.physical_router.user_credentials
        management_ip = self.physical_router.management_ip
        if (self.user_creds != user_creds or self.management_ip != management_ip):
            return True
        return False
    # end are_creds_modified

    def device_connect(self):
        if not self._nc_manager:
            try:
                self._nc_manager = manager.connect(host=self.management_ip, port=22,
                             username=self.user_creds['username'],
                             password=self.user_creds['password'],
                             timeout=10,
                             device_params = {'name':'junos'},
                             unknown_host_cb=lambda x, y: True)
            except Exception as e:
               if self._logger:
                   self._logger.error("could not establish netconf session with "
                           " router %s: %s" % (self.management_ip, e.message))
    # end device_connect

    def device_disconnect(self):
        if self._nc_manager and self._nc_manager.connected:
            self._nc_manager = None
    # end device_disconnect

    def is_connected(self):
        return self._nc_manager and self._nc_manager.connected
    # end is_connected

    def initialize(self):
        self.ri_config = None
        self.routing_instances = {}
        self.interfaces_config = None
        self.services_config = None
        self.policy_config = None
        self.firewall_config = None
        self.inet4_forwarding_filter = None
        self.inet6_forwarding_filter = None
        self.forwarding_options_config = None
        self.global_routing_options_config = None
        self.proto_config = None
        self.route_targets = set()
        self.bgp_peers = {}
        self.external_peers = {}
    # ene initialize

    def device_send(self, conf, default_operation="merge",
                     operation="replace"):
        config_str = self.serialize(conf)
        self.push_config_state = PushConfigState.PUSH_STATE_INIT
        start_time = None
        config_size = 0
        try:
            self.device_connect()
            self._logger.info("\nsend netconf message: %s\n" % config_str)
            config_size = len(config_str)
            self._nc_manager.edit_config(
                    target='candidate', config=config_str,
                    test_option='test-then-set',
                    default_operation=default_operation)
            self.commit_stats['total_commits_sent_since_up'] += 1
            start_time = time.time()
            self._nc_manager.commit()
            end_time = time.time()
            self.commit_stats['commit_status_message'] = 'success'
            self.commit_stats['last_commit_time'] = \
                    datetime.datetime.fromtimestamp(
                    end_time).strftime('%Y-%m-%d %H:%M:%S')
            self.commit_stats['last_commit_duration'] = str(
                    end_time - start_time)
            self.push_config_state = PushConfigState.PUSH_STATE_SUCCESS
        except Exception as e:
            self._logger.error("Router %s: %s" % (self.management_ip,
                                                      e.message))
            self.commit_stats[
                    'commit_status_message'] = 'failed to apply config,\
                                                router response: ' + e.message
            if start_time is not None:
                self.commit_stats['last_commit_time'] = \
                        datetime.datetime.fromtimestamp(
                            start_time).strftime('%Y-%m-%d %H:%M:%S')
                self.commit_stats['last_commit_duration'] = str(
                        time.time() - start_time)
            self.push_config_state = PushConfigState.PUSH_STATE_RETRY
        return config_size
    # end device_send

    def device_get(self, filters = {}):
        try:
            self.device_connect()
            sw_info = new_ele('get-software-information')
            res = self._nc_manager.rpc(sw_info)
            pname = res.xpath('//software-information/product-name')[0].text
            pmodel = res.xpath('//software-information/product-model')[0].text
            ele = res.xpath("//software-information/package-information"
                                "[name='junos-version']")[0]
            jversion = ele.find('comment').text
            dev_conf = {
                'product-name': pname,
                'product-model': pmodel,
                'software-version': jversion
            }
            return dev_conf
        except Exception as e:
            self._logger.error("could not fetch config from router %s: %s" % (
                                          self.management_ip, e.message))
        return {}
    # end device_get

    def get_vn_li_map(self):
        pr = self.physical_router
        vn_dict = {}
        for vn_id in pr.virtual_networks:
            vn_dict[vn_id] = []

        li_set = pr.logical_interfaces
        for pi_uuid in pr.physical_interfaces:
            pi = PhysicalInterfaceDM.get(pi_uuid)
            if pi is None:
                continue
            li_set |= pi.logical_interfaces
        for li_uuid in li_set:
            li = LogicalInterfaceDM.get(li_uuid)
            if li is None:
                continue
            vmi_id = li.virtual_machine_interface
            vmi = VirtualMachineInterfaceDM.get(vmi_id)
            if vmi is None:
                continue
            vn_id = vmi.virtual_network
            vn_dict.setdefault(vn_id, []).append(
                JunosInterface(li.name, li.li_type, li.vlan_tag))
        return vn_dict
    # end

    def prepare_groups(self, is_delete=False):
        groups = Groups()
        if is_delete:
            return groups
        groups.set_comment(DMUtils.groups_comment())
        groups.set_routing_instances(self.ri_config)
        groups.set_interfaces(self.interfaces_config)
        groups.set_services(self.services_config)
        groups.set_policy_options(self.policy_config)
        groups.set_firewall(self.firewall_config)
        groups.set_forwarding_options(self.forwarding_options_config)
        groups.set_routing_options(self.global_routing_options_config)
        groups.set_protocols(self.proto_config)
        return groups
    # end prepare_groups

    def build_conf(self, groups, operation='replace'):
        groups.set_name("__contrail__")
        configuraion = Configuration(groups=groups)
        groups.set_operation(operation)
        apply_groups = ApplyGroups(name="__contrail__")
        configuraion.set_apply_groups(apply_groups)
        if operation == "delete":
            apply_groups.set_operation(operation)
        conf = config(configuration=configuraion)
        return conf
    # end build_conf

    def serialize(self, config):
        xml_data = StringIO()
        config.export_xml(xml_data, 1)
        xml_str = xml_data.getvalue()
        return xml_str.replace("comment>", "junos:comment>", -1)
    # end serialize

    def prepare_conf(self, default_operation="merge", operation="replace"):
        groups = self.prepare_groups(is_delete = True if operation is 'delete' else False)
        return self.build_conf(groups, operation)
    # end prepare_conf

    @abc.abstractmethod
    def has_conf(self):
        """Check if the there is some config to send"""
        return False
    # end has_conf

    def send_conf(self, is_delete=False):
        if not self.has_conf() and not is_delete:
            return 0
        default_operation = "none" if is_delete else "merge"
        operation = "delete" if is_delete else "replace"
        conf = self.prepare_conf(default_operation, operation)
        return self.device_send(conf, default_operation, operation)
    # end send_conf

# end JuniperConf

class JunosInterface(object):

    def __init__(self, if_name, if_type, if_vlan_tag=0, if_ip=None):
        self.name = if_name
        self.if_type = if_type
        self.vlan_tag = if_vlan_tag
        ifparts = if_name.split('.')
        self.ifd_name = ifparts[0]
        self.unit = ifparts[1]
        self.ip = if_ip
    # end __init__

    def is_untagged(self):
        if not self.vlan_tag:
            return True
        return False
    # end is_untagged

# end JunosInterface
