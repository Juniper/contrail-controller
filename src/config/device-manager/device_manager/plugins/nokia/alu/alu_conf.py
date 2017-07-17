#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of netconf interface for ALU router
"""

from db import *
from dm_utils import DMUtils
from nokia_conf import NokiaConf
from lxml import etree

class AluConf(NokiaConf):
    _products = ['alu']

    def __init__(self, logger, params={}):
        self._logger = logger
        self.physical_router = params.get("physical_router")
        super(AluConf, self).__init__()
    # end __init__

    @classmethod
    def register(cls):
        mconf = {
              "vendor": cls._vendor,
              "products": cls._products,
              "class": cls
            }
        return super(AluConf, cls).register(mconf)
    # end register

    @classmethod
    def is_product_supported(cls, name, role):
        if role and not role.lower().startswith('e2-'):
            return False
        for product in cls._products:
            if name.lower().startswith(product.lower()):
                return True
        return False
    # end is_product_supported

    def push_conf(self, is_delete=False):
        if not self.physical_router:
            return 0
        return None
    # end push_conf

    def get_service_status(self, service_params={}):
       #Not implemented yet
        return None
    #end get_service_status

    def config_e2_services(self):
        status = False
        node_role = None
        service_exists = False
        service_index = 0
        #if 'access'in self.name:
        if 'role'in self.physical_router_property:
            node_role = self.physical_router_property['role']
        self._logger.info("Total services on %s is =%d" %
                (self.name, len(self.service_endpoints)))
        ps_config_dict={}
        ps_intf_list=[]
        ps_circuit_id_dict = {}
        for sindex, sep_uuid in enumerate(self.service_endpoints):
            service_exists = True
            prot_configured = False
            service_type = None
            peer_pr_entry = None
            peer_sep_entry = None
            service_fabric = False
            peer_is_nokia = False
            local_is_nokia = False
            peer_router_id = 0
            peer_router_lpbk_id = 0
            local_site_id = 0
            remote_site_id = 0
            peer_phy_circuit_id = 0
            sep = ServiceEndpointDM.get(sep_uuid)
            if sep is None:
                self._logger.info("SEP is NULL for node=%s" %
                                  (self.name))
                continue
            local_site_id = sep.site_id
            pr_uuid = sep.physical_router
            pr_entry = PhysicalRouterDM.get(pr_uuid)
            if pr_entry is None:
                self._logger.info("PR is NULL for node=%s" %
                                  (self.name))
                continue
            if 'as_number'in pr_entry.physical_router_property:
                as_number = pr_entry.physical_router_property['as_number']
            if pr_entry.vendor.lower() == "nokia":
                local_is_nokia = True
            router_id = pr_entry.physical_router_id
            router_lpbk_id = pr_entry.physical_router_loopback_id
            phy_circuit_id = pr_entry.next_index
            for scm_id in sep.service_connection_modules:
                phy_mtu = 0
                li_mtu  = 0
                no_control_word = False
                scm    = ServiceConnectionModuleDM.get(scm_id)
                if scm is None:
                    self._logger.info("SCM is NULL for node=%s, sep=%s" %
                                      (self.name, sep.name))
                    continue
                for skey, sval in scm.service_connection_info.iteritems():
                    if 'set_config' in skey:
                        #set_config = scm.service_connection_info['set_config']
                        set_config = sval
                    if 'service_type' in skey:
                        service_type = sval
                    if 'resources' in skey:
                        mtu_present = False
                        cw_present = False
                        for res_entry in sval:
                            if 'mtu' in res_entry.values():
                                mtu_present = True
                            if 'control-word' in res_entry.values():
                                cw_present = True
                            for res_key, res_value in res_entry.iteritems():
                                if 'rvalue' in res_key and mtu_present == True:
                                    scm.mtu = res_value
                                    phy_mtu = scm.mtu
                                    mtu_present = False
                                if 'rvalue' in res_key and cw_present == True:
                                    scm.no_control_word = True
                                    no_control_word = True
                                    cw_present = False
                if set_config == False:
                    self._logger.info("Skipping sep=%s on node=%s, set_config=%d" %
                                      (sep.name, self.name, set_config))
                    continue
                peer_seps = scm.service_endpoints
                peer_sep_circuit_id = None
                if peer_seps is not None:
                    peer_seps = list(peer_seps)
                    for peer_sep_uuid  in peer_seps:
                        if peer_sep_uuid == sep_uuid:
                            continue;
                        peer_sep_entry = ServiceEndpointDM.get(peer_sep_uuid)
                        remote_site_id = peer_sep_entry.site_id
                        peer_pr_uuid = peer_sep_entry.physical_router
                        peer_pr_entry = PhysicalRouterDM.get(peer_pr_uuid)
                        if peer_pr_entry is not None:
                            peer_phy_circuit_id = peer_pr_entry.next_index
                            peer_router_id = peer_pr_entry.physical_router_id
                            peer_router_lpbk_id = \
                                    peer_pr_entry.physical_router_loopback_id
                            if peer_pr_entry.vendor.lower() == "nokia":
                                peer_is_nokia = True

                if phy_circuit_id > peer_phy_circuit_id:
                    scm_circuit_id = phy_circuit_id
                else:
                    scm_circuit_id = peer_phy_circuit_id
                vmi_id = sep.virtual_machine_interface
                vmi    = VirtualMachineInterfaceDM.get(vmi_id)
                if vmi is None:
                    continue
                li_id = vmi.logical_interface
                li    = LogicalInterfaceDM.get(li_id)
                if li is None:
                    continue
                self._logger.info("Service config,pr=%s, role=%s, sep=%s, service-type=%s" %
                                  (self.name, node_role, sep.name, service_type))
                if service_type == 'fabric-interface' and \
                   service_fabric == False:
                    ps_config = None
                    self.config_manager.add_e2_phy_logical_interface(li.name,
                    li.vlan_tag, service_type, node_role, phy_mtu, li_mtu,
                    peer_is_nokia, ps_config)
                    self.config_manager.add_e2_lo0_config(node_role,
                                                          router_lpbk_id)
                    self.config_manager.add_e2_routing_options(node_role,
                                                               router_id)
                    service_fabric == True
                elif service_type == 'vpws-l2ckt':
                    ps_config = None
                    self.config_manager.add_e2_phy_logical_interface(li.name,
                            li.vlan_tag, service_type, node_role, phy_mtu,
                            li_mtu, peer_is_nokia, ps_config)
                    if scm.circuit_id == 0:
                        service_index = scm_circuit_id + 1
                        scm.circuit_id = service_index
                        pr_entry.next_index = service_index
                        if peer_pr_entry is not None:
                            peer_pr_entry.next_index = service_index
                    else :
                        service_index = scm.circuit_id
                    if local_is_nokia == True:
                        #Nokia logic here - starts
                        ifparts = li.name.split('.')
                        ifd_name = ifparts[0]
                        if li.vlan_tag != 0:
                            scm.sap_info = ifd_name + ':' + str(li.vlan_tag)
                        else:
                            scm.sap_info = ifd_name
                        scm.sdp_info = '4' + ':' + str(service_index)
                        scm.management_ip = pr_entry.management_ip
                        scm.user_creds = pr_entry.user_credentials
                        li.management_ip = pr_entry.management_ip
                        li.user_creds = pr_entry.user_credentials
                        li.nokia = True
                        #Nokia logic here - ends
                    self.add_e2_services_l2ckt_config_xml(node_role,
                            li.name, li.vlan_tag, service_index, peer_router_id,
                            peer_is_nokia, no_control_word)
                elif service_type == 'vpws-l2vpn':
                    ps_config = None
                    self.add_e2_phy_logical_interface(li.name,
                            li.vlan_tag, service_type, node_role, phy_mtu, \
                            li_mtu, peer_is_nokia, ps_config)
                    if scm.circuit_id == 0:
                        service_index = scm_circuit_id + 1
                        scm.circuit_id = service_index
                        pr_entry.next_index = service_index
                        if peer_pr_entry is not None:
                            peer_pr_entry.next_index = service_index
                    else:
                        service_index = scm.circuit_id
                    if local_site_id == 0 and remote_site_id == 0:
                        local_site_id = 1
                        remote_site_id = 2
                        sep.site_id = local_site_id
                        if peer_sep_entry is not None:
                            peer_sep_entry.site_id = remote_site_id
                    # local service endpoint is created, while remote is not yet
                    elif local_site_id != 0:
                        if peer_sep_entry is not None and remote_site_id == 0:
                            remote_site_id = local_site_id + 1
                            peer_sep_entry.site_id = remote_site_id
                    elif remote_site_id != 0:
                        if  local_site_id == 0:
                            local_site_id = remote_site_id + 1
                            sep_entry.site_id = local_site_id
                    self.config_manager.add_e2_services_l2vpn_config_xml(
                            node_role, li.name, li.vlan_tag, as_number,
                            service_index, local_site_id, remote_site_id)
                elif service_type == 'vpws-evpn':
                    self.config_manager.add_e2_chassis_config()
                    vn_obj = VirtualNetworkDM.get(vmi.virtual_network)
                    ifname = li.name.split('.')
                    if 'ps' in ifname[0] and ifname[0] in ps_config_dict:
                        ps_config = ps_config_dict[ifname[0]]
                    elif 'ps' in ifname[0] and not ifname[0] in ps_intf_list:
                        ps_config = None
                        li_name = ifname[0] + '.0'
                        ps_config = \
                               self.config_manager.add_e2_phy_logical_interface(
                                li_name,
                                0, service_type, node_role, phy_mtu,
                                li_mtu, peer_is_nokia, ps_config)
                    else:
                        ps_config = None
                    ps_config = \
                      self.config_manager.add_e2_phy_logical_interface(li.name,
                            li.vlan_tag, service_type, node_role, phy_mtu,
                            li_mtu, peer_is_nokia, ps_config)
                    if 'ps' in ifname[0] and not ifname[0] in ps_config_dict:
                        ps_config_dict[ifname[0]] = ps_config
                    if vn_obj.circuit_id != 0:
                        scm.circuit_id = vn_obj.circuit_id
                    if scm.circuit_id == 0:
                        service_index = scm_circuit_id + 1
                        scm.circuit_id = service_index
                        pr_entry.next_index = service_index
                        if peer_pr_entry is not None:
                            peer_pr_entry.next_index = service_index
                    else :
                        service_index = scm.circuit_id
                    if vn_obj.circuit_id == 0:
                        vn_obj.circuit_id = service_index
                    if not ifname[0] in ps_intf_list:
                        li_name = ifname[0] + '.0'
                        self.config_manager.add_e2_services_pwht_config_mx_xml(
                                node_role, li_name, li.vlan_tag, service_index,
                                peer_router_id,
                                peer_is_nokia, no_control_word)
                        if not ps_intf_list:
                            ps_intf_list = [ifname[0]]
                        else:
                            ps_intf_list.append(ifname[0])
    # end config_e2_services

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

    def get_nokia_xml_data(self, config):
        add_config = etree.Element(
           "config",
           nsmap={"xc": "urn:ietf:params:xml:ns:netconf:base:1.0"})
        config_data = etree.SubElement(add_config, "configure",
                     xmlns="urn:alcatel-lucent.com:sros:ns:yang:conf-r13")
        if isinstance(config, list):
            for nc in config:
                config_data.append(nc)
        else:
            config_data.append(config)
        config_str = etree.tostring(add_config)
        return add_config
    # end get_nokia_xml_data

    def disable_l2ckt_protocol_config_nokia_xml(self, circuit_id, sap_info,
                                                spd_info):
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

    def clean_l2ckt_protocol_config_nokia_xml(self, circuit_id, sap_info,
                                              spd_info):
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

    def del_nokia_l2ckt_protocol_config_xml(self, m, circuit_id, sap_info,
                                            sdp_info):
        self.e2_services_prot_config = None
        l2ckt_config = self.disable_l2ckt_protocol_config_nokia_xml(circuit_id,
                sap_info, sdp_info)
        netconf_result = self.service_request_rpc(m, l2ckt_config)
        self.e2_services_prot_config = None
        l2ckt_config = self.clean_l2ckt_protocol_config_nokia_xml(circuit_id,
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
           or self.service_connection_info['service_type'] == \
           'fabric-interface':
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
                    self.del_nokia_l2ckt_protocol_config_xml(m,
                            self.circuit_id, self.sap_info, self.sdp_info)
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
# end AluConf

class AluIntf(object):
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
        config_data = etree.SubElement(add_config, "configure",
                           xmlns="urn:alcatel-lucent.com:sros:ns:yang:conf-r13")
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

# end AluIntf
