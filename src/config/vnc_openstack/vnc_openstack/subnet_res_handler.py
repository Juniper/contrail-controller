#    Copyright
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.

import uuid

from cfgm_common import exceptions as vnc_exc
import netaddr
from vnc_api import vnc_api

import contrail_res_handler as res_handler
import neutron_plugin_db_handler as db_handler
import vn_res_handler as vn_handler

_IFACE_ROUTE_TABLE_NAME_PREFIX = 'NEUTRON_IFACE_RT'


class SubnetMixin(object):
    @staticmethod
    def get_subnet_dict(subnet_obj, vn_obj):
        pass

    @staticmethod
    def _subnet_vnc_get_key(subnet_vnc, net_id):
        pfx = subnet_vnc.subnet.get_ip_prefix()
        pfx_len = subnet_vnc.subnet.get_ip_prefix_len()

        network = netaddr.IPNetwork('%s/%s' % (pfx, pfx_len))
        return '%s %s/%s' % (net_id, str(network.ip), pfx_len)

    def _subnet_vnc_create_mapping(self, subnet_id, subnet_key):
        self._vnc_lib.kv_store(subnet_id, subnet_key)
        self._vnc_lib.kv_store(subnet_key, subnet_id)

    def _subnet_vnc_delete_mapping(self, subnet_id, subnet_key):
        self._vnc_lib.kv_delete(subnet_id)
        self._vnc_lib.kv_delete(subnet_key)

    def _subnet_vnc_read_mapping(self, id=None, key=None):
        def _subnet_id_to_key():
            all_net_objs = self._resource_list(detail=True)
            for net_obj in all_net_objs:
                ipam_refs = net_obj.get_network_ipam_refs()
                net_uuid = net_obj.uuid
                for ipam_ref in ipam_refs or []:
                    subnet_vncs = ipam_ref['attr'].get_ipam_subnets()
                    for subnet_vnc in subnet_vncs:
                        if subnet_vnc.subnet_uuid == id:
                            return self._subnet_vnc_get_key(subnet_vnc,
                                                            net_uuid)
            return None
        # _subnet_id_to_key

        if id:
            try:
                subnet_key = self._vnc_lib.kv_retrieve(id)
            except vnc_exc.NoIdError:
                # contrail UI/api might have been used to create the subnet,
                # create id to key mapping now/here.
                subnet_key = _subnet_id_to_key()
                if not subnet_key:
                    db_handler.DBInterfaceV2._raise_contrail_exception(
                        'SubnetNotFound', subnet_id=id)
                # persist to avoid this calculation later
                self._subnet_vnc_create_mapping(id, subnet_key)
            return subnet_key

        if key:
            try:
                subnet_id = self._vnc_lib.kv_retrieve(key)
            except vnc_exc.NoIdError:
                # contrail UI/api might have been used to create the subnet,
                # create key to id mapping now/here.
                subnet_vnc = self._subnet_read(key)
                subnet_id = subnet_vnc.subnet_uuid
                # persist to avoid this calculation later
                self._subnet_vnc_create_mapping(subnet_id, key)
            return subnet_id

    def get_vn_obj_for_subnet_id(self, subnet_id):
        subnet_key = self._vnc_lib.kv_retrieve(subnet_id)
        net_uuid = subnet_key.split(' ')[0]
        return self._resource_get(id=net_uuid)

    def _subnet_read(self, subnet_key=None, subnet_id=None):
        if not subnet_key:
            subnet_key = self._vnc_lib.kv_retrieve(subnet_id)

        net_uuid = subnet_key.split(' ')[0]
        try:
            vn_obj = self._resource_get(id=net_uuid)
        except vnc_exc.NoIdError:
            return None

        ipam_refs = vn_obj.get_network_ipam_refs()

        # TODO() scope for optimization
        for ipam_ref in ipam_refs or []:
            subnet_vncs = ipam_ref['attr'].get_ipam_subnets()
            for subnet_vnc in subnet_vncs:
                if self._subnet_vnc_get_key(subnet_vnc,
                                            net_uuid) == subnet_key:
                    return subnet_vnc

    def _subnet_vnc_read_or_create_mapping(self, id, key):
        # if subnet was created outside of neutron handle it and create
        # neutron representation now (lazily)
        try:
            return self._subnet_vnc_read_mapping(key=key)
        except vnc_exc.NoIdError:
            self._subnet_vnc_create_mapping(id, key)
            return self._subnet_vnc_read_mapping(key=key)

    def get_subnet_id_cidr(self, subnet_vnc, vn_obj=None):
        sn_id = None
        if vn_obj:
            subnet_key = self._subnet_vnc_get_key(subnet_vnc, vn_obj.uuid)
            sn_id = self._subnet_vnc_read_or_create_mapping(
                id=subnet_vnc.subnet_uuid, key=subnet_key)

        cidr = '%s/%s' % (subnet_vnc.subnet.get_ip_prefix(),
                          subnet_vnc.subnet.get_ip_prefix_len())
        return (sn_id, cidr)

    def _get_allocation_pools_dict(self, alloc_objs, gateway_ip, cidr):
        allocation_pools = []
        for alloc_obj in alloc_objs or []:
            first_ip = alloc_obj.get_start()
            last_ip = alloc_obj.get_end()
            alloc_dict = {'first_ip': first_ip, 'last_ip': last_ip}
            allocation_pools.append(alloc_dict)

        if not allocation_pools:
            if (int(netaddr.IPNetwork(gateway_ip).network) ==
                    int(netaddr.IPNetwork(cidr).network+1)):
                first_ip = str(netaddr.IPNetwork(cidr).network + 2)
            else:
                first_ip = str(netaddr.IPNetwork(cidr).network + 1)
            last_ip = str(netaddr.IPNetwork(cidr).broadcast - 1)
            cidr_pool = {'first_ip': first_ip, 'last_ip': last_ip}
            allocation_pools.append(cidr_pool)

        return allocation_pools

    @staticmethod
    def get_vn_subnets(vn_obj):
        """Returns a list of dicts of subnet-id:cidr of a vn."""
        ret_subnets = []

        ipam_refs = vn_obj.get_network_ipam_refs()
        for ipam_ref in ipam_refs or []:
            subnet_vncs = ipam_ref['attr'].get_ipam_subnets()
            for subnet_vnc in subnet_vncs:
                subnet_id = subnet_vnc.subnet_uuid
                cidr = '%s/%s' % (subnet_vnc.subnet.get_ip_prefix(),
                                  subnet_vnc.subnet.get_ip_prefix_len())
                ret_subnets.append({'id': subnet_id, 'cidr': cidr})

        return ret_subnets

    @staticmethod
    def _subnet_neutron_to_vnc(subnet_q):
        cidr = netaddr.IPNetwork(subnet_q['cidr'])
        pfx = str(cidr.network)
        pfx_len = int(cidr.prefixlen)
        if cidr.version != 4 and cidr.version != 6:
            db_handler.DBInterfaceV2._raise_contrail_exception(
                'BadRequest',
                resource='subnet', msg='Unknown IP family')
        elif cidr.version != int(subnet_q['ip_version']):
            msg = ("cidr '%s' does not match the ip_version '%s'"
                   % (subnet_q['cidr'], subnet_q['ip_version']))
            db_handler.DBInterfaceV2._raise_contrail_exception(
                'InvalidInput', error_message=msg)
        if 'gateway_ip' in subnet_q:
            default_gw = subnet_q['gateway_ip']
        else:
            # Assigned first+1 from cidr
            default_gw = str(netaddr.IPAddress(cidr.first + 1))

        if 'allocation_pools' in subnet_q:
            alloc_pools = subnet_q['allocation_pools']
        else:
            # Assigned by address manager
            alloc_pools = None

        dhcp_option_list = None
        if 'dns_nameservers' in subnet_q and subnet_q['dns_nameservers']:
            dhcp_options = []
            dns_servers = " ".join(subnet_q['dns_nameservers'])
            if dns_servers:
                dhcp_options.append(vnc_api.DhcpOptionType(
                    dhcp_option_name='6', dhcp_option_value=dns_servers))
            if dhcp_options:
                dhcp_option_list = vnc_api.DhcpOptionsListType(dhcp_options)

        host_route_list = None
        if 'host_routes' in subnet_q and subnet_q['host_routes']:
            host_routes = []
            for host_route in subnet_q['host_routes']:
                host_routes.append(vnc_api.RouteType(
                    prefix=host_route['destination'],
                    next_hop=host_route['nexthop']))
            if host_routes:
                host_route_list = vnc_api.RouteTableType(host_routes)

        if 'enable_dhcp' in subnet_q:
            dhcp_config = subnet_q['enable_dhcp']
        else:
            dhcp_config = None
        sn_name = subnet_q.get('name')
        subnet_vnc = vnc_api.IpamSubnetType(
            subnet=vnc_api.SubnetType(pfx, pfx_len),
            default_gateway=default_gw,
            enable_dhcp=dhcp_config,
            dns_nameservers=None,
            allocation_pools=alloc_pools,
            addr_from_start=True,
            dhcp_option_list=dhcp_option_list,
            host_routes=host_route_list,
            subnet_name=sn_name,
            subnet_uuid=str(uuid.uuid4()))

        return subnet_vnc

    def _subnet_vnc_to_neutron(self, subnet_vnc, vn_obj, ipam_fq_name):
        sn_q_dict = {}
        sn_name = subnet_vnc.get_subnet_name()
        if sn_name is not None:
            sn_q_dict['name'] = sn_name
        else:
            sn_q_dict['name'] = ''
        sn_q_dict['tenant_id'] = vn_obj.parent_uuid.replace('-', '')
        sn_q_dict['network_id'] = vn_obj.uuid
        sn_q_dict['ipv6_ra_mode'] = None
        sn_q_dict['ipv6_address_mode'] = None

        cidr = '%s/%s' % (subnet_vnc.subnet.get_ip_prefix(),
                          subnet_vnc.subnet.get_ip_prefix_len())
        sn_q_dict['cidr'] = cidr
        sn_q_dict['ip_version'] = netaddr.IPNetwork(cidr).version  # 4 or 6

        # read from useragent kv only for old subnets created
        # before schema had uuid in subnet
        sn_id = subnet_vnc.subnet_uuid
        if not sn_id:
            subnet_key = self._subnet_vnc_get_key(subnet_vnc, vn_obj.uuid)
            sn_id = self._subnet_vnc_read_or_create_mapping(
                id=subnet_vnc.subnet_uuid, key=subnet_key)

        sn_q_dict['id'] = sn_id

        sn_q_dict['gateway_ip'] = subnet_vnc.default_gateway

        sn_q_dict['allocation_pools'] = self._get_allocation_pools_dict(
            subnet_vnc.get_allocation_pools(), sn_q_dict['gateway_ip'], cidr)

        sn_q_dict['enable_dhcp'] = subnet_vnc.get_enable_dhcp()

        nameserver_dict_list = list()
        dhcp_option_list = subnet_vnc.get_dhcp_option_list()
        if dhcp_option_list:
            for dhcp_option in dhcp_option_list.dhcp_option or []:
                if dhcp_option.get_dhcp_option_name() == '6':
                    dns_servers = dhcp_option.get_dhcp_option_value().split()
                    for dns_server in dns_servers:
                        nameserver_entry = {'address': dns_server,
                                            'subnet_id': sn_id}
                        nameserver_dict_list.append(nameserver_entry)
        sn_q_dict['dns_nameservers'] = nameserver_dict_list

        host_route_dict_list = list()
        host_routes = subnet_vnc.get_host_routes()
        if host_routes:
            for host_route in host_routes.route or []:
                host_route_entry = {'destination': host_route.get_prefix(),
                                    'nexthop': host_route.get_next_hop(),
                                    'subnet_id': sn_id}
                host_route_dict_list.append(host_route_entry)
        sn_q_dict['routes'] = host_route_dict_list

        if vn_obj.is_shared:
            sn_q_dict['shared'] = True
        else:
            sn_q_dict['shared'] = False

        return sn_q_dict


