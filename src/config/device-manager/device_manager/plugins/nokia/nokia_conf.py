#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains generic plugin implementation for Nokia devices
"""

from ncclient import manager
from ncclient.xml_ import new_ele
import time
import datetime
from cStringIO import StringIO
from device_conf import DeviceConf
from dm_utils import PushConfigState
from db import PhysicalInterfaceDM
from db import LogicalInterfaceDM
from db import BgpRouterDM
from db import GlobalSystemConfigDM
from db import VirtualMachineInterfaceDM
import abc

class NokiaConf(DeviceConf):
    _vendor = "nokia"

    @classmethod
    def register(cls, plugin_info):
        common_params = { "vendor": cls._vendor }
        plugin_info.update(common_params)
        return super(NokiaConf, cls).register(plugin_info)
    # end register

    def __init__(self):
        self._nc_manager = None
        self.user_creds = self.physical_router.user_credentials
        self.management_ip = self.physical_router.management_ip
        self.timeout = 10
        self.push_config_state = PushConfigState.PUSH_STATE_INIT
        super(NokiaConf, self).__init__()
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
                             device_params = {'name':'alu'},
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
        self.bgp_peers = {}
    # ene initialize

    def device_send(self, conf, default_operation="merge",
                     operation="replace"):
        return None
    # end device_send

    def device_get(self, filters = {}):
        host=self.management_ip
        device_params = {'name':'alu'}
        try:
            with manager.connect(host, port=830,
                                 username=self.user_creds['username'],
                                 password=self.user_creds['password'],
                                 timeout=100,
                                 device_params = device_params,
                                 unknown_host_cb=lambda x, y: True) as m:
                config_data = m.get_config(source='running').data_xml
                with open("/var/log/contrail/%s.xml" % host, 'w') as f:
                    f.write(config_data)
                return config_data
        except Exception as e:
            if self._logger:
                self._logger.error("could not fetch device configuration from router %s: %s" % (
                                          host, e.message))
        return None

    def build_e2_router_config(self):
        if not self.e2_config:
            self.e2_config = NokiaConfE2()
        self.e2_config.config_e2_router_config(self)
    # end build_e2_router_config

    def build_e2_telemetry_config(self):
        if not self.e2_config:
            self.e2_config = NokiaConfE2()
        self.e2_config.config_e2_telemetry_config(self)
    # end build_e2_router_config

    def service_request_rpc(self, rpc_command):
        res = self._nc_manager.rpc(rpc_command)
        return res
    # end service_request_rpc

    def send_conf(self, is_delete=False):
        if not self.has_conf() and not is_delete:
            return 0
        default_operation = "none" if is_delete else "merge"
        operation = "delete" if is_delete else "replace"
        conf = self.e2_config.device_e2_send(self, default_operation, operation)
        return conf
    # end send_conf

# end NokiaConf

class NokiaConfE2(object):

    def config_e2_router_config(self, obj):
        self.config_e2_router_config_nokia(self, obj)
    # end config_e2_router_config

    def config_e2_router_config_nokia(self, obj):
        snmp = False
        lldp = False
        if 'snmp'in obj.physical_router_property:
            snmp = obj.physical_router_property['snmp']
        if 'lldp'in obj.physical_router_property:
            lldp = obj.physical_router_property['lldp']
        obj.add_e2_router_config_xml(snmp, lldp)
    # end config_e2_router_config_nokia

    def config_e2_telemetry_config(self, obj):
        server_ip = None
        server_port = 0
        telemetry_info = self.telemetry_info

        for tkey, tdk in telemetry_info.iteritems():
            if 'server_ip' in tkey:
                server_ip = tdk
            if 'server_port' in tkey:
                server_port = tdk
            if 'resource' in tkey:
                for res_entry in tdk:
                    rname = rpath = rrate = None
                    for res_key, res_value in res_entry.iteritems():
                        if 'rname' in res_key:
                            rname = res_value
                        if 'rpath' in res_key:
                            rpath = res_value
                        if 'rrate' in res_key:
                            rrate = res_value
                    self.config_manager.add_e2_telemetry_per_resource_config_xml(self.management_ip, rname, rpath, rrate)

        self.config_manager.add_e2_telemetry_config_xml(server_ip, server_port)
    # end config_e2_telemetry_config

    def device_e2_send(self, obj, default_operation="merge",
                     operation="replace"):
        e2_router_config = self.e2_router_config
        if e2_router_config is not None:
            config_list = [e2_router_config]
        else:
            config_list = []
        if self.e2_chassis_config is not None:
            if not config_list:
                config_list = self.e2_chassis_config
            else:
                config_list.append(self.e2_chassis_config)

        if self.e2_phy_intf_config is not None:
            if not config_list:
                config_list = self.e2_phy_intf_config
            else:
                config_list.append(self.e2_phy_intf_config)

        if self.e2_fab_intf_config is not None:
            if not config_list:
                config_list = [self.e2_fab_intf_config]
            else:
                config_list.append(self.e2_fab_intf_config)

        if self.e2_routing_config is not None:
            if not config_list:
                config_list = [self.e2_routing_config]
            else:
                config_list.append(self.e2_routing_config)

        if self.e2_services_prot_config is not None:
            if not config_list:
                config_list = [self.e2_services_prot_config]
            else:
                config_list.append(self.e2_services_prot_config)

        if self.e2_services_ri_config is not None:
            if not config_list:
                config_list = [self.e2_services_ri_config]
            else:
                config_list.append(self.e2_services_ri_config)

        if self.e2_telemetry_config is not None:
            telemetry_cfg = etree.Element("services")
            telemetry_cfg.append(self.e2_telemetry_config)
            if not config_list:
                config_list = [telemetry_cfg]
            else:
                config_list.append(telemetry_cfg)

        #no element exists, so delete e2 config.
        if len(config_list) == 1:
           return self.send_e2_netconf([], default_operation="none", operation="delete")
        return self.send_e2_netconf(config_list)
    # end prepare_e2_conf

    def send_e2_netconf(self, new_config, default_operation="merge",
                        operation="replace"):
        config_size = self.send_e2_nokia_netconf(new_config, "merge", "merge")
        return config_size
    # end send_e2_netconf

    def send_e2_nokia_netconf(self, new_config, default_operation, operation):
        self.push_config_state = PushConfigState.PUSH_STATE_INIT
        start_time = None
        config_size = 0
        try:
            with manager.connect(host=self.management_ip, port=830,
                                 username=self.user_creds['username'],
                                 password=self.user_creds['password'],
                                 unknown_host_cb=lambda x, y: True) as m:
                add_config = etree.Element(
                    "config",
                    nsmap={"xc": "urn:ietf:params:xml:ns:netconf:base:1.0"})
                config = etree.SubElement(add_config, "configuration")
                config_group = etree.SubElement(
                    config, "groups", operation=operation)
                contrail_group = etree.SubElement(config_group, "name")
                contrail_group.text = "__contrail-e2__"
                if isinstance(new_config, list):
                    for nc in new_config:
                        config_group.append(nc)
                else:
                    config_group.append(new_config)
                if operation == "delete":
                    apply_groups = etree.SubElement(
                        config, "apply-groups", operation=operation)
                    apply_groups.text = "__contrail-e2__"
                else:
                    apply_groups = etree.SubElement(config, "apply-groups", insert="first")
                    apply_groups.text = "__contrail-e2__"
                self._logger.info("\nsend netconf message: Router %s: %s\n" % (
                    self.management_ip,
                    etree.tostring(add_config, pretty_print=True)))
                config_str = etree.tostring(add_config)
                config_size = len(config_str)
                m.edit_config(
                    target='candidate', config=config_str,
                    test_option='test-then-set',
                    default_operation=default_operation)
                self.commit_stats['total_commits_sent_since_up'] += 1
                start_time = time.time()
                m.commit()
                end_time = time.time()
                self.commit_stats['commit_status_message'] = 'success'
                self.commit_stats['last_commit_time'] = \
                    datetime.datetime.fromtimestamp(
                    end_time).strftime('%Y-%m-%d %H:%M:%S')
                self.commit_stats['last_commit_duration'] = str(
                    end_time - start_time)
                self.push_config_state = PushConfigState.PUSH_STATE_SUCCESS
        except Exception as e:
            if self._logger:
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
    # end send_e2_nokia_netconf

