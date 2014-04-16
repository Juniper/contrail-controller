#!/usr/bin/python
#
#Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import json
import copy
import re
import sys

sys.path.insert(1, sys.path[0]+'/../api-venv/lib/python2.7/site-packages')
from vnc_api.vnc_api import *

class DnsProvisioner(object):
    def __init__(self, user, password, tenant, api_server_ip, api_server_port):
        self._admin_user = user
        self._admin_password = password
        self._admin_tenant_name = tenant
        self._api_server_ip =  api_server_ip
        self._api_server_port = api_server_port
        self._vnc_lib = VncApi(self._admin_user, self._admin_password, self._admin_tenant_name,
                               self._api_server_ip,
                               self._api_server_port, '/')
    #end __init__

    @staticmethod
    def is_valid_dns_name(name):
        if len(name) > 255:
            return False
        #if name.endswith("."): # A single trailing dot is legal
        #    name = name[:-1] # strip exactly one dot from the right, if present
        disallowed = re.compile("[^A-Z\d-]", re.IGNORECASE)
        return all( # Split by labels and verify individually
            (label and len(label) <= 63 # length is within proper range
             and not label.startswith("-") and not label.endswith("-") # no bordering hyphens
             and not disallowed.search(label)) # contains only legal characters
            for label in name.split("."))
    #end is_valid_dns_name

    @staticmethod
    def is_valid_ipv4_address(address):
        parts = address.split(".")
        if len(parts) != 4:
            return False
        for item in parts:
            try:
                if not 0 <= int(item) <= 255:
                    return False
            except ValueError:
                return False;
        return True
    #end is_valid_ipv4_address

    def add_virtual_dns(self, name, domain_name, dns_domain, dyn_updates, rec_order, ttl, next_vdns):
        vnc_lib = self._vnc_lib
        domain_name_list = []
        domain_name_list.append(domain_name)
        domain_name_list_list = list(domain_name_list)
        try:
            domain_obj = vnc_lib.domain_read(fq_name=domain_name_list_list)
        except NoIdError:
            print 'Domain ' + domain_name + ' not found!'
            return
       
        if next_vdns and len(next_vdns):
            try:
                next_vdns_obj = vnc_lib.virtual_DNS_read(fq_name_str = next_vdns)
            except NoIdError:
                print 'Virtual DNS ' + next_vdns + ' not found!'
                return

        vdns_str = ':'.join([domain_name, name])
        vdns_data = VirtualDnsType(dns_domain, dyn_updates, rec_order, int(ttl), next_vdns)
        dns_obj = VirtualDns(name, domain_obj, 
                             virtual_DNS_data = vdns_data)
        vnc_lib.virtual_DNS_create(dns_obj)
    #end add_virtual_dns

    def del_virtual_dns(self, vdns_fqname_str):
        vnc_lib = self._vnc_lib
        vdns_fqname = vdns_fqname_str.split(':') 
        #Verify this VDNS is not being referred by any other VDNSs as next-DNS
        vdns_list = vnc_lib.virtual_DNSs_list() 
        vdns_list = json.loads(vdns_list)
        for k, v in vdns_list.iteritems():
            for elem in v:
                fqname = elem["fq_name"][0] + ':' + elem["fq_name"][1]
                if fqname == vdns_fqname_str:
                    continue
                vdns_obj = vnc_lib.virtual_DNS_read(elem["fq_name"])
                vdns_data = vdns_obj.get_virtual_DNS_data()
                if vdns_data.get_next_virtual_DNS() == vdns_fqname_str:
                    print 'Virtual DNS ' + vdns_fqname_str + ' is being referred by ' + vdns_obj.get_fq_name_str()
                    return
        #end vdns_list for
        try:
            vnc_lib.virtual_DNS_delete(vdns_fqname)
        except NoIdError:
            print 'Virtual DNS ' + vdns_fqname_str + ' not found!'
    #end del_virtual_dns

    def add_virtual_dns_record(self, name, vdns_fqname_str, rec_name, rec_type, rec_class, rec_data, rec_ttl):
        vnc_lib = self._vnc_lib
        try:
            vdns_obj = vnc_lib.virtual_DNS_read(fq_name_str = vdns_fqname_str)
        except NoIdError:
            print 'Virtual DNS ' + vdns_fqname_str + ' not found!'
            return
        vdns_rec_data = VirtualDnsRecordType(rec_name, rec_type, rec_class, rec_data, int(rec_ttl))
        vdns_rec_obj = VirtualDnsRecord(name, vdns_obj, vdns_rec_data)
        vnc_lib.virtual_DNS_record_create(vdns_rec_obj)
    #end add_virtual_dns_record

    def del_virtual_dns_record(self, vdns_rec_fqname):
        vnc_lib = self._vnc_lib
        rec_fqname = vdns_rec_fqname.split(':')
        try:
            vnc_lib.virtual_DNS_record_delete(fq_name = rec_fqname)
        except NoIdError:
            print 'Virtual DNS Record ' + vdns_rec_fqname + ' not found!'
    #end del_virtual_dns_record

    def associate_vdns_with_ipam(self, ipam_fqname_str, ipam_dns_method, dns_srv_obj):
        vnc_lib = self._vnc_lib
        ipam_fqname = ipam_fqname_str.split(':')
        try:
            ipam_obj = vnc_lib.network_ipam_read(ipam_fqname)
        except NoIdError:
            print 'Network Ipam ' + ipam_fqname_str + ' not found!'
            return

        if ipam_dns_method == "virtual-dns-server":
            vdns_fqname_str = dns_srv_obj.get_virtual_dns_server_name()
            '''
            Before associating vdns with IPAM, make sure that
            vdns exists by doing a read of vnds
            '''
            vdns_fqname = vdns_fqname_str.split(':') 
            try:
                vdns_obj = vnc_lib.virtual_DNS_read(vdns_fqname)
            except NoIdError:
                print 'Virtual DNS ' + vdns_fqname_str + ' not found!'
                return
            ipam_obj.add_virtual_DNS(vdns_obj)
        
        ipam_mgmt_obj = ipam_obj.get_network_ipam_mgmt()
        if not ipam_mgmt_obj:
            ipam_mgmt_obj = IpamType()
        ipam_mgmt_obj.set_ipam_dns_method(ipam_dns_method)
        if dns_srv_obj:
            ipam_mgmt_obj.set_ipam_dns_server(dns_srv_obj)
        ipam_obj.set_network_ipam_mgmt(ipam_mgmt_obj)
        vnc_lib.network_ipam_update(ipam_obj)
    #end associate_vdns_with_ipam

    def disassociate_vdns_from_ipam(self, ipam_fqname_str, vdns_fqname_str):
        vnc_lib = self._vnc_lib
        ipam_fqname = ipam_fqname_str.split(':')
        try:
            ipam_obj = vnc_lib.network_ipam_read(ipam_fqname)
        except NoIdError:
            print 'Network Ipam ' + ipam_fqname_str + ' not found!'
            return
        if vdns_fqname_str:
            vdns_fqname = vdns_fqname_str.split(':')
            try:
                vdns_obj = vnc_lib.virtual_DNS_read(vdns_fqname)
            except NoIdError:
                print 'Virtual DNS ' + vdns_fqname_str + ' not found!'
                return
            found = False
            vdns_list = ipam_obj.get_virtual_DNS_refs()
            for v_dict in vdns_list:
                vdns_name_list = v_dict['to']
                vdns_fqname_tmp = vdns_name_list[0] + ':' + vdns_name_list[1]
                if vdns_fqname_tmp == vdns_fqname_str:
                    found = True
                    break
            if not found:
                print 'The specified VDNS is not associated to specified Ipam'
                return
            ipam_obj.del_virtual_DNS(vdns_obj)
        ipam_mgmt_obj = ipam_obj.get_network_ipam_mgmt()
        ipam_mgmt_obj.set_ipam_dns_method("default-dns-server")
        ipam_mgmt_obj.set_ipam_dns_server(None)
        ipam_obj.set_network_ipam_mgmt(ipam_mgmt_obj)
        vnc_lib.network_ipam_update(ipam_obj)
    #end disassociate_vdns_with_ipam
# end class DnsProvisioner