class SubnetCreateHandler(res_handler.ResourceCreateHandler, SubnetMixin):

    def _get_netipam_obj(self, ipam_fq_name=None, vn_obj=None):
        if ipam_fq_name:
            domain_name, project_name, ipam_name = ipam_fq_name

            domain_obj = vnc_api.Domain(domain_name)
            project_obj = vnc_api.Project(project_name, domain_obj)
            netipam_obj = vnc_api.NetworkIpam(ipam_name, project_obj)
            return netipam_obj

        if vn_obj:
            try:
                ipam_fq_name = vn_obj.get_fq_name()[:-1]
                ipam_fq_name.append('default-network-ipam')
                netipam_obj = self._vnc_lib.network_ipam_read(
                    fq_name=ipam_fq_name)
            except vnc_exc.NoIdError:
                netipam_obj = vnc_api.NetworkIpam()
            return netipam_obj

    def resource_create(self, subnet_q):
        net_id = subnet_q['network_id']
        vn_obj = self._resource_get(id=net_id)
        ipam_fq_name = subnet_q.get('contrail:ipam_fq_name')
        netipam_obj = self._get_netipam_obj(ipam_fq_name,
                                            vn_obj)
        if not ipam_fq_name:
            ipam_fq_name = netipam_obj.get_fq_name()

        subnet_vnc = self._subnet_neutron_to_vnc(subnet_q)
        subnet_key = self._subnet_vnc_get_key(subnet_vnc, net_id)

        # Locate list of subnets to which this subnet has to be appended
        net_ipam_ref = None
        ipam_refs = vn_obj.get_network_ipam_refs()
        for ipam_ref in ipam_refs or []:
            if ipam_ref['to'] == ipam_fq_name:
                net_ipam_ref = ipam_ref
                break

        if not net_ipam_ref:
            # First link from net to this ipam
            vnsn_data = vnc_api.VnSubnetsType([subnet_vnc])
            vn_obj.add_network_ipam(netipam_obj, vnsn_data)
        else:  # virtual-network already linked to this ipam
            for subnet in net_ipam_ref['attr'].get_ipam_subnets():
                if subnet_key == self._subnet_vnc_get_key(subnet, net_id):
                    existing_sn_id = self._subnet_vnc_read_mapping(
                        key=subnet_key)
                    # duplicate !!
                    msg = ("Cidr %s overlaps with another subnet of subnet %s"
                           ) % (subnet_q['cidr'], existing_sn_id)
                    db_handler.DBInterfaceV2._raise_contrail_exception(
                        'BadRequest', resource='subnet', msg=msg)
            vnsn_data = net_ipam_ref['attr']
            vnsn_data.ipam_subnets.append(subnet_vnc)
            # TODO(): Add 'ref_update' API that will set this field
            vn_obj._pending_field_updates.add('network_ipam_refs')
        self._resource_update(vn_obj)

        # allocate an id to the subnet and store mapping with
        # api-server
        subnet_id = subnet_vnc.subnet_uuid
        self._subnet_vnc_create_mapping(subnet_id, subnet_key)

        # Read in subnet from server to get updated values for gw etc.
        subnet_vnc = self._subnet_read(subnet_key)
        subnet_info = self._subnet_vnc_to_neutron(subnet_vnc, vn_obj,
                                                  ipam_fq_name)

        return subnet_info


