#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import uuid
import netaddr

import svc_monitor.services.loadbalancer.drivers.abstract_driver as abstract_driver

from cfgm_common.zkclient import IndexAllocator
from cfgm_common import exceptions as vnc_exc
from cfgm_common.utils import cgitb_hook
from vnc_api.vnc_api import *
from f5.bigip import bigip as f5_bigip
from f5.bigip import bigip_interfaces

from svc_monitor.config_db import *

APP_COOKIE_RULE_PREFIX = 'app_cookie_'
RPS_THROTTLE_RULE_PREFIX = 'rps_throttle_'

def get_subnet_network_id(client, subnet_id):
    kv_pair = client.kv_retrieve(subnet_id)
    return kv_pair.split()[0]

def get_subnet_cidr(client, subnet_id):
    kv_pair = client.kv_retrieve(subnet_id)
    return kv_pair.split()[1]


class OpencontrailF5LoadbalancerDriver(
        abstract_driver.ContrailLoadBalancerAbstractDriver):

    # BIG-IP containers
    __bigips = {}
    __traffic_groups = []
    ifl_list = {}
    subnet_snat_list = {}
    project_list = {}
    def fill_config_options(self, config_section):
        # F5 Loadbalancer config options
        f5opts = {
            'device_ip': '127.0.0.1',
            'sync_mode': 'replication',
            'global_routed_mode': 'True',
            'ha_mode': 'standalone',
            'use_snat': 'True',
            'vip_vlan': None,
            'num_snat': '1',
            'user': 'admin',
            'password': 'c0ntrail123',
            'mx_name': None,
            'mx_ip': None,
            'mx_f5_interface': None,
            'f5_mx_interface': None,
        }

        f5opts.update(dict(config_section.items(self._name)))
        self.device_ip = f5opts.get('device_ip')
        self.global_routed_mode = f5opts.get('global_routed_mode')
        self.sync_mode = f5opts.get('sync_mode')
        self.ha_mode = f5opts.get('ha_mode')
        self.use_snat = f5opts.get('use_snat')
        self.vip_vlan = f5opts.get('vip_vlan')
        self.num_snat = f5opts.get('num_snat')
        self.icontrol_user = f5opts.get('user')
        self.icontrol_password = f5opts.get('password')
        self.mx_f5_interface = f5opts.get('mx_f5_interface')
        self.f5_mx_interface = f5opts.get('f5_mx_interface')
        self.mx_name = f5opts.get('mx_name')
        self.mx_ip = f5opts.get('mx_ip')

    def __init__(self, name, manager, api, db, args=None):
        self._name = name
        self._vlan_allocator = None
        self._api = api
        self._svc_mon = manager
        self._args = args
        self.db = db
        self.fill_config_options(args.config_sections)
        self.connected = False
        self._init_connection()
        if not self.global_routed_mode:
            self.mx_physical_router = self.create_physical_router(self.mx_name, self.mx_ip)
            self.mx_physical_interface = self.create_ifd_on_mx(self.mx_f5_interface)
    # end  __init__

    def init_traffic_groups(self, bigip):
        self.__traffic_groups = bigip.cluster.get_traffic_groups()
        if '/Common/traffic-group-local-only' in self.__traffic_groups:
            self.__traffic_groups.remove(
                            '/Common/traffic-group-local-only')
        self.__traffic_groups.sort()
    # end init_traffic_groups

    def allocate_vlan(self, net_id):
        if not self._vlan_allocator:
            self._vlan_allocator = IndexAllocator(
                self._svc_mon._zookeeper_client,
                '/id/f5-lb/vlan/',
                4093)

        vlan = self._vlan_allocator.alloc(net_id)
        # Since vlan tag 0 is not valid, increment before returning
        return vlan + 1
    # end allocate_vlan

    def free_vlan(self, vlan):
        vlan = vlan - 1
        if not self._vlan_allocator:
            self._vlan_allocator = IndexAllocator(
                self._svc_mon._zookeeper_client,
                '/id/f5-lb/vlan/',
                4093)

        self._vlan_allocator.delete(vlan)
        if self._vlan_allocator.empty():
            del self._vlan_allocator
            self._vlan_allocator = None
    # end free_vlan

    def create_ifl_on_mx(self, physical_router, physical_interface, vlan_tag):
        interface_name = physical_interface.get_fq_name_str()+'_'+str(vlan_tag)
        try:
            mx_logical_interface = self._api.logical_interface_read(fq_name_str=interface_name)
        except vnc_exc.NoIdError:
            mx_logical_interface = LogicalInterface(interface_name.split(':')[-1], 
                                    physical_interface, vlan_tag)
            mx_logical_interface.uuid = str(uuid.uuid4())
            mx_logical_interface_id = self._api.logical_interface_create(mx_logical_interface)

        return mx_logical_interface
    # end  create_ifl_on_mx

    def create_ifd_on_mx(self, physical_interface):
        interface_name = self.mx_physical_router.get_fq_name_str()+':'+physical_interface
        try:
            mx_physical_interface = self._api.physical_interface_read(fq_name_str=interface_name)
        except vnc_exc.NoIdError:
            mx_physical_interface = PhysicalInterface(physical_interface, self.mx_physical_router)
            mx_physical_interface.uuid = str(uuid.uuid4())
            mx_physical_interface_id = self._api.physical_interface_create(mx_physical_interface)

        return mx_physical_interface
    # end  create_ifd_on_mx

    def create_physical_router(self, router_name, router_ip):
        try:
            physical_router = self._api.physical_router_read(fq_name_str=router_name)
        except vnc_exc.NoIdError:
            physical_router = PhysicalRouter(router_name.split(':')[1], physical_router_management_ip = router_ip,
                                            physical_router_dataplane_ip = router_ip)
            physical_router.uuid = str(uuid.uuid4())
            physical_router_id = self._api.physical_router_create(physical_router)

        return physical_router
    # end  create_physical_router

    def _init_connection(self):
        if not self.connected:
            try:
                bigip = f5_bigip.BigIP(self.device_ip,
                                        self.icontrol_user,
                                        self.icontrol_password,
                                        5,
                                        True,
                                        True)

                self.init_traffic_groups(bigip)

                self.__bigips[self.device_ip] = bigip

                bigips = self.__bigips.values()

                for set_bigip in bigips:
                    set_bigip.group_bigips = bigips
                    set_bigip.sync_mode = self.sync_mode
                    set_bigip.global_routed_mode = self.global_routed_mode
                    set_bigip.assured_networks = []
                    set_bigip.assured_snat_subnets = []
                    set_bigip.assured_gateway_subnets = []
                    set_bigip.local_ip = None

                self.connected = True

            except Exception as exc:
                print 'Could not connect to iControl devices: ', exc.message

    def find_ifl(self, physical_router, net_obj):
        for vmi in net_obj.virtual_machine_interfaces:
            vmi_obj = VirtualMachineInterfaceSM.get(vmi)
            logical_interface = vmi_obj.logical_interface
            if logical_interface is None:
                continue
            ifl_obj = LogicalInterfaceSM.get(logical_interface)
            physical_interface = ifl_obj.physical_interface
            if physical_interface is None:
                continue
            ifd_obj = PhysicalInterfaceSM.get(physical_interface)
            if ifd_obj.physical_router == physical_router.uuid:
                return ifl_obj
        return None
    # end find_ifl

    def create_service(self, pool_info):
        bigips = self.__bigips.values()
        for set_bigip in bigips:
            try:
                self.create_service_on_device(set_bigip, pool_info)
            except Exception as e:
                cgitb_hook(format="text",)

    # end create_service

    def calculate_delta(self, bigip, old_pool_info, new_pool_info):
        update_pool_info = {}
        for key in new_pool_info.keys():
            if new_pool_info[key] != old_pool_info[key]:
                if key == 'params':
                    # update the pool properties
                    for properties in new_pool_info['params'].keys():
                        if properties == old_pool_info['params'][properties]:
                            continue
                        if properties == 'loadbalancer_method':
                            bigip.pool.set_lb_method(name=new_pool_info['id'],
                              lb_method=new_pool_info['params']['loadbalancer_method'],
                              folder=new_pool_info['tenant_id'])
                            self._update_session_persistance(bigip, new_pool_info)

                if key == 'description' or key == 'name':
                    # update the pool description
                    description = new_pool_info['name'] + ':' + new_pool_info['description']
                    bigip.pool.set_description(name=new_pool_info['id'],
                          description=description,
                          folder=new_pool_info['tenant_id'])

                if key == 'virtual_ip':
                    self.delete_vip_service(bigip, old_pool_info)
                    self.create_vip_service(bigip, new_pool_info)
                    update_pool_info['remove_vip'] = old_pool_info['vip']

                if key == 'health_monitors':
                    added_hms = set(new_pool_info[key].keys()) - set(old_pool_info[key].keys())
                    removed_hms = set(old_pool_info[key].keys()) - set(new_pool_info[key].keys())
                    common_hms = set(old_pool_info[key].keys()) & set(new_pool_info[key].keys())
                    if len(added_hms):
                        for hm in added_hms:
                            timeout = int(new_pool_info['health_monitors'][hm]['max_retries']) * int(new_pool_info['health_monitors'][hm]['timeout'])
                            bigip.monitor.create(name=hm,
                                                 mon_type=new_pool_info['health_monitors'][hm]['monitor_type'],
                                                 interval=new_pool_info['health_monitors'][hm]['delay'],
                                                 timeout=timeout,
                                                 send_text=None,
                                                 recv_text=None,
                                                 folder=new_pool_info['tenant_id'])
                            self._update_monitor(bigip, new_pool_info['tenant_id'], hm, 
                                                 new_pool_info['health_monitors'][hm], set_times=False)

                            bigip.pool.add_monitor(name=new_pool_info['id'], monitor_name=hm,
                                                   folder=new_pool_info['tenant_id'])

                    if len(removed_hms):
                        for hm in removed_hms:
                            bigip.pool.remove_monitor(name=new_pool_info['id'], monitor_name=hm,
                                                   folder=new_pool_info['tenant_id'])
                            try:
                                bigip.monitor.delete(name=hm,
                                                     mon_type=old_pool_info['health_monitors'][hm]['monitor_type'],
                                                     folder=new_pool_info['tenant_id'])
                            except:
                                pass
 
                    if len(common_hms):
                        for hm in common_hms:
                            if new_pool_info['health_monitors'][hm] == old_pool_info['health_monitors'][hm]:
                                continue
                            self._update_monitor(bigip, new_pool_info['tenant_id'], hm, 
                                                 new_pool_info['health_monitors'][hm], set_times=True)

                if key == 'members':
                    added_members = set(new_pool_info[key].keys()) - set(old_pool_info[key].keys())
                    removed_members = set(old_pool_info[key].keys()) - set(new_pool_info[key].keys())
                    common_members = set(old_pool_info[key].keys()) & set(new_pool_info[key].keys())
                    if len(added_members):
                        for member in added_members:
                            ip_address=new_pool_info['members'][member]['address']
                            if self.global_routed_mode:
                                ip_address=ip_address + "%0"
                            # Add the new members
                            result = bigip.pool.add_member(
                                                  name=new_pool_info['id'],
                                                  ip_address=ip_address,
                                                  port=int(new_pool_info['members'][member]['protocol_port']),
                                                  folder=new_pool_info['tenant_id'],
                                                  no_checks=True)
                            # check admin state
                            if new_pool_info['members'][member]['admin_state'] == 'true':
                                bigip.pool.enable_member(name=new_pool_info['id'],
                                                    ip_address=ip_address,
                                                    port=int(new_pool_info['members'][member]['protocol_port']),
                                                    folder=new_pool_info['tenant_id'],
                                                    no_checks=True)
                            else:
                                bigip.pool.disable_member(name=new_pool_info['id'],
                                                    ip_address=ip_address,
                                                    port=int(new_pool_info['members'][member]['protocol_port']),
                                                    folder=new_pool_info['tenant_id'],
                                                    no_checks=True)
                            bigip.pool.set_member_ratio(name=new_pool_info['id'],
                                                    ip_address=ip_address,
                                                    port=int(new_pool_info['members'][member]['protocol_port']),
                                                    ratio=int(new_pool_info['members'][member]['weight']),
                                                    folder=new_pool_info['tenant_id'],
                                                    no_checks=True)

                    if len(removed_members):
                        for member in removed_members:
                            ip_address=old_pool_info['members'][member]['address']
                            if self.global_routed_mode:
                                ip_address = ip_address + "%0"
                            # Remove the members
                            bigip.pool.remove_member(name=new_pool_info['id'],
                                  ip_address=ip_address,
                                  port=int(old_pool_info['members'][member]['protocol_port']),
                                  folder=new_pool_info['tenant_id'])
 
                    if len(common_members):
                        for member in common_members:
                            # Update the members
                            for member_property in new_pool_info['members'][member].keys():
                                if new_pool_info['members'][member][member_property] == \
                                   old_pool_info['members'][member][member_property]:
                                    continue
                                if member_property == 'admin_state':
                                    ip_address = new_pool_info['members'][member]['address']
                                    if self.global_routed_mode:
                                        ip_address = ip_address + "%0"
                                    # check admin state
                                    if new_pool_info['members'][member]['admin_state'] == 'true':
                                        bigip.pool.enable_member(name=new_pool_info['id'],
                                                    ip_address=ip_address,
                                                    port=int(new_pool_info['members'][member]['protocol_port']),
                                                    folder=new_pool_info['tenant_id'],
                                                    no_checks=True)
                                    else:
                                        bigip.pool.disable_member(name=new_pool_info['id'],
                                                    ip_address=ip_address,
                                                    port=int(new_pool_info['members'][member]['protocol_port']),
                                                    folder=new_pool_info['tenant_id'],
                                                    no_checks=True)
                                if member_property == 'weight':
                                    ip_address = new_pool_info['members'][member]['address']
                                    if self.global_routed_mode:
                                        ip_address = ip_address + "%0"
                                    bigip.pool.set_member_ratio(name=new_pool_info['id'],
                                                    ip_address=ip_address,
                                                    port=int(new_pool_info['members'][member]['protocol_port']),
                                                    ratio=int(new_pool_info['members'][member]['weight']),
                                                    folder=new_pool_info['tenant_id'],
                                                    no_checks=True)

                if key == 'vip':
                    if old_pool_info['virtual_ip'] == new_pool_info['virtual_ip']:
                        for vip_key in new_pool_info['vip'].keys():
                            if new_pool_info['vip'][vip_key] != old_pool_info['vip'][vip_key]:
                                bigip_vs = bigip.virtual_server
                                if vip_key == 'params':
                                    for vip_property in new_pool_info['vip'][vip_key].keys():
                                        if new_pool_info['vip']['params'][vip_property] == old_pool_info['vip']['params'][vip_property]:
                                            continue
                                        # Update the vip params
                                        if vip_property == 'admin_state':
                                            if new_pool_info['vip']['params']['admin_state'] == 'true':
                                                bigip_vs.enable_virtual_server(name=new_pool_info['virtual_ip'], 
                                                                        folder=new_pool_info['tenant_id'])
                                            else:
                                                bigip_vs.disable_virtual_server(name=new_pool_info['virtual_ip'],
                                                                        folder=new_pool_info['tenant_id'])
                                        elif vip_property == 'connection_limit':
                                            self._update_connection_limit(bigip, new_pool_info)
                                        elif vip_property == 'persistence_type':
                                            self._update_session_persistance(bigip, new_pool_info)
                                        elif vip_property == 'persistence_cookie_name':
                                            self._update_session_persistance(bigip, new_pool_info)

                                if vip_key == 'description' or vip_key == 'name':
                                    # Update the vip description
                                    description = new_pool_info['vip']['name'] + ':' + new_pool_info['vip']['description']
                                    bigip_vs.set_description(name=new_pool_info['virtual_ip'],
                                                             description=description,
                                                             folder=new_pool_info['tenant_id'])

        return update_pool_info
    # end calculate_delta

    def update_service(self, old_pool_info, pool_info):
        bigips = self.__bigips.values()
        for set_bigip in bigips:
            try:
                update_pool_info = self.update_service_on_device(set_bigip, old_pool_info, pool_info)
            except Exception as e:
                cgitb_hook(format="text",)

        for key in update_pool_info.keys():
            if key == "remove_vip":
                self.release_vip_resource(old_pool_info)

    def delete_service(self, pool_info):
        bigips = self.__bigips.values()
        for set_bigip in bigips:
            try:
                self.delete_service_on_device(set_bigip, pool_info)
            except Exception as e:
                cgitb_hook(format="text",)
        self.release_resource(pool_info)
    # end delete_service
 
    def update_service_on_device(self, bigip, old_pool_info, pool_info):
        update_pool_info = self.calculate_delta(bigip, old_pool_info, pool_info)
        return update_pool_info
    # end update_service_on_device

    def delete_members(self, bigip, tenant_id, pool_id, member_list):
        for member in member_list['members'].keys() or []:
            ip_address = member_list['members'][member]['address']
            if self.global_routed_mode:
                ip_address = ip_address + "%0"
            bigip.pool.remove_member(name=pool_id,
                                  ip_address=ip_address,
                                  port=int(member_list['members'][member]['protocol_port']),
                                  folder=tenant_id)
    # end delete_members

    def _create_app_cookie_persist_rule(self, cookiename):
        rule_text = "when HTTP_REQUEST {\n"
        rule_text += " if { [HTTP::cookie " + str(cookiename)
        rule_text += "] ne \"\" }{\n"
        rule_text += "     persist uie [string tolower [HTTP::cookie \""
        rule_text += cookiename + "\"]] 3600\n"
        rule_text += " }\n"
        rule_text += "}\n\n"
        rule_text += "when HTTP_RESPONSE {\n"
        rule_text += " if { [HTTP::cookie \"" + str(cookiename)
        rule_text += "\"] ne \"\" }{\n"
        rule_text += "     persist add uie [string tolower [HTTP::cookie \""
        rule_text += cookiename + "\"]] 3600\n"
        rule_text += " }\n"
        rule_text += "}\n\n"
        return rule_text
    # end _create_app_cookie_persist_rule

    def _create_http_rps_throttle_rule(self, req_limit):
        rule_text = "when HTTP_REQUEST {\n"
        rule_text += " set expiration_time 300\n"
        rule_text += " set client_ip [IP::client_addr]\n"
        rule_text += " set req_limit " + str(req_limit) + "\n"
        rule_text += " set curr_time [clock seconds]\n"
        rule_text += " set timekey starttime\n"
        rule_text += " set reqkey reqcount\n"
        rule_text += " set request_count [session lookup uie $reqkey]\n"
        rule_text += " if { $request_count eq \"\" } {\n"
        rule_text += "   set request_count 1\n"
        rule_text += "   session add uie $reqkey $request_count "
        rule_text += "$expiration_time\n"
        rule_text += "   session add uie $timekey [expr {$curr_time - 2}]"
        rule_text += "[expr {$expiration_time + 2}]\n"
        rule_text += " } else {\n"
        rule_text += "   set start_time [session lookup uie $timekey]\n"
        rule_text += "   incr request_count\n"
        rule_text += "   session add uie $reqkey $request_count"
        rule_text += "$expiration_time\n"
        rule_text += "   set elapsed_time [expr {$curr_time - $start_time}]\n"
        rule_text += "   if {$elapsed_time < 60} {\n"
        rule_text += "     set elapsed_time 60\n"
        rule_text += "   }\n"
        rule_text += "   set curr_rate [expr {$request_count /"
        rule_text += "($elapsed_time/60)}]\n"
        rule_text += "   if {$curr_rate > $req_limit}{\n"
        rule_text += "     HTTP::respond 503 throttled \"Retry-After\" 60\n"
        rule_text += "   }\n"
        rule_text += " }\n"
        rule_text += "}\n"
        return rule_text
    # end _create_http_rps_throttle_rule
    def create_vip_service(self, bigip, pool_info):
        if not self.global_routed_mode:
            # Create VLAN and self ip for VIP subnet/network
            self.create_vlan_interface(bigip, pool_info['id'], pool_info['tenant_id'],
                                   str(pool_info['vip']['vlan_tag']),
                                   pool_info['vip']['vlan_tag'],
                                   pool_info['vip']['cidr'],
                                   pool_info['vip']['self_ip'][0],
                                   pool_info['vip']['self_ip'][1])

            # create SNAT for vip subnet
            for snat_ip in pool_info['vip']['snat_vmi'].keys() or []:
                self.create_snat(bigip, pool_info['tenant_id'], pool_info['tenant_id'], 
                             pool_info['vip']['snat_vmi'][snat_ip][0], snat_ip)


        snat_pool_name = bigip_interfaces.decorate_name(
                                    pool_info['tenant_id'],
                                    pool_info['tenant_id'])
        ip_address = pool_info['vip']['params']['address']
        if self.global_routed_mode:
            ip_address = ip_address + "%0"
            vlan_name = self.vip_vlan
            use_snat = True
            snat_pool_name = None
        else:
            vlan_name = str(pool_info['vip']['vlan_tag'])
            use_snat = self.use_snat
        bigip_vs = bigip.virtual_server
        bigip_vs.create(name=pool_info['virtual_ip'],
                        ip_address=ip_address,
                        mask='255.255.255.255',
                        port=int(pool_info['vip']['params']['protocol_port']),
                        protocol=pool_info['vip']['params']['protocol'],
                        vlan_name=vlan_name,
                        traffic_group='/Common/traffic-group-1',
                        use_snat=use_snat,
                        snat_pool=snat_pool_name,
                        folder=pool_info['tenant_id'],
                        preserve_vlan_name=True)

        description = pool_info['vip']['name'] + ':' + pool_info['vip']['description']
        bigip_vs.set_description(name=pool_info['virtual_ip'],
                                 description=description,
                                 folder=pool_info['tenant_id'])
        bigip_vs.set_pool(name=pool_info['virtual_ip'],
                          pool_name=pool_info['id'],
                          folder=pool_info['tenant_id'])
        if pool_info['vip']['params']['admin_state']:
            bigip_vs.enable_virtual_server(name=pool_info['virtual_ip'], 
                                    folder=pool_info['tenant_id'])
        else:
            bigip_vs.disable_virtual_server(name=pool_info['virtual_ip'],
                                    folder=pool_info['tenant_id'])

        self._update_session_persistance(bigip, pool_info)

        self._update_connection_limit(bigip, pool_info)
    # end create_vip_service


    def _update_session_persistance(self, bigip, pool_info):
        bigip_vs = bigip.virtual_server
        if pool_info['vip']['params']['persistence_type']:
            # branch on persistence type
            persistence_type = pool_info['vip']['params']['persistence_type']
            if persistence_type == 'SOURCE_IP':
                # add source_addr persistence profile
                bigip_vs.set_persist_profile(name=pool_info['virtual_ip'],
                    profile_name='/Common/source_addr',
                    folder=pool_info['tenant_id'])
            elif persistence_type == 'HTTP_COOKIE':
                # HTTP cookie persistence requires an HTTP profile
                bigip_vs.add_profile(name=pool_info['virtual_ip'],
                    profile_name='/Common/http',
                    folder=pool_info['tenant_id'])
                # add standard cookie persistence profile
                bigip_vs.set_persist_profile(
                    name=pool_info['virtual_ip'],
                    profile_name='/Common/cookie',
                    folder=pool_info['tenant_id'])
                if pool_info['params']['loadbalancer_method'] == 'SOURCE_IP':
                    bigip_vs.set_fallback_persist_profile(
                                    name=pool_info['virtual_ip'],
                                    profile_name='/Common/source_addr',
                                    folder=pool_info['tenant_id'])
            elif persistence_type == 'APP_COOKIE':
                # application cookie persistence requires
                # an HTTP profile
                bigip_vs.add_profile(name=pool_info['virtual_ip'],
                                profile_name='/Common/http',
                                folder=pool_info['tenant_id'])
                # make sure they gave us a cookie_name
                if pool_info['vip']['params']['persistence_cookie_name']: 
                    cookie_name = pool_info['vip']['params']['persistence_cookie_name']
                    # create and add irule to capture cookie
                    # from the service response.
                    rule_definition = self._create_app_cookie_persist_rule(cookie_name)
                    # try to create the irule
                    if bigip.rule.create(name=APP_COOKIE_RULE_PREFIX + pool_info['virtual_ip'],
                            rule_definition=rule_definition,
                            folder=pool_info['tenant_id']):
                        # create universal persistence profile
                        bigip_vs.create_uie_profile(name=APP_COOKIE_RULE_PREFIX + pool_info['virtual_ip'],
                                        rule_name=APP_COOKIE_RULE_PREFIX + pool_info['virtual_ip'],
                                        folder=pool_info['tenant_id'])
                    # set persistence profile
                    bigip_vs.set_persist_profile(name=pool_info['virtual_ip'],
                        profile_name=APP_COOKIE_RULE_PREFIX + pool_info['virtual_ip'],
                        folder=pool_info['tenant_id'])
                    if pool_info['params']['loadbalancer_method'] == 'SOURCE_IP':
                        bigip_vs.set_fallback_persist_profile(name=pool_info['virtual_ip'],
                            profile_name='/Common/source_addr', folder=pool_info['tenant_id'])
                else:
                    # if they did not supply a cookie_name
                    # just default to regualar cookie peristence
                    bigip_vs.set_persist_profile(name=pool_info['virtual_ip'],
                        profile_name='/Common/cookie',
                        folder=pool_info['tenant_id'])
                    if pool_info['params']['loadbalancer_method'] == 'SOURCE_IP':
                        bigip_vs.set_fallback_persist_profile(name=pool_info['virtual_ip'],
                            profile_name='/Common/source_addr', folder=pool_info['tenant_id'])
        else:
            bigip_vs.remove_all_persist_profiles(name=pool_info['virtual_ip'], 
                    folder=pool_info['tenant_id'])
    # end _update_session_persistance

    def _update_connection_limit(self, bigip, pool_info):
        bigip_vs = bigip.virtual_server
        rule_name = 'http_throttle_' + pool_info['virtual_ip']

        if int(pool_info['vip']['params']['connection_limit']) > 0:
            # spec says you need to do this for HTTP
            # and HTTPS, but unless you can decrypt
            # you can't measure HTTP rps for HTTPs... Duh..
            if pool_info['vip']['params']['protocol'] == 'HTTP':
                # add an http profile
                bigip_vs.add_profile(name=pool_info['virtual_ip'],
                    profile_name='/Common/http',
                    folder=pool_info['tenant_id'])
                # create the rps irule
                rule_definition = self._create_http_rps_throttle_rule(pool_info['vip']['params']['connection_limit'])
                # try to create the irule
                bigip.rule.create(name=RPS_THROTTLE_RULE_PREFIX +
                                  pool_info['virtual_ip'],
                                  rule_definition=rule_definition,
                                  folder=pool_info['tenant_id'])
                # for the rule text to update becuase
                # connection limit may have changed
                bigip.rule.update(name=RPS_THROTTLE_RULE_PREFIX +
                                  pool_info['virtual_ip'],
                                  rule_definition=rule_definition,
                                  folder=pool_info['tenant_id'])
                            # add the throttle to the vip
                bigip_vs.add_rule(name=pool_info['virtual_ip'],
                                  rule_name=RPS_THROTTLE_RULE_PREFIX + pool_info['virtual_ip'],
                                  priority=500, folder=pool_info['tenant_id'])
            else:
                # if not HTTP.. use connection limits
                bigip_vs.set_connection_limit(name=pool_info['virtual_ip'],
                    connection_limit=int(pool_info['vip']['params']['connection_limit']),
                    folder=pool_info['tenant_id'])
        else:
            # clear throttle rule
            bigip_vs.remove_rule(name=RPS_THROTTLE_RULE_PREFIX + pool_info['virtual_ip'],
                                 rule_name=rule_name, priority=500,
                                 folder=pool_info['tenant_id'])
            # clear the connection limits
            bigip_vs.set_connection_limit(name=pool_info['virtual_ip'],
                            connection_limit=0,
                            folder=pool_info['tenant_id'])
    # end _update_connection_limit

    def delete_vip_service(self, bigip, pool_info):
        # Delete VIP related config
        bigip_vs = bigip.virtual_server
        bigip_vs.delete(name=pool_info['virtual_ip'], folder=pool_info['tenant_id'])
    # end delete_vip_service

    def delete_service_on_device(self, bigip, pool_info):
        # Delete all members
        self.delete_members(bigip, pool_info['tenant_id'], pool_info['id'], pool_info)

        # Delete VIP related config
        self.delete_vip_service(bigip, pool_info)

        # Delete health monitors
        for health_monitor in pool_info['health_monitors'].keys() or []:
            bigip.pool.remove_monitor(name=pool_info['id'], monitor_name=health_monitor,
                                   folder=pool_info['tenant_id'])
            # not sure if the monitor might be in use
            try:
                bigip.monitor.delete(name=health_monitor,
                                     mon_type=pool_info['health_monitors'][health_monitor]['monitor_type'],
                                     folder=pool_info['tenant_id'])
            except:
                pass

        # Delete the Pool config
        bigip.pool.delete(name=pool_info['id'],
                              folder=pool_info['tenant_id'])
    # end delete_service_on_device

    def delete_vlan_interface(self, bigip, tenant_id, vlan_name, self_ip_name):
        bigip.selfip.delete(name=self_ip_name, folder=tenant_id)
        bigip.vlan.delete(name=vlan_name, folder=tenant_id)
    # end delete_vlan_interface

    def create_vlan_interface(self, bigip, pool_id, tenant_id, vlan_name, 
                              vlan_tag, subnet, self_ip_name, self_ip_address):
        # Create VLAN and self ip for Pool subnet/network
        bigip.vlan.create(name=vlan_name,
                          vlanid=vlan_tag,
                          interface=self.f5_mx_interface,
                          folder=tenant_id,
                          description=pool_id)
        netmask = netaddr.IPNetwork(subnet).netmask

        bigip.selfip.create(name=self_ip_name,
                            ip_address=self_ip_address,
                            netmask=netmask,
                            vlan_name=vlan_name,
                            floating=False,
                            folder=tenant_id,
                            preserve_vlan_name=False)
    # end create_vlan_interface

    def create_snat(self, bigip, tenant_id, snat_pool_name, member_snat_name, ip_address):
        tglo = '/Common/traffic-group-1'
        bigip.snat.create(name=member_snat_name,
                          ip_address=ip_address,
                          traffic_group=tglo,
                          snat_pool_name=None,
                          folder=tenant_id)
        bigip.snat.create_pool(name=snat_pool_name,
                               member_name=member_snat_name,
                               folder=tenant_id)
   # end create_snat

    def delete_snat(self, bigip, tenant_id, snat_pool_name, member_snat_name):
        bigip.snat.remove_from_pool(name=snat_pool_name,
                                    member_name=member_snat_name,
                                    folder=tenant_id)
        bigip.snat.delete(name=member_snat_name, folder=tenant_id)
    # end delete_snat

    def create_service_on_device(self, bigip, pool_info):
        if not self.global_routed_mode:
            # create IRB for pool subnet
            self.create_vlan_interface(bigip, pool_info['id'], pool_info['tenant_id'],
                                   str(pool_info['vlan_tag']), 
                                   pool_info['vlan_tag'], 
                                   pool_info['cidr'],
                                   pool_info['self_ip'][0],
                                   pool_info['self_ip'][1])
            # create SNAT for pool subnet
            for snat_ip in pool_info['snat_vmi'].keys() or []:
                self.create_snat(bigip, pool_info['tenant_id'], pool_info['tenant_id'], 
                             pool_info['snat_vmi'][snat_ip][0], snat_ip)

       # create pool on device
        description = pool_info['name'] + ':' + pool_info['description']
        bigip.pool.create(name=pool_info['id'],
                          lb_method=pool_info['params']['loadbalancer_method'],
                          description=description,
                          folder=pool_info['tenant_id'])

        for member in pool_info['members'].keys() or []:
            ip_address=pool_info['members'][member]['address']
            if self.global_routed_mode:
                ip_address= ip_address + "%0"
            result = bigip.pool.add_member(
                                  name=pool_info['id'],
                                  ip_address=ip_address,
                                  port=int(pool_info['members'][member]['protocol_port']),
                                  folder=pool_info['tenant_id'],
                                  no_checks=True)
            # check admin state
            if pool_info['members'][member]['admin_state']:
                bigip.pool.enable_member(name=pool_info['id'],
                                    ip_address=ip_address,
                                    port=int(pool_info['members'][member]['protocol_port']),
                                    folder=pool_info['tenant_id'],
                                    no_checks=True)
            else:
                bigip.pool.disable_member(name=pool_info['id'],
                                    ip_address=ip_address,
                                    port=int(pool_info['members'][member]['protocol_port']),
                                    folder=pool_info['tenant_id'],
                                    no_checks=True)
            bigip.pool.set_member_ratio(name=pool_info['id'],
                                    ip_address=ip_address,
                                    port=int(pool_info['members'][member]['protocol_port']),
                                    ratio=int(pool_info['members'][member]['weight']),
                                    folder=pool_info['tenant_id'],
                                    no_checks=True)

        # create vip and and related objects
        self.create_vip_service(bigip, pool_info)

        # Create health monitors
        for health_monitor in pool_info['health_monitors'].keys() or []:
            timeout = int(pool_info['health_monitors'][health_monitor]['max_retries']) * int(pool_info['health_monitors'][health_monitor]['timeout'])
            bigip.monitor.create(name=health_monitor,
                                 mon_type=pool_info['health_monitors'][health_monitor]['monitor_type'],
                                 interval=pool_info['health_monitors'][health_monitor]['delay'],
                                 timeout=timeout,
                                 send_text=None,
                                 recv_text=None,
                                 folder=pool_info['tenant_id'])
            self._update_monitor(bigip, pool_info['tenant_id'], health_monitor, 
                                 pool_info['health_monitors'][health_monitor], set_times=False)

            bigip.pool.add_monitor(name=pool_info['id'], monitor_name=health_monitor,
                                   folder=pool_info['tenant_id'])
    # end create_service_on_device

    def _update_monitor(self, bigip, folder, monitor_id, monitor_data, set_times=True):
        if set_times:
            timeout = int(monitor_data['max_retries']) * int(monitor_data['timeout'])
            # make sure monitor attributes are correct
            bigip.monitor.set_interval(name=monitor_id,
                               mon_type=monitor_data['monitor_type'],
                               interval=monitor_data['delay'],
                               folder=folder)
            bigip.monitor.set_timeout(name=monitor_id,
                              mon_type=monitor_data['monitor_type'],
                              timeout=timeout,
                              folder=folder)

        if monitor_data['monitor_type'] == 'HTTP' or monitor_data['monitor_type'] == 'HTTPS':
            if 'url_path' in monitor_data:
                send_text = "GET " + monitor_data['url_path'] + \
                                                " HTTP/1.0\\r\\n\\r\\n"
            else:
                send_text = "GET / HTTP/1.0\\r\\n\\r\\n"

            if 'expected_codes' in monitor_data:
                try:
                    if monitor_data['expected_codes'].find(",") > 0:
                        status_codes = \
                            monitor_data['expected_codes'].split(',')
                        recv_text = "HTTP/1\.(0|1) ("
                        for status in status_codes:
                            int(status)
                            recv_text += status + "|"
                        recv_text = recv_text[:-1]
                        recv_text += ")"
                    elif monitor_data['expected_codes'].find("-") > 0:
                        status_range = \
                            monitor_data['expected_codes'].split('-')
                        start_range = status_range[0]
                        int(start_range)
                        stop_range = status_range[1]
                        int(stop_range)
                        recv_text = \
                            "HTTP/1\.(0|1) [" + \
                            start_range + "-" + \
                            stop_range + "]"
                    else:
                        int(monitor_data['expected_codes'])
                        recv_text = "HTTP/1\.(0|1) " + \
                                    monitor_data['expected_codes']
                except:
                    recv_text = "HTTP/1\.(0|1) 200"
            else:
                recv_text = "HTTP/1\.(0|1) 200"

            bigip.monitor.set_send_string(name=monitor_id,
                                          mon_type=monitor_data['monitor_type'],
                                          send_text=send_text,
                                          folder=folder)
            bigip.monitor.set_recv_string(name=monitor_id,
                                          mon_type=monitor_data['monitor_type'],
                                          recv_text=recv_text,
                                          folder=folder)
    # end _update_monitor

    def release_pool_resource(self, pool_info):
        if self.global_routed_mode:
            return
        ifl_uuid = pool_info[u'mx_ifl']
        if ifl_uuid not in self.ifl_list:
            return
        self.ifl_list[ifl_uuid].remove(pool_info['id'])

        if not len(self.ifl_list[ifl_uuid]):
            bigips = self.__bigips.values()
            for set_bigip in bigips:
                # Delete POOL selfip
                self.delete_vlan_interface(set_bigip, pool_info['tenant_id'], 
                                   str(pool_info['vlan_tag']), 
                                   pool_info['self_ip'][0])
            # Release the VLAN 
            self.free_vlan(pool_info['vlan_tag'])

            # delete the IFL
            self._api.logical_interface_delete(id=pool_info[u'mx_ifl'])

            vmi = VirtualMachineInterfaceSM.get(pool_info[u'self_ip'][2])
            for iip_id in vmi.instance_ips:
                self._api.instance_ip_delete(id=iip_id)
            self._api.virtual_machine_interface_delete(id=pool_info[u'self_ip'][2])
            del(self.ifl_list[ifl_uuid])

        subnet_id = pool_info[u'subnet']
        if subnet_id not in self.subnet_snat_list:
            return
        self.subnet_snat_list[subnet_id]['id'].remove(pool_info['id'])
        if not len(self.subnet_snat_list[subnet_id]['id']):
            del(self.subnet_snat_list[subnet_id])
            for snat_ip in pool_info['snat_vmi'].keys() or []:
                vmi = VirtualMachineInterfaceSM.get(pool_info['snat_vmi'][snat_ip][1])
                for iip_id in vmi.instance_ips:
                    self._api.instance_ip_delete(id=iip_id)
                self._api.virtual_machine_interface_delete(id=pool_info['snat_vmi'][snat_ip][1])
                bigips = self.__bigips.values()
                for set_bigip in bigips:
                    self.delete_snat(set_bigip, pool_info['tenant_id'], pool_info['tenant_id'], 
                             pool_info['snat_vmi'][snat_ip][0])

    # end release_pool_resource

    def release_vip_resource(self, pool_info):
        if self.global_routed_mode:
            return
        ifl_uuid = pool_info['vip'][u'mx_ifl']
        if ifl_uuid not in self.ifl_list:
            return
        self.ifl_list[ifl_uuid].remove(pool_info['vip']['id'])
        if not len(self.ifl_list[ifl_uuid]):
            bigips = self.__bigips.values()
            for set_bigip in bigips:
                # Delete VIP selfip
                self.delete_vlan_interface(set_bigip, pool_info['tenant_id'], 
                                   str(pool_info['vip']['vlan_tag']), 
                                   pool_info['vip']['self_ip'][0])
            # Release the VLAN 
            self.free_vlan(pool_info['vip']['vlan_tag'])

            # delete the IFL
            self._api.logical_interface_delete(id=ifl_uuid)

            # delete the VMI
            vmi = VirtualMachineInterfaceSM.get(pool_info['vip'][u'self_ip'][2])
            for iip_id in vmi.instance_ips:
                self._api.instance_ip_delete(id=iip_id)
            self._api.virtual_machine_interface_delete(id=pool_info['vip'][u'self_ip'][2])
            del(self.ifl_list[ifl_uuid])

        subnet_id = pool_info[u'vip'][u'subnet']
        if subnet_id not in self.subnet_snat_list:
            return
        self.subnet_snat_list[subnet_id]['id'].remove(pool_info['vip']['id'])
        if not len(self.subnet_snat_list[subnet_id]['id']):
            del(self.subnet_snat_list[subnet_id])
            for snat_ip in pool_info['vip']['snat_vmi'].keys() or []:
                vmi = VirtualMachineInterfaceSM.get(pool_info['vip']['snat_vmi'][snat_ip][1])
                for iip_id in vmi.instance_ips:
                    self._api.instance_ip_delete(id=iip_id)
                self._api.virtual_machine_interface_delete(id=pool_info['vip']['snat_vmi'][snat_ip][1])
                bigips = self.__bigips.values()
                for set_bigip in bigips:
                    self.delete_snat(set_bigip, pool_info['tenant_id'], pool_info['tenant_id'], 
                             pool_info['vip']['snat_vmi'][snat_ip][0])
    # end release_vip_resource

    def release_resource(self, pool_info):
        self.release_vip_resource(pool_info)
        self.release_pool_resource(pool_info)
        if pool_info['tenant_id'] not in self.project_list:
            return
        if pool_info['id'] in self.project_list[pool_info['tenant_id']]:
            self.project_list[pool_info['tenant_id']].remove(pool_info['id'])
        if not len(self.project_list[pool_info['tenant_id']]):
            bigips = self.__bigips.values()
            for bigip in bigips:
                bigip.arp.delete_all(folder=pool_info['tenant_id'])
                bigip.route.delete_domain(folder=pool_info['tenant_id'])
                bigip.system.delete_folder(
                            folder=bigip.decorate_folder(pool_info['tenant_id']))
            del(self.project_list[pool_info['tenant_id']])
    # end release_resource

    def locate_resources(self, pool, add_change=True):
        pool_in_db = None
        pool_db_read = self.db.pool_driver_info_get(pool)
        if pool_db_read and 'f5' in pool_db_read:
            pool_in_db = pool_db_read['f5']
        if not add_change:
            return (pool_in_db, None)

        new_pool_info = {}

        pool_obj = self._get_pool(pool)

        if pool_obj.parent_uuid not in self.project_list:
            self.project_list[pool_obj.parent_uuid] = set()
        self.project_list[pool_obj.parent_uuid].add(pool_obj.uuid)

        pool_subnet_id = pool_obj.params['subnet_id']
        pool_subnet_cidr = get_subnet_cidr(self._api, pool_subnet_id)
        pool_net_id = get_subnet_network_id(self._api, pool_subnet_id)
        pool_network_obj = self._get_network(pool_net_id)
        new_members = {}
        for member in pool_obj.members or []:
            member_obj = LoadbalancerMemberSM.get(member)
            if not member_obj:
                return (None, None)
            new_members[member] = member_obj.params
        vip_obj = self._get_vip(pool_obj.virtual_ip)
        if not vip_obj:
            return (None, None)
        vip_subnet_id = vip_obj.params['subnet_id']
        vip_subnet_cidr = get_subnet_cidr(self._api, vip_subnet_id)
        vip_net_id = get_subnet_network_id(self._api, vip_subnet_id)
        vip_network_obj = self._get_network(vip_net_id)

        new_pool_info[u'id'] = pool_obj.uuid
        new_pool_info[u'name'] = pool_obj.name
        new_pool_info[u'description'] = pool_obj.id_perms['description']
        new_pool_info[u'tenant_id'] = pool_obj.parent_uuid
        new_pool_info[u'virtual_ip'] = pool_obj.virtual_ip

        new_pool_info[u'subnet'] = pool_subnet_id
        new_pool_info[u'cidr'] = pool_subnet_cidr
        new_pool_info[u'network_id'] = pool_net_id
        new_pool_info[u'params'] = pool_obj.params

        new_pool_info[u'members'] = new_members

        hms = {}
        for hm in pool_obj.loadbalancer_healthmonitors or []:
            hm_obj = HealthMonitorSM.get(hm)
            if not hm_obj:
                return (None, None)
            hms[hm] = hm_obj.params

        new_pool_info[u'health_monitors'] = hms

        vip_info = {}
        vip_info[u'name'] = vip_obj.name
        vip_info[u'id'] = vip_obj.uuid
        vip_info[u'description'] = vip_obj.id_perms['description']
        vip_info[u'subnet'] = vip_subnet_id
        vip_info[u'cidr'] = vip_subnet_cidr
        vip_info[u'network_id'] = vip_net_id
        vip_info[u'params'] = vip_obj.params

        if self.global_routed_mode:
            new_pool_info[u'vip'] = vip_info
            return (pool_in_db, new_pool_info)
        ifl_uuid = None
        if pool_in_db is None:
            # Locate the IFL for pool
            ifl = self.find_ifl(self.mx_physical_router, pool_network_obj)
            ports_created = {}
            if ifl is None:
                name = 'self_ip_' + pool + '_' + pool_obj.virtual_ip + '_' + pool_subnet_id
                ports_created = self._create_port(pool_obj.parent_uuid, pool_subnet_id, name, 1)
                pool_vlan_id = self.allocate_vlan(name)
                ifl = self.create_ifl_on_mx(self.mx_physical_router, self.mx_physical_interface, pool_vlan_id)
                # Link the ifl and VMI
                for port in ports_created.values() or []:
                    vmi = port
                    ifl.set_virtual_machine_interface(port)
                    self._api.logical_interface_update(ifl)
            else:
                vmi = VirtualMachineInterfaceSM.get(ifl.virtual_machine_interface)
                for iip_id in vmi.instance_ips:
                    ports_created[InstanceIpSM.get(iip_id).address] = vmi
            new_pool_info[u'mx_ifl'] = ifl.uuid
            new_pool_info[u'vlan_tag'] = ifl.logical_interface_vlan_tag
            new_pool_info[u'self_ip'] = (vmi.name, ports_created.keys()[0], ports_created.values()[0].uuid)

            ifl_uuid = ifl.uuid
            if self.use_snat:
                if pool_subnet_id not in self.subnet_snat_list:
                    # Allocate VMI for SNAT for client initiated traffic
                    name = 'snat_' + pool + '_' + pool_obj.virtual_ip + '_' + pool_subnet_id
                    ports_created = self._create_port(pool_obj.parent_uuid, pool_subnet_id, name, self.num_snat)
                    pool_snat_list = {}
                    for port in ports_created.keys() or []:
                        pool_snat_list[port] = (ports_created[port].name, ports_created[port].uuid)
                else:
                    pool_snat_list = {}
                    for port in self.subnet_snat_list[pool_subnet_id]['vmi_list'] or []:
                        vmi = VirtualMachineInterfaceSM.get(port)
                        for iip_id in vmi.instance_ips:
                            pool_snat_list[InstanceIpSM.get(iip_id).address] = (vmi.name, port)
                new_pool_info[u'snat_vmi'] = pool_snat_list
        else:
            new_pool_info[u'mx_ifl'] = pool_in_db[u'mx_ifl']
            ifl_uuid = pool_in_db[u'mx_ifl']
            new_pool_info[u'vlan_tag'] = pool_in_db[u'vlan_tag'] 
            new_pool_info[u'snat_vmi'] = pool_in_db[u'snat_vmi']
            new_pool_info[u'self_ip'] = pool_in_db[u'self_ip']

        if ifl_uuid not in self.ifl_list:
            self.ifl_list[ifl_uuid] = set()
        self.ifl_list[ifl_uuid].add(pool)

        if pool_subnet_id not in self.subnet_snat_list:
            self.subnet_snat_list[pool_subnet_id] = {'vmi_list': set(), 'id' : set()}

        self.subnet_snat_list[pool_subnet_id]['id'].add(pool)

        for snat_ip in new_pool_info['snat_vmi'].keys() or []:
            self.subnet_snat_list[pool_subnet_id]['vmi_list'].add(new_pool_info['snat_vmi'][snat_ip][1])

        if pool_in_db is None or pool_in_db[u'virtual_ip'] != pool_obj.virtual_ip:
            # Locate the IFL for vip
            ports_created = {}
            ifl = self.find_ifl(self.mx_physical_router, vip_network_obj)
            if ifl is None:
                name = 'self_ip_' + pool + '_' + pool_obj.virtual_ip + '_' + vip_subnet_id
                ports_created = self._create_port(vip_obj.parent_uuid, vip_subnet_id, name, 1)
                vip_vlan_id = self.allocate_vlan(name)
                ifl = self.create_ifl_on_mx(self.mx_physical_router, self.mx_physical_interface, vip_vlan_id)
                # Link the ifl and VMI
                for port in ports_created.values() or []:
                    vmi = port
                    ifl.set_virtual_machine_interface(port)
                    self._api.logical_interface_update(ifl)
            else:
                vmi = VirtualMachineInterfaceSM.get(ifl.virtual_machine_interface)
                for iip_id in vmi.instance_ips:
                    ports_created[InstanceIpSM.get(iip_id).address] = vmi
            ifl_uuid = ifl.uuid
            vip_info[u'mx_ifl'] = ifl_uuid
            vip_info[u'vlan_tag'] = ifl.logical_interface_vlan_tag
            vip_info[u'self_ip'] = (vmi.name, ports_created.keys()[0], ports_created.values()[0].uuid)
            if self.use_snat:
                if vip_subnet_id not in self.subnet_snat_list:
                    # Allocate VMI for SNAT for server initiated traffic
                    name = 'snat_' + pool + '_' + pool_obj.virtual_ip + '_' + vip_subnet_id
                    ports_created = self._create_port(pool_obj.parent_uuid, vip_subnet_id, name, self.num_snat)
                    vip_snat_list = {}
                    for port in ports_created.keys() or []:
                        vip_snat_list[port] = (ports_created[port].name, ports_created[port].uuid)
                else:
                    vip_snat_list = {}
                    for port in self.subnet_snat_list[vip_subnet_id]['vmi_list'] or []:
                        vmi = VirtualMachineInterfaceSM.get(port)
                        for iip_id in vmi.instance_ips:
                            vip_snat_list[InstanceIpSM.get(iip_id).address] = (vmi.name, port)
                vip_info[u'snat_vmi'] = vip_snat_list
        else:
            vip_info[u'mx_ifl'] = pool_in_db[u'vip'][u'mx_ifl']
            ifl_uuid =  pool_in_db[u'vip'][u'mx_ifl']
            vip_info[u'vlan_tag'] = pool_in_db[u'vip'][u'vlan_tag'] 
            vip_info[u'snat_vmi'] = pool_in_db[u'vip'][u'snat_vmi']
            vip_info[u'self_ip'] = pool_in_db[u'vip'][u'self_ip']

        new_pool_info[u'vip'] = vip_info

        if ifl_uuid not in self.ifl_list:
            self.ifl_list[ifl_uuid] = set()
        self.ifl_list[ifl_uuid].add(vip_obj.uuid)

        if vip_subnet_id not in self.subnet_snat_list:
            self.subnet_snat_list[vip_subnet_id] = {'vmi_list': set(), 'id' : set()}

        self.subnet_snat_list[vip_subnet_id]['id'].add(vip_obj.uuid)

        for snat_ip in new_pool_info['vip']['snat_vmi'].keys() or []:
            self.subnet_snat_list[vip_subnet_id]['vmi_list'].add(
                           new_pool_info['vip']['snat_vmi'][snat_ip][1])

        return (pool_in_db, new_pool_info)
    # end locate_resources

    def _get_pool(self, pool_id):
        pool_obj = LoadbalancerPoolSM.get(pool_id)
        return pool_obj
    # end _get_pool

    def _get_vip(self, vip):
        vip_obj = VirtualIpSM.get(vip)
        return vip_obj
    # end _get_vip

    def _get_member(self, member):
        member_obj = LoadbalancerMemberSM.get(member)
        return member_obj
    # end _get_member

    def _get_network(self, net_id):
        net_obj = VirtualNetworkSM.get(net_id)
        return net_obj
    # end _get_network

    def _get_project(self, tenant_id):
        project_obj = ProjectSM.get(tenant_id)
        return project_obj
    # end _get_project

    def _create_port(self, tenant_id, subnet_id, name_prefix, num_ips):
        proj_obj = self._get_project(tenant_id)
        vnc_project = Project(proj_obj.name, parent_type = 'domain', fq_name = proj_obj.fq_name)
        vnc_project.uuid = proj_obj.uuid

        net_id = get_subnet_network_id(self._api, subnet_id)
        net_obj = self._get_network(net_id)
        vnc_net = VirtualNetwork(net_obj.name, parent_type = 'project', fq_name = net_obj.fq_name)
        vnc_net.uuid = net_obj.uuid

        ports_created = {}
        for i in range(int(num_ips)):
            if num_ips == 1:
                name = name_prefix
            else:
                name = name_prefix + '_' + str(i)
            port_obj = self._create_vmi(name, vnc_project, vnc_net)
            ip_addr, ip_uuid = self._allocate_ip(port_obj, vnc_net, subnet_id)
            ports_created[ip_addr] = port_obj
        return ports_created
    # end _create_port

    def _allocate_ip(self, port_obj, net_obj, subnet_id):
        ip_name = str(uuid.uuid4())
        ip_obj = InstanceIp(name=ip_name)
        ip_obj.uuid = ip_name
        if subnet_id:
            ip_obj.set_subnet_uuid(subnet_id)
        ip_obj.set_virtual_machine_interface(port_obj)
        ip_obj.set_virtual_network(net_obj)
        ip_obj.set_instance_ip_family("v4")
        ip_id = self._api.instance_ip_create(ip_obj)
        obj = InstanceIpSM.locate(ip_id)
        return obj.address, ip_id
    # end _allocate_ip

    def _create_vmi(self, name, proj_obj, net_obj):
        id_perms = IdPermsType(enable=True)
        port_obj = VirtualMachineInterface(name, proj_obj, id_perms=id_perms)
        port_obj.uuid = str(uuid.uuid4())
        port_obj.set_virtual_network(net_obj)
        port_obj.display_name = name
        port_obj.set_virtual_machine_interface_device_owner("F5:Snat")
        try:
            self._api.virtual_machine_interface_create(port_obj)
        except Exception as e:
            print str(e)
            pass
        return port_obj
    # end _create_vmi

    def create_vip(self, vip):
        pass
    # end  create_vip

    def update_vip(self, old_vip, vip):
        pass
    # end  update_vip

    def delete_vip(self, vip):
        pass
    # end  delete_vip

    def create_pool(self, pool):
        try:
            if 'vip_id' in pool and pool['vip_id']:
                old_pool_svc, new_pool_svc = self.locate_resources(pool['id'])
            else:
                old_pool_svc, new_pool_svc = self.locate_resources(pool['id'], False)
            if old_pool_svc is None and new_pool_svc is None:
                return
            elif old_pool_svc is None:
                self.create_service(new_pool_svc)
            elif new_pool_svc is None:
                self.delete_service(old_pool_svc)
                self.db.pool_remove(pool['id'], ['driver_info'])
                return
            elif old_pool_svc and old_pool_svc != new_pool_svc:
                self.update_service(old_pool_svc, new_pool_svc)
            else:
                return
            self.db.pool_driver_info_insert(pool['id'], {'f5': new_pool_svc})
        except Exception as e:
            cgitb_hook(format="text",)
    # end  create_pool

    def update_pool(self, old_pool, pool):
        try:
            if 'vip_id' in pool and pool['vip_id']:
                old_pool_svc, new_pool_svc = self.locate_resources(pool['id'])
            else:
                old_pool_svc, new_pool_svc = self.locate_resources(pool['id'], False)
            if old_pool_svc is None and new_pool_svc is None:
                return
            elif old_pool_svc is None:
                self.create_service(new_pool_svc)
            elif new_pool_svc is None:
                self.delete_service(old_pool_svc)
                self.db.pool_remove(pool['id'], ['driver_info'])
                return
            elif old_pool_svc != new_pool_svc:
                self.update_service(old_pool_svc, new_pool_svc)
            else:
                return
            self.db.pool_driver_info_insert(pool['id'], {'f5': new_pool_svc})
        except Exception as e:
            cgitb_hook(format="text",)
    # end  update_pool

    def delete_pool(self, pool):
        old_pool_svc, new_pool_svc = self.locate_resources(pool['id'], False)
        if old_pool_svc:
            self.delete_service(old_pool_svc)
            self.db.pool_remove(pool['id'], ['driver_info'])
    # end  delete_pool

    def stats(self, pool_id):
        pass
    # end  stats

    def create_member(self, member):
        pass
    # end  create_member

    def update_member(self, old_member, member):
        pass
    # end  update_member

    def delete_member(self, member):
        pass
    # end  delete_member

    def create_pool_health_monitor(self, 
                                   health_monitor,
                                   pool_id):
        pass
    # end  create_pool_health_monitor

    def delete_pool_health_monitor(self, health_monitor, pool_id):
        pass
    # end  delete_pool_health_monitor

    def update_health_monitor(self, 
                              old_health_monitor,
                              health_monitor,
                              pool_id):
        pass
    # end  update_health_monitor

    def set_config_v1(self, pool_id):
        pass

    def set_config_v2(self, lb_id):
        pass
