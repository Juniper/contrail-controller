#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of data model for physical router
configuration manager
"""
from lxml import etree
from ncclient.xml_ import new_ele
from ncclient import manager
from device_conf import DeviceConf
from dm_utils import PushConfigState
from dm_utils import DMUtils
from sandesh.dm_introspect import ttypes as sandesh
from cfgm_common.vnc_db import DBBase
from cfgm_common.uve.physical_router_config.ttypes import *
from cfgm_common.uve.service_status.ttypes import *
from vnc_api.vnc_api import *
import copy
import socket
import gevent
import re
import traceback
from gevent import queue
from cfgm_common.vnc_object_db import VncObjectDBClient
from netaddr import IPAddress
from cfgm_common.zkclient import IndexAllocator
from cfgm_common import vnc_greenlets
from sandesh_common.vns.constants import DEVICE_MANAGER_KEYSPACE_NAME
from time import gmtime, strftime
import datetime
import db

class DBBaseE2DM(DBBase):
    obj_type = __name__

class PhysicalRouterE2DM():

    def __init__(self, uuid):
        self.uuid = uuid
        self.next_index = 0
        self.ssh_port = 0
        self.device_config = {}
        self.pr = None
    # end __init__

    def fetch_device_config(self, params):
        self.pr = params.get("physical_router")
        if not self.device_config:
            self.device_config = self.pr.config_manager.get_device_config()
    # end fetch_device_config

    def push_config(self):
        if self.pr.delete_config() or not self.pr.is_vnc_managed():
            return
        self.pr.config_manager.initialize()
        #set the state here so based on the type the config is sent.
        self.config_router_mode = self.pr.router_mode

        #self.init_device_config()
        #model = self.device_config.get('product-model')
        #if 'mx' not in model.lower() and 'vsr' not in model.lower():
            #self._logger.error("physical router: %s, product model is not supported. "
                      #"device configuration=%s" % (self.uuid, str(self.device_config)))
            #self.device_config = {}
            #return
        config_size = self.pr.config_manager.push_conf()
        import pdb;pdb.set_trace()

        self.pr.set_conf_sent_state(True)
        self.pr.uve_send()
        if self.pr.config_manager.retry():
            # failed commit: set repush interval upto max value
            self.pr.config_repush_interval = min([2 * self.pr.config_repush_interval,
                                               PushConfigState.get_repush_max_interval()])
            self.pr.block_and_set_config_state(self.pr.config_repush_interval)
        else:
            # successful commit: reset repush interval to base
            self.pr.config_repush_interval = PushConfigState.get_repush_interval()
            if PushConfigState.get_push_delay_enable():
                # sleep, delay=compute max delay between two successive commits
                gevent.sleep(self.get_push_config_interval(config_size))
    # end push_config

class LogicalInterfaceE2DM(object):
    _dict = {}

    def __init__(self):
        self.mtu = 0
        #Nokia specific data -starts
        self.management_ip = None
        self.user_creds = None
        self.nokia = False
        #Nokia specific data -ends
        self.li_type = None
        self.delete_e2_service_nokia_config(None, None, False)
        self.commit_stats = {
            'last_commit_time': '',
            'last_commit_duration': '',
            'commit_status_message': '',
            'total_commits_sent_since_up': 0,
        }
    # end __init__

    def get_nokia_xml_data(self, config):
        add_config = etree.Element(
           "config",
           nsmap={"xc": "urn:ietf:params:xml:ns:netconf:base:1.0"})
        config_data = etree.SubElement(add_config, "configure", xmlns="urn:alcatel-lucent.com:sros:ns:yang:conf-r13")
        if isinstance(config, list):
            for nc in config:
                config_data.append(nc)
        else:
            config_data.append(config)
        config_str = etree.tostring(add_config)
        return add_config
    # end get_nokia_xml_data

    def service_request_rpc(self, m, config):
        config_str = self.get_nokia_xml_data(config)
        self._logger.info("\nsend netconf message: Router %s: %s\n" % (
            self.management_ip,
            etree.tostring(config_str, pretty_print=True)))
        config_size = len(config_str)
        m.edit_config(
            target='running', config=config_str, test_option='test-then-set')
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
    # end service_request_rpc

    def del_nokia_port_config_xml(self, m, port_name):
        p_cfg = etree.Element("port")
        etree.SubElement(p_cfg, "port-id").text = port_name
        etree.SubElement(p_cfg, "shutdown").text = "true"
        p_desc = etree.SubElement(p_cfg, "description", operation="delete")
        p_eth = etree.SubElement(p_cfg, "ethernet")
        p_mode = etree.SubElement(p_eth, "mode", operation="delete")
        p_encap = etree.SubElement(p_eth, "encap-type", operation="delete")
        p_mtu = etree.SubElement(p_eth, "mtu", operation="delete")
        netconf_result = self.service_request_rpc(m, p_cfg)
    # end del_nokia_port_config_xml

    def delete_e2_service_nokia_config(self, obj, pr, deleted=False):
        if deleted == False:
            return
        host=pr.management_ip
        try:
            with manager.connect(host, port=22,
                                 username=pr.user_creds['username'],
                                 password=pr.user_creds['password'],
                                 timeout=100,
                                 device_params = {'name':'alu'},
                                 unknown_host_cb=lambda x, y: True) as m:
                ifparts = obj.name.split('.')
                ifd_name = ifparts[0]
                self.del_nokia_port_config_xml(m, ifd_name)
        except Exception as e:
            if obj._logger:
                obj._logger.error("could not delete service for router %s: %s" % (
                                          host, e.message))
    def delete_state(self, obj, pr):
        if self.nokia == True:
            self.delete_e2_service_nokia_config(obj, pr, True)
    # end delete
# end LogicalInterfaceE2DM

class ServiceEndpointDM(DBBaseE2DM):
    _dict = {}
    obj_type = 'service_endpoint'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.physical_router = None
        self.service_connection_modules = set()
        self.virtual_machine_interface = None
        self.site_id = 0
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.service_name = obj.get('service_name')
        self.update_single_ref('physical_router', obj)
        self.update_multiple_refs('service_connection_module', obj)
        self.update_single_ref('virtual_machine_interface', obj)

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('physical_router', {})
        obj.update_multiple_refs('service_connection_module', {})
        obj.update_single_ref('virtual_machine_interface', {})
        del cls._dict[uuid]
# end class ServiceEndpointDM

class ServiceConnectionModuleDM(DBBaseE2DM):
    _dict = {}
    obj_type = 'service_connection_module'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_endpoints = set()
        self.service_object = None
        self.circuit_id = 0
        self.mtu = 0
        self.no_control_word = False
        #Nokia specific data -starts
        self.management_ip = None
        self.user_creds = None
        self.sap_info = None
        self.sdp_info = None
        self.e2_services_prot_config = None
        self.e2_services_port_config = None
        self.service_connection_info = None
        self.commit_stats = {
            'last_commit_time': '',
            'last_commit_duration': '',
            'commit_status_message': '',
            'total_commits_sent_since_up': 0,
        }
        self.delete_e2_service_nokia_config()
        self.update(obj_dict)
        #Nokai specific data -ends
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.e2service = obj.get('e2service')
        self.service_connection_info = obj.get('service_connection_info')
        self.update_multiple_refs('service_endpoint', obj)
        self.update_single_ref('service_object', obj)
        #Nokia testing!!
        #if obj.is_vnc_managed() and obj.is_conf_sent():
            #self.delete_e2_service_nokia_config(obj)
        #self.delete_e2_service_nokia_config(obj)

    def get_nokia_xml_data(self, config):
        add_config = etree.Element(
           "config",
           nsmap={"xc": "urn:ietf:params:xml:ns:netconf:base:1.0"})
        config_data = etree.SubElement(add_config, "configure", xmlns="urn:alcatel-lucent.com:sros:ns:yang:conf-r13")
        if isinstance(config, list):
            for nc in config:
                config_data.append(nc)
        else:
            config_data.append(config)
        config_str = etree.tostring(add_config)
        return add_config
    # end get_nokia_xml_data

    def service_request_rpc(self, m, config):
        config_str = self.get_nokia_xml_data(config)
        self._logger.info("\nsend netconf message: Router %s: %s\n" % (
            self.management_ip,
            etree.tostring(config_str, pretty_print=True)))
        config_size = len(config_str)
        m.edit_config(
            target='running', config=config_str, test_option='test-then-set')
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
    # end service_request_rpc

    def disable_l2ckt_protocol_config_nokia_xml(self, circuit_id, sap_info, spd_info):
        l2ckt_cfg = etree.Element("epipe")
        etree.SubElement(l2ckt_cfg, "service-id").text = str(circuit_id)
        etree.SubElement(l2ckt_cfg, "shutdown").text = "true"
        l2ckt_sap = etree.SubElement(l2ckt_cfg, "sap")
        etree.SubElement(l2ckt_sap, "sap-id").text = sap_info
        etree.SubElement(l2ckt_sap, "shutdown").text = "true"
        l2ckt_sdp = etree.SubElement(l2ckt_cfg, "spoke-sdp")
        etree.SubElement(l2ckt_sdp, "sdp-id-vc-id").text = spd_info
        etree.SubElement(l2ckt_sdp, "shutdown").text = "true"

        if self.e2_services_prot_config is not None:
            self.e2_services_prot_config.append(l2ckt_cfg)
        else:
            routing_cfg = etree.Element("service")
            routing_cfg.append(l2ckt_cfg)
            self.e2_services_prot_config = routing_cfg
        return self.e2_services_prot_config
    # end disable_l2ckt_protocol_config_nokia_xml

    def clean_l2ckt_protocol_config_nokia_xml(self, circuit_id, sap_info, spd_info):
        l2ckt_cfg = etree.Element("epipe", operation="delete")
        etree.SubElement(l2ckt_cfg, "service-id").text = str(circuit_id)
        l2ckt_sap = etree.SubElement(l2ckt_cfg, "sap", operation="delete")
        etree.SubElement(l2ckt_sap, "sap-id").text = sap_info
        l2ckt_sdp = etree.SubElement(l2ckt_cfg, "spoke-sdp", operation="delete")
        etree.SubElement(l2ckt_sdp, "sdp-id-vc-id").text = spd_info

        if self.e2_services_prot_config is not None:
            self.e2_services_prot_config.append(l2ckt_cfg)
        else:
            e2_services = etree.Element("service")
            e2_services.append(l2ckt_cfg)
            self.e2_services_prot_config = e2_services
        return self.e2_services_prot_config
    # end clean_l2ckt_protocol_config_nokia_xml

    def del_nokia_l2ckt_protocol_config_xml(self, m, circuit_id, sap_info, sdp_info):
        self.e2_services_prot_config = None
        l2ckt_config = self.disable_l2ckt_protocol_config_nokia_xml(circuit_id, \
                sap_info, sdp_info)
        netconf_result = self.service_request_rpc(m, l2ckt_config)
        self.e2_services_prot_config = None
        l2ckt_config = self.clean_l2ckt_protocol_config_nokia_xml(circuit_id, \
                sap_info, sdp_info)
        netconf_result = self.service_request_rpc(m, l2ckt_config)
    # end del_nokia_l2ckt_protocol_config_xml

    def del_nokia_port_config_xml(self, m, port_name):
        p_cfg = etree.Element("port")
        etree.SubElement(p_cfg, "port-id").text = port_name
        etree.SubElement(p_cfg, "shutdown").text = "true"
        p_desc = etree.SubElement(p_cfg, "description", operation="delete")
        p_eth = etree.SubElement(p_cfg, "ethernet")
        p_mode = etree.SubElement(p_eth, "mode", operation="delete")
        p_encap = etree.SubElement(p_eth, "encap-type", operation="delete")
        p_mtu = etree.SubElement(p_eth, "mtu", operation="delete")
        netconf_result = self.service_request_rpc(m, p_cfg)
    # end del_nokia_port_config_xml

    def delete_e2_service_nokia_config(self):
        if not self.service_connection_info \
           or self.service_connection_info['service_type'] == 'fabric-interface':
            return
        service_type = self.service_connection_info['service_type']
        host=self.management_ip
        try:
            with manager.connect(host, port=22,
                                 username=self.user_creds['username'],
                                 password=self.user_creds['password'],
                                 timeout=100,
                                 device_params = {'name':'alu'},
                                 unknown_host_cb=lambda x, y: True) as m:
                if service_type == 'vpws-l2ckt':
                    #delete l2ckt config
                    self.del_nokia_l2ckt_protocol_config_xml(m, self.circuit_id,\
                            self.sap_info, self.sdp_info)
                    if not ':' in self.sap_info:
                        self.del_nokia_port_config_xml(m, self.sap_info)
                elif service_type == 'vpws-l2vpn':
                    #delete l2vpn config
                    self.del_nokia_l2vpn_protocol_config_xml()
                else:
                    self._logger.error("could not delete service for router %s: type %s" % (
                                          host, service_type))
        except Exception as e:
            if self._logger:
                self._logger.error("could not delete service for router %s: %s" % (
                                          host, e.message))

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        #if obj.is_vnc_managed() and obj.is_conf_sent():
            #self.delete_e2_service_nokia_config(obj)
        obj.delete_e2_service_nokia_config()
        obj.update_multiple_refs('service_endpoint', {})
        obj.update_single_ref('service_object', {})
        del cls._dict[uuid]
# end class ServiceConnectionModuleDM

class ServiceObjectDM(DBBaseE2DM):
    _dict = {}
    obj_type = 'service_object'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_connection_module = None
        self.sep_list = None
        self.physical_router = None
        self.service_status = {}
        self.management_ip = None
        self.user_creds = None
        self.service_type = None
        self.update(obj_dict)

    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.service_object_name = obj.get('service_object_name')
        self.update_single_ref('service_connection_module', obj)
        circuit_id = 0
        if self.service_connection_module is not None:
            scm = ServiceConnectionModuleDM.get(self.service_connection_module)
            if scm is not None:
                circuit_id = scm.circuit_id
                if circuit_id == 0 and \
                   scm.service_connection_info['service_type'] != 'fabric-interface':
                    return
                found = False
                neigbor_id = None
                for sindex, sep_uuid in enumerate(scm.service_endpoints):
                    sep = ServiceEndpointDM.get(sep_uuid)
                    if sep is None:
                        continue
                    pr_uuid = sep.physical_router
                    pr = db.PhysicalRouterDM.get(pr_uuid)
                    if pr is not None and pr.vendor.lower() == "juniper" and found != True:
                        self.management_ip = pr.management_ip
                        self.user_creds    = pr.user_credentials
                        self.service_type = scm.service_connection_info['service_type']
                        found = True
                    elif pr is not None:
                        neigbor_id = pr.physical_router_id
                if found == True:
                    service_params = {
                            "service_type": self.service_type,
                            "circuit_id": circuit_id,
                            "neigbor_id": neigbor_id,
                    }
                    self.service_status = self.get_service_status(service_params)
                    self.uve_send()

    def uve_send(self):
        mydata=self.service_status
        if self.service_status is not None:
            last_timestamp = strftime("%Y-%m-%d %H:%M:%S", gmtime())
            pr_trace = UveServiceStatus(
                    name=self.name,
                    ip_address=self.management_ip,
                    service_name=self.name,
                    status_data=str(mydata),
                    operational_status="None",
                    last_get_time=last_timestamp)

            pr_msg = UveServiceStatusTrace(
                data=pr_trace, sandesh=DBBaseE2DM._sandesh)
            pr_msg.send(sandesh=DBBaseE2DM._sandesh)
    # end uve_send
    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('service_connection_module', {})
        del cls._dict[uuid]
# end class ServiceObjectDM

class NetworkDeviceConfigDM(DBBaseE2DM):
    _dict = {}
    obj_type = 'network_device_config'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.physical_router = None
        self.device_configuration = {}
        self.management_ip = None
        #self.user_creds = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.config_object_name = obj.get('config_object_name')
        self.update_single_ref('physical_router', obj)
        if self.physical_router is not None:
            pr = db.PhysicalRouterDM.get(self.physical_router)
            if pr is not None:
                self.management_ip = pr.management_ip
                #self.user_creds    = pr.user_credentials
                #self.vendor        = pr.vendor
                self.device_configuration =  \
                pr.config_manager.device_get_config()
                self.uve_send()

    def uve_send(self):
        mydata=self.device_configuration
        if self.device_configuration is not None:
            last_timestamp = strftime("%Y-%m-%d %H:%M:%S", gmtime())
            pr_trace = UvePhysicalRouterConfiguration(
                    name=self.name,
                    ip_address=self.management_ip,
                    config_data=mydata,
                    last_get_time=last_timestamp)

            pr_msg = UvePhysicalRouterConfigurationTrace(
                data=pr_trace, sandesh=DBBaseE2DM._sandesh)
            pr_msg.send(sandesh=DBBaseE2DM._sandesh)
    # end uve_send

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('physical_router', {})
        del cls._dict[uuid]
# end class NetworkDeviceConfigDM