class SubnetDeleteHandler(res_handler.ResourceDeleteHandler, SubnetMixin):

    def resource_delete(self, subnet_id):
        subnet_key = self._subnet_vnc_read_mapping(id=subnet_id)
        net_id = subnet_key.split()[0]

        vn_obj = self._resource_get(id=net_id)
        ipam_refs = vn_obj.get_network_ipam_refs()
        for ipam_ref in ipam_refs or []:
            orig_subnets = ipam_ref['attr'].get_ipam_subnets()
            new_subnets = [subnet_vnc for subnet_vnc in orig_subnets
                           if self._subnet_vnc_get_key(
                               subnet_vnc, net_id) != subnet_key]
            if len(orig_subnets) != len(new_subnets):
                # matched subnet to be deleted
                ipam_ref['attr'].set_ipam_subnets(new_subnets)
                vn_obj._pending_field_updates.add('network_ipam_refs')
                try:
                    self._resource_update(vn_obj)
                except vnc_exc.RefsExistError:
                    db_handler.DBInterfaceV2._raise_contrail_exception(
                        'SubnetInUse', subnet_id=subnet_id)
                self._subnet_vnc_delete_mapping(subnet_id, subnet_key)


class SubnetGetHandler(res_handler.ResourceGetHandler, SubnetMixin):
    resource_list_method = 'virtual_networks_list'
    resource_get_method = 'virtual_network_read'

    def resource_get(self, subnet_id):
        subnet_key = self._subnet_vnc_read_mapping(id=subnet_id)
        net_id = subnet_key.split()[0]

        try:
            vn_obj = self._resource_get(id=net_id)
        except vnc_exc.NoIdError:
            db_handler.DBInterfaceV2._raise_contrail_exception(
                'SubnetNotFound', subnet_id=subnet_id)

        ipam_refs = vn_obj.get_network_ipam_refs()
        for ipam_ref in ipam_refs or []:
            subnet_vncs = ipam_ref['attr'].get_ipam_subnets()
            for subnet_vnc in subnet_vncs:
                if self._subnet_vnc_get_key(subnet_vnc, net_id) == subnet_key:
                    ret_subnet_q = self._subnet_vnc_to_neutron(
                        subnet_vnc, vn_obj, ipam_ref['to'])
                    return ret_subnet_q

        return {}

    def resource_count(self, **kwargs):
        subnets_info = self.resource_list(**kwargs)
        return len(subnets_info)

    def _get_subnet_list_after_apply_filter_(self, vn_list, filters):
        ret_subnets = []
        ret_dict = {}
        for vn_obj in vn_list:
            if vn_obj.uuid in ret_dict:
                continue
            ret_dict[vn_obj.uuid] = 1

            ipam_refs = vn_obj.get_network_ipam_refs()
            for ipam_ref in ipam_refs or []:
                subnet_vncs = ipam_ref['attr'].get_ipam_subnets()
                for subnet_vnc in subnet_vncs:
                    sn_info = self._subnet_vnc_to_neutron(subnet_vnc,
                                                          vn_obj,
                                                          ipam_ref['to'])
                    sn_id = sn_info['id']
                    sn_proj_id = sn_info['tenant_id']
                    sn_net_id = sn_info['network_id']
                    sn_name = sn_info['name']

                    if (filters and 'shared' in filters and
                            filters['shared'][0]):
                        if not vn_obj.is_shared:
                            continue
                    elif filters:
                        if not self._filters_is_present(filters, 'id',
                                                        sn_id):
                            continue
                        if not self._filters_is_present(filters,
                                                        'tenant_id',
                                                        sn_proj_id):
                            continue
                        if not self._filters_is_present(filters,
                                                        'network_id',
                                                        sn_net_id):
                            continue
                        if not self._filters_is_present(filters,
                                                        'name',
                                                        sn_name):
                            continue

                    ret_subnets.append(sn_info)

        return ret_subnets

    def resource_list(self, **kwargs):
        filters = kwargs.get('filters')
        context = kwargs.get('context')
        vn_get_handler = vn_handler.VNetworkGetHandler(self._vnc_lib)
        all_vn_objs = []
        if filters and 'id' in filters:
            # required subnets are specified,
            # just read in corresponding net_ids
            net_ids = []
            for subnet_id in filters['id']:
                subnet_key = self._subnet_vnc_read_mapping(id=subnet_id)
                net_id = subnet_key.split()[0]
                net_ids.append(net_id)

            all_vn_objs.extend(vn_get_handler._resource_list(obj_uuids=net_ids,
                                                             detail=True))
        else:
            if not context['is_admin']:
                proj_id = context['tenant']
            else:
                proj_id = None
            vn_objs = vn_get_handler.get_vn_list_project(proj_id)
            all_vn_objs.extend(vn_objs)
            vn_objs = vn_get_handler.vn_list_shared()
            all_vn_objs.extend(vn_objs)

        return self._get_subnet_list_after_apply_filter_(all_vn_objs, filters)


class SubnetUpdateHandler(res_handler.ResourceUpdateHandler, SubnetMixin):
    resource_update_method = 'virtual_network_update'

    def _subnet_update(self, subnet_q, subnet_id, vn_obj, subnet_vnc,
                       ipam_ref, apply_subnet_host_routes=False):
        if subnet_q.get('name') is not None:
            subnet_vnc.set_subnet_name(subnet_q['name'])

        if subnet_q.get('gateway_ip') is not None:
            subnet_vnc.set_default_gateway(subnet_q['gateway_ip'])

        if subnet_q.get('enable_dhcp') is not None:
            subnet_vnc.set_enable_dhcp(subnet_q['enable_dhcp'])

        if subnet_q.get('dns_nameservers') is not None:
            dhcp_options = []
            dns_servers = " ".join(subnet_q['dns_nameservers'])
            if dns_servers:
                dhcp_options.append(vnc_api.DhcpOptionType(
                    dhcp_option_name='6', dhcp_option_value=dns_servers))
            if dhcp_options:
                subnet_vnc.set_dhcp_option_list(vnc_api.DhcpOptionsListType(
                    dhcp_options))
            else:
                subnet_vnc.set_dhcp_option_list(None)

        if subnet_q.get('host_routes') is not None:
            host_routes = []
            for host_route in subnet_q['host_routes']:
                host_routes.append(vnc_api.RouteType(
                    prefix=host_route['destination'],
                    next_hop=host_route['nexthop']))
            if apply_subnet_host_routes:
                old_host_routes = subnet_vnc.get_host_routes()
                subnet_cidr = '%s/%s' % (subnet_vnc.subnet.get_ip_prefix(),
                                         subnet_vnc.subnet.get_ip_prefix_len())
                subnet_host_handler = SubnetHostRoutesHandler(self._vnc_lib)
                subnet_host_handler.port_update_iface_route_table(
                    vn_obj, subnet_cidr, subnet_id, host_routes,
                    old_host_routes)
            if host_routes:
                subnet_vnc.set_host_routes(vnc_api.RouteTableType(host_routes))
            else:
                subnet_vnc.set_host_routes(None)

        vn_obj._pending_field_updates.add('network_ipam_refs')
        self._resource_update(vn_obj)
        ret_subnet_q = self._subnet_vnc_to_neutron(
            subnet_vnc, vn_obj, ipam_ref['to'])

        return ret_subnet_q

    def resource_update(self, subnet_id, subnet_q,
                        apply_subnet_host_routes=False):
        if 'gateway_ip' in subnet_q:
            if subnet_q['gateway_ip'] is not None:
                self._raise_contrail_exception(
                    'BadRequest', resource='subnet',
                    msg="update of gateway is not supported")

        if 'allocation_pools' in subnet_q:
            if subnet_q['allocation_pools'] is not None:
                db_handler.DBInterfaceV2._raise_contrail_exception(
                    'BadRequest', resource='subnet',
                    msg="update of allocation_pools is not allowed")

        subnet_key = self._subnet_vnc_read_mapping(id=subnet_id)
        net_id = subnet_key.split()[0]
        vn_obj = self._resource_get(id=net_id)
        ipam_refs = vn_obj.get_network_ipam_refs()
        for ipam_ref in ipam_refs or []:
            subnets = ipam_ref['attr'].get_ipam_subnets()
            for subnet_vnc in subnets:
                if self._subnet_vnc_get_key(
                        subnet_vnc,
                        net_id) == subnet_key:
                    return self._subnet_update(
                        subnet_q, subnet_id, vn_obj, subnet_vnc, ipam_ref,
                        apply_subnet_host_routes=apply_subnet_host_routes)

        return {}


class SubnetHostRoutesHandler(res_handler.ContrailResourceHandler,
                              SubnetMixin):

    @staticmethod
    def _port_get_host_prefixes(host_routes, subnet_cidr):
        """This function returns the host prefixes.

        Eg. If host_routes have the below routes
           ---------------------------
           |destination   | next hop  |
           ---------------------------
           |  10.0.0.0/24 | 8.0.0.2   |
           |  12.0.0.0/24 | 10.0.0.4  |
           |  14.0.0.0/24 | 12.0.0.23 |
           |  16.0.0.0/24 | 8.0.0.4   |
           |  15.0.0.0/24 | 16.0.0.2  |
           |  20.0.0.0/24 | 8.0.0.12  |
           ---------------------------
           subnet_cidr is 8.0.0.0/24

           This function returns the dictionary
           '8.0.0.2' : ['10.0.0.0/24', '12.0.0.0/24', '14.0.0.0/24']
           '8.0.0.4' : ['16.0.0.0/24', '15.0.0.0/24']
           '8.0.0.12': ['20.0.0.0/24']
        """
        temp_host_routes = list(host_routes)
        cidr_ip_set = netaddr.IPSet([subnet_cidr])
        host_route_dict = {}
        for route in temp_host_routes[:]:
            next_hop = route.get_next_hop()
            if netaddr.IPAddress(next_hop) in cidr_ip_set:
                if next_hop in host_route_dict:
                    host_route_dict[next_hop].append(route.get_prefix())
                else:
                    host_route_dict[next_hop] = [route.get_prefix()]
                temp_host_routes.remove(route)

        # look for indirect routes
        if temp_host_routes:
            for ipaddr in host_route_dict:
                SubnetHostRoutesHandler._port_update_prefixes(
                    host_route_dict[ipaddr], temp_host_routes)
        return host_route_dict

    @staticmethod
    def _port_update_prefixes(matched_route_list, unmatched_host_routes):
        process_host_routes = True
        while process_host_routes:
            process_host_routes = False
            for route in unmatched_host_routes:
                ip_addr = netaddr.IPAddress(route.get_next_hop())
                if ip_addr in netaddr.IPSet(matched_route_list):
                    matched_route_list.append(route.get_prefix())
                    unmatched_host_routes.remove(route)
                    process_host_routes = True

    def _port_add_iface_route_table(self, route_prefix_list, vmi_obj,
                                    subnet_id):
        project_obj = self._project_read(proj_id=vmi_obj.parent_uuid)
        intf_rt_name = '%s_%s_%s' % (_IFACE_ROUTE_TABLE_NAME_PREFIX,
                                     subnet_id, vmi_obj.uuid)
        intf_rt_fq_name = list(project_obj.get_fq_name())
        intf_rt_fq_name.append(intf_rt_name)

        try:
            intf_route_table_obj = self._vnc_lib.interface_route_table_read(
                fq_name=intf_rt_fq_name)
        except vnc_exc.NoIdError:
            route_table = vnc_api.RouteTableType(intf_rt_name)
            route_table.set_route([])
            intf_route_table = vnc_api.InterfaceRouteTable(
                interface_route_table_routes=route_table,
                parent_obj=project_obj,
                name=intf_rt_name)

            intf_route_table_id = self._vnc_lib.interface_route_table_create(
                intf_route_table)
            intf_route_table_obj = self._vnc_lib.interface_route_table_read(
                id=intf_route_table_id)

        rt_routes = intf_route_table_obj.get_interface_route_table_routes()
        routes = rt_routes.get_route()
        # delete any old routes
        routes = []
        for prefix in route_prefix_list:
            routes.append(vnc_api.RouteType(prefix=prefix))
        rt_routes.set_route(routes)
        intf_route_table_obj.set_interface_route_table_routes(rt_routes)
        self._vnc_lib.interface_route_table_update(intf_route_table_obj)
        vmi_obj.add_interface_route_table(intf_route_table_obj)
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)

    def port_check_and_add_iface_route_table(self, fixed_ips, vn_obj,
                                             vmi_obj):
        ipam_refs = vn_obj.get_network_ipam_refs()
        if not ipam_refs:
            return

        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets()
            for subnet in subnets:
                host_routes = subnet.get_host_routes()
                if host_routes is None:
                    continue
                subnet_key = self._subnet_vnc_get_key(subnet, vn_obj.uuid)
                sn_id = self._subnet_vnc_read_mapping(key=subnet_key)
                subnet_cidr = '%s/%s' % (subnet.subnet.get_ip_prefix(),
                                         subnet.subnet.get_ip_prefix_len())

                for ip_addr in ([fixed_ip['ip_address'] for fixed_ip in
                                fixed_ips if fixed_ip['subnet_id'] == sn_id]):
                    host_prefixes = self._port_get_host_prefixes(
                        host_routes.route, subnet_cidr)
                    if ip_addr in host_prefixes:
                        self._port_add_iface_route_table(
                            host_prefixes[ip_addr], vmi_obj, sn_id)

    def port_update_iface_route_table(self, vn_obj, subnet_cidr, subnet_id,
                                      new_host_routes, old_host_routes=None):
        old_host_prefixes = {}
        if old_host_routes:
            old_host_prefixes = self._port_get_host_prefixes(
                old_host_routes.route, subnet_cidr)
        new_host_prefixes = self._port_get_host_prefixes(new_host_routes,
                                                         subnet_cidr)

        for ipaddr, prefixes in old_host_prefixes.items():
            if ipaddr in new_host_prefixes:
                need_update = False
                if len(prefixes) == len(new_host_prefixes[ipaddr]):
                    for prefix in prefixes:
                        if prefix not in new_host_prefixes[ipaddr]:
                            need_update = True
                            break
                else:
                    need_update = True
                if need_update:
                    old_host_prefixes.pop(ipaddr)
                else:
                    # both the old and new are same. No need to do
                    # anything
                    old_host_prefixes.pop(ipaddr)
                    new_host_prefixes.pop(ipaddr)

        if not new_host_prefixes and not old_host_prefixes:
            # nothing to be done as old_host_routes and
            # new_host_routes match exactly
            return

        # get the list of all the ip objs for this network
        ipobjs = self._vnc_lib.instance_ips_list(
            detail=True, back_ref_id=[vn_obj.uuid])
        back_ref_fields = ['logical_router_back_refs', 'instance_ip_back_refs',
                           'floating_ip_back_refs']
        for ipobj in ipobjs:
            ipaddr = ipobj.get_instance_ip_address()
            if ipaddr in old_host_prefixes:
                self._port_remove_iface_route_table(ipobj, subnet_id)
                continue

            if ipaddr in new_host_prefixes:
                port_back_refs = ipobj.get_virtual_machine_interface_refs()
                for port_ref in port_back_refs:
                    vmi_obj = self._vnc_lib.virtual_machine_interface_read(
                        id=port_ref['uuid'], fields=back_ref_fields)
                    self._port_add_iface_route_table(new_host_prefixes[ipaddr],
                                                     vmi_obj, subnet_id)

    def _port_remove_iface_route_table(self, ipobj, subnet_id):
        port_refs = ipobj.get_virtual_machine_interface_refs()
        back_ref_fields = ['logical_router_back_refs', 'instance_ip_back_refs',
                           'floating_ip_back_refs']
        for port_ref in port_refs or []:
            vmi_obj = self._vnc_lib.virtual_machine_interface_read(
                id=port_ref['uuid'], fields=back_ref_fields)
            intf_rt_name = '%s_%s_%s' % (_IFACE_ROUTE_TABLE_NAME_PREFIX,
                                         subnet_id, vmi_obj.uuid)
            for rt_ref in vmi_obj.get_interface_route_table_refs() or []:
                if rt_ref['to'][2] != intf_rt_name:
                    continue
                try:
                    intf_route_table_obj = (
                        self._vnc_lib.interface_route_table_read(
                            id=rt_ref['uuid']))
                    vmi_obj.del_interface_route_table(intf_route_table_obj)
                    self._vnc_lib.virtual_machine_interface_update(vmi_obj)
                    self._vnc_lib.interface_route_table_delete(
                        id=rt_ref['uuid'])
                except vnc_exc.NoIdError:
                    pass


class SubnetHandler(SubnetGetHandler,
                    SubnetCreateHandler,
                    SubnetDeleteHandler,
                    SubnetUpdateHandler):
    pass
