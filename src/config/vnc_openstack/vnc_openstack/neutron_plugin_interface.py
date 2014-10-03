#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import bottle
import cgitb
import gevent
import json
import logging
from pprint import pformat
import requests
import sys
import string
import ConfigParser

from vnc_api import vnc_api
from neutron_plugin_db import DBInterface


LOG = logging.getLogger(__name__)

@bottle.error(400)
def error_400(err):
    return err.body

@bottle.error(404)
def error_404(err):
    return err.body

@bottle.error(409)
def error_409(err):
    return err.body


class NeutronPluginInterface(object):
    """
    An instance of this class receives requests from Contrail Neutron Plugin
    """

    def __init__(self, api_server_ip, api_server_port, conf_sections):
        self._vnc_api_ip = api_server_ip
        self._vnc_api_port = api_server_port
        self._config_sections = conf_sections
        self._auth_user = conf_sections.get('KEYSTONE', 'admin_user')
        self._auth_passwd = conf_sections.get('KEYSTONE', 'admin_password')
        self._auth_tenant = conf_sections.get('KEYSTONE', 'admin_tenant_name')

        try:
            exts_enabled = conf_sections.getboolean('NEUTRON',
                'contrail_extensions_enabled')
        except (ConfigParser.NoSectionError, ConfigParser.NoOptionError):
            exts_enabled = True
        self._contrail_extensions_enabled = exts_enabled

        try:
            self._multi_tenancy = conf_sections.get('DEFAULTS', 'multi_tenancy')
        except ConfigParser.NoOptionError:
            self._multi_tenancy = False
        try:
            self._list_optimization_enabled = \
                conf_sections.get('DEFAULTS', 'list_optimization_enabled')
        except ConfigParser.NoOptionError:
            self._list_optimization_enabled = False

        try:
            self._sn_host_route = conf_sections.get('DEFAULTS',
                                                    'apply_subnet_host_routes')
        except ConfigParser.NoOptionError:
            self._sn_host_route = False

        self._cfgdb = None
        self._cfgdb_map = {}

    def _connect_to_db(self):
        """
        Many instantiations of plugin (base + extensions) but need to have
        only one config db conn (else error from ifmap-server)
        """
        if self._cfgdb is None:
            # Initialize connection to DB and add default entries
            exts_enabled = self._contrail_extensions_enabled
            apply_sn_route = self._sn_host_route
            self._cfgdb = DBInterface(self._auth_user,
                                      self._auth_passwd,
                                      self._auth_tenant,
                                      self._vnc_api_ip,
                                      self._vnc_api_port,
                                      contrail_extensions_enabled=exts_enabled,
                                      list_optimization_enabled=\
                                      self._list_optimization_enabled,
                                      apply_subnet_host_routes=apply_sn_route)
            self._cfgdb.manager = self
    #end _connect_to_db

    def _get_user_cfgdb(self, context):

        self._connect_to_db()

        if not self._multi_tenancy:
            return self._cfgdb
        user_id = context['user_id']
        role = string.join(context['roles'], ",")
        if not user_id in self._cfgdb_map:
            self._cfgdb_map[user_id] = DBInterface(
                self._auth_user, self._auth_passwd, self._auth_tenant,
                self._vnc_api_ip, self._vnc_api_port,
                user_info={'user_id': user_id, 'role': role},
                list_optimization_enabled=self._list_optimization_enabled,
                apply_subnet_host_routes=self._sn_host_route)
            self._cfgdb_map[user_id].manager = self

        return self._cfgdb_map[user_id]

    def _get_requests_data(self):
        ctype = bottle.request.headers['content-type']
        try:
            if 'application/json' in ctype:
                req = bottle.request.json
                return req['context'], req['data']
        except Exception as e:
            bottle.abort(400, 'Unable to parse request data')

    # Network API Handling
    def plugin_get_network(self, context, network):
        """
        Network get request
        """

        fields = network['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            net_info = cfgdb.network_read(network['id'], fields)
            return net_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_create_network(self, context, network):
        """
        Network create request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            net_info = cfgdb.network_create(network['resource'])
            return net_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_update_network(self, context, network):
        """
        Network update request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            net_info = cfgdb.network_update(network['id'],
                                            network['resource'])
            return net_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_delete_network(self, context, network):
        """
        Network delete request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            cfgdb.network_delete(network['id'])
            LOG.debug("plugin_delete_network(): " + pformat(network['id']))
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_networks(self, context, network):
        """
        Networks get request
        """

        filters = network['filters']
        fields = network['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            nets_info = cfgdb.network_list(context, filters)
            return json.dumps(nets_info)
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_networks_count(self, context, network):
        """
        Networks count request
        """

        filters = network['filters']

        try:
            cfgdb = self._get_user_cfgdb(context)
            nets_count = cfgdb.network_count(filters)
            LOG.debug("plugin_get_networks_count(): filters: "
                      + pformat(filters) + " data: " + str(nets_count))
            return {'count': nets_count}
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_http_post_network(self):
        """
        Bottle callback for Network POST
        """
        context, network = self._get_requests_data()

        if context['operation'] == 'READ':
            return self.plugin_get_network(context, network)
        elif context['operation'] == 'CREATE':
            return self.plugin_create_network(context, network)
        elif context['operation'] == 'UPDATE':
            return self.plugin_update_network(context, network)
        elif context['operation'] == 'DELETE':
            return self.plugin_delete_network(context, network)
        elif context['operation'] == 'READALL':
            return self.plugin_get_networks(context, network)
        elif context['operation'] == 'READCOUNT':
            return self.plugin_get_networks_count(context, network)

    def _make_subnet_dict(self, subnet):
        res = {'id': subnet['id'],
               'name': subnet['name'],
               'tenant_id': subnet['tenant_id'],
               'network_id': subnet['network_id'],
               'ip_version': subnet['ip_version'],
               'cidr': subnet['cidr'],
               'allocation_pools': [{'start': pool['first_ip'],
                                     'end': pool['last_ip']}
                                    for pool in subnet['allocation_pools']],
               'gateway_ip': subnet['gateway_ip'],
               'enable_dhcp': subnet['enable_dhcp'],
               'ipv6_ra_mode': subnet['ipv6_ra_mode'],
               'ipv6_address_mode': subnet['ipv6_address_mode'],
               'dns_nameservers': [dns['address']
                                   for dns in subnet['dns_nameservers']],
               'host_routes': [{'destination': route['destination'],
                                'nexthop': route['nexthop']}
                               for route in subnet['routes']],
               'shared': subnet['shared']
               }
        return res

    # Subnet API Handling
    def plugin_get_subnet(self, context, subnet):
        """
        Subnet get request
        """

        fields = subnet['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            subnet_info = cfgdb.subnet_read(subnet['id'])
            return self._make_subnet_dict(subnet_info)
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_create_subnet(self, context, subnet):
        """
        Subnet create request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            subnet_info = cfgdb.subnet_create(subnet['resource'])
            return self._make_subnet_dict(subnet_info)
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_update_subnet(self, context, subnet):
        """
        Subnet update request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            subnet_info = cfgdb.subnet_update(subnet['id'],
                                              subnet['resource'])
            return self._make_subnet_dict(subnet_info)
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_delete_subnet(self, context, subnet):
        """
        Subnet delete request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            cfgdb.subnet_delete(subnet['id'])
            LOG.debug("plugin_delete_subnet(): " + pformat(subnet['id']))
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_subnets(self, context, subnet):
        """
        Subnets get request
        """

        filters = subnet['filters']
        fields = subnet['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            subnets_info = cfgdb.subnets_list(context, filters)
            return json.dumps([self._make_subnet_dict(i) for i in subnets_info])
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_subnets_count(self, context, subnet):
        """
        Subnets count request
        """

        filters = subnet['filters']

        try:
            cfgdb = self._get_user_cfgdb(context)
            subnets_count = cfgdb.subnets_count(context, filters)
            LOG.debug("plugin_get_subnets_count(): filters: "
                      + pformat(filters) + " data: " + str(subnets_count))
            return {'count': subnets_count}
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_http_post_subnet(self):
        """
        Bottle callback for Subnet POST
        """
        context, subnet = self._get_requests_data()

        if context['operation'] == 'READ':
            return self.plugin_get_subnet(context, subnet)
        elif context['operation'] == 'CREATE':
            return self.plugin_create_subnet(context, subnet)
        elif context['operation'] == 'UPDATE':
            return self.plugin_update_subnet(context, subnet)
        elif context['operation'] == 'DELETE':
            return self.plugin_delete_subnet(context, subnet)
        elif context['operation'] == 'READALL':
            return self.plugin_get_subnets(context, subnet)
        elif context['operation'] == 'READCOUNT':
            return self.plugin_get_subnets_count(context, subnet)

    # Port API Handling
    def plugin_get_port(self, context, port):
        """
        Port get request
        """

        fields = port['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            port_info = cfgdb.port_read(port['id'])
            return port_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_create_port(self, context, port):
        """
        Port create request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            net_info = cfgdb.port_create(context, port['resource'])
            return net_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_update_port(self, context, port):
        """
        Port update request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            net_info = cfgdb.port_update(port['id'],
                                         port['resource'])
            return net_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_delete_port(self, context, port):
        """
        Port delete request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            cfgdb.port_delete(port['id'])
            LOG.debug("plugin_delete_port(): " + pformat(port['id']))
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_ports(self, context, port):
        """
        Ports get request
        """

        filters = port['filters']
        fields = port['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            ports_info = cfgdb.port_list(context, filters)
            return json.dumps(ports_info)
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_ports_count(self, context, port):
        """
        Ports count request
        """

        filters = port['filters']

        try:
            cfgdb = self._get_user_cfgdb(context)
            ports_count = cfgdb.port_count(filters)
            LOG.debug("plugin_get_ports_count(): filters: "
                      + pformat(filters) + " data: " + str(ports_count))
            return {'count': ports_count}
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_http_post_port(self):
        """
        Bottle callback for Port POST
        """
        context, port = self._get_requests_data()

        if context['operation'] == 'READ':
            return self.plugin_get_port(context, port)
        elif context['operation'] == 'CREATE':
            return self.plugin_create_port(context, port)
        elif context['operation'] == 'UPDATE':
            return self.plugin_update_port(context, port)
        elif context['operation'] == 'DELETE':
            return self.plugin_delete_port(context, port)
        elif context['operation'] == 'READALL':
            return self.plugin_get_ports(context, port)
        elif context['operation'] == 'READCOUNT':
            return self.plugin_get_ports_count(context, port)

    # Floating IP API Handling
    def plugin_get_floatingip(self, context, floatingip):
        """
        Floating IP get request
        """

        fields = floatingip['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            fip_info = cfgdb.floatingip_read(floatingip['id'])
            return fip_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_create_floatingip(self, context, floatingip):
        """
        Floating IP create request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            net_info = cfgdb.floatingip_create(context, floatingip['resource'])
            return net_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_update_floatingip(self, context, floatingip):
        """
        Floating IP update request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            floatingip_info = cfgdb.floatingip_update(context, floatingip['id'],
                                                      floatingip['resource'])
            return floatingip_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_delete_floatingip(self, context, floatingip):
        """
        Floating IP delete request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            cfgdb.floatingip_delete(floatingip['id'])
            LOG.debug("plugin_delete_floatingip(): " + 
                pformat(floatingip['id']))
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_floatingips(self, context, floatingip):
        """
        Floating IPs get request
        """

        filters = floatingip['filters']
        fields = floatingip['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            floatingips_info = cfgdb.floatingip_list(context, filters)
            return json.dumps(floatingips_info)
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_floatingips_count(self, context, floatingip):
        """
        Floating IPs count request
        """

        filters = floatingip['filters']

        try:
            cfgdb = self._get_user_cfgdb(context)
            floatingips_count = cfgdb.floatingip_count(context, filters)
            LOG.debug("plugin_get_floatingips_count(): filters: "
                      + pformat(filters) + " data: " + str(floatingips_count))
            return {'count': floatingips_count}
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_http_post_floatingip(self):
        """
        Bottle callback for Floating IP POST
        """
        context, floatingip = self._get_requests_data()

        if context['operation'] == 'READ':
            return self.plugin_get_floatingip(context, floatingip)
        elif context['operation'] == 'CREATE':
            return self.plugin_create_floatingip(context, floatingip)
        elif context['operation'] == 'UPDATE':
            return self.plugin_update_floatingip(context, floatingip)
        elif context['operation'] == 'DELETE':
            return self.plugin_delete_floatingip(context, floatingip)
        elif context['operation'] == 'READALL':
            return self.plugin_get_floatingips(context, floatingip)
        elif context['operation'] == 'READCOUNT':
            return self.plugin_get_floatingips_count(context, floatingip)

    # Security Group API Handling
    def plugin_get_sec_group(self, context, sg):
        """
        Security group get request
        """

        fields = sg['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            sg_info = cfgdb.security_group_read(sg['id'])
            return sg_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_create_sec_group(self, context, sg):
        """
        Security group create request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            sg_info = cfgdb.security_group_create(sg['resource'])
            return sg_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_update_sec_group(self, context, sg):
        """
        Security group update request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            sg_info = cfgdb.security_group_update(sg['id'],
                                                  sg['resource'])
            return sg_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_delete_sec_group(self, context, sg):
        """
        Security group delete request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            cfgdb.security_group_delete(sg['id'])
            LOG.debug("plugin_delete_sec_group(): " + pformat(sg['id']))
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_sec_groups(self, context, sg):
        """
        Security groups get request
        """

        filters = sg['filters']
        fields = sg['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            sgs_info = cfgdb.security_group_list(context, filters)
            return json.dumps(sgs_info)
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_http_post_securitygroup(self):
        """
        Bottle callback for Security Group POST
        """
        context, sg = self._get_requests_data()

        if context['operation'] == 'READ':
            return self.plugin_get_sec_group(context, sg)
        elif context['operation'] == 'CREATE':
            return self.plugin_create_sec_group(context, sg)
        elif context['operation'] == 'UPDATE':
            return self.plugin_update_sec_group(context, sg)
        elif context['operation'] == 'DELETE':
            return self.plugin_delete_sec_group(context, sg)
        elif context['operation'] == 'READALL':
            return self.plugin_get_sec_groups(context, sg)

    # Floating IP API Handling
    def plugin_get_sec_group_rule(self, context, sg_rule):
        """
        Security group rule get request
        """

        fields = sg_rule['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            sg_rule_info = cfgdb.security_group_rule_read(sg_rule['id'])
            return sg_rule_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_create_sec_group_rule(self, context, sg_rule):
        """
        Security group rule create request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            sg_rule_info = cfgdb.security_group_rule_create(sg_rule['resource'])
            return sg_rule_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_delete_sec_group_rule(self, context, sg_rule):
        """
        Security group rule delete request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            cfgdb.security_group_rule_delete(sg_rule['id'])
            LOG.debug("plugin_delete_sec_group_rule(): " + 
                pformat(sg_rule['id']))
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_sec_group_rules(self, context, sg_rule):
        """
        Security group rules get request
        """

        filters = sg_rule['filters']
        fields = sg_rule['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            sg_rules_info = cfgdb.security_group_rule_list(context, filters)
            return json.dumps(sg_rules_info)
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_http_post_securitygrouprule(self):
        """
        Bottle callback for sec_group_rule POST
        """
        context, sg_rule = self._get_requests_data()

        if context['operation'] == 'READ':
            return self.plugin_get_sec_group_rule(context, sg_rule)
        elif context['operation'] == 'CREATE':
            return self.plugin_create_sec_group_rule(context, sg_rule)
        elif context['operation'] == 'UPDATE':
            return self.plugin_update_sec_group_rule(context, sg_rule)
        elif context['operation'] == 'DELETE':
            return self.plugin_delete_sec_group_rule(context, sg_rule)
        elif context['operation'] == 'READALL':
            return self.plugin_get_sec_group_rules(context, sg_rule)
        elif context['operation'] == 'READCOUNT':
            return self.plugin_get_sec_group_rules_count(context, sg_rule)

    # Router IP API Handling
    def plugin_get_router(self, context, router):
        """
        Router get request
        """

        fields = router['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            router_info = cfgdb.router_read(router['id'])
            return router_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_create_router(self, context, router):
        """
        Router create request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            router_info = cfgdb.router_create(router['resource'])
            return router_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_update_router(self, context, router):
        """
        Router update request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            router_info = cfgdb.router_update(router['id'],
                                              router['resource'])
            return router_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_delete_router(self, context, router):
        """
        Router delete request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            cfgdb.router_delete(router['id'])
            LOG.debug("plugin_delete_router(): " + 
                pformat(router['id']))
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_routers(self, context, router):
        """
        Routers get request
        """

        filters = router['filters']
        fields = router['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            routers_info = cfgdb.router_list(context, filters)
            return json.dumps(routers_info)
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_routers_count(self, context, router):
        """
        Routers count request
        """

        filters = router['filters']

        try:
            cfgdb = self._get_user_cfgdb(context)
            routers_count = cfgdb.router_count(filters)
            LOG.debug("plugin_get_routers_count(): filters: "
                      + pformat(filters) + " data: " + str(routers_count))
            return {'count': routers_count}
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_add_router_interface(self, context, interface_info):
        """
        Add interface to a router
        """
        try:
            cfgdb = self._get_user_cfgdb(context)
            router_id = interface_info['id']
            if 'port_id' in interface_info['resource']:
                port_id = interface_info['resource']['port_id']
                return cfgdb.add_router_interface(context, router_id, port_id=port_id)
            elif 'subnet_id' in interface_info['resource']:
                subnet_id = interface_info['resource']['subnet_id']
                return cfgdb.add_router_interface(context, router_id,
                                                  subnet_id=subnet_id)
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_del_router_interface(self, context, interface_info):
        """
        Delete interface from a router
        """
        try:
            cfgdb = self._get_user_cfgdb(context)
            router_id = interface_info['id']
            if 'port_id' in interface_info['resource']:
                port_id = interface_info['resource']['port_id']
                return cfgdb.remove_router_interface(router_id,
                                                     port_id=port_id)
            elif 'subnet_id' in interface_info['resource']:
                subnet_id = interface_info['resource']['subnet_id']
                return cfgdb.remove_router_interface(router_id,
                                                     subnet_id=subnet_id)
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_http_post_router(self):
        """
        Bottle callback for Router POST
        """
        context, router = self._get_requests_data()

        if context['operation'] == 'READ':
            return self.plugin_get_router(context, router)
        elif context['operation'] == 'CREATE':
            return self.plugin_create_router(context, router)
        elif context['operation'] == 'UPDATE':
            return self.plugin_update_router(context, router)
        elif context['operation'] == 'DELETE':
            return self.plugin_delete_router(context, router)
        elif context['operation'] == 'READALL':
            return self.plugin_get_routers(context, router)
        elif context['operation'] == 'READCOUNT':
            return self.plugin_get_routers_count(context, router)
        elif context['operation'] == 'ADDINTERFACE':
            return self.plugin_add_router_interface(context, router)
        elif context['operation'] == 'DELINTERFACE':
            return self.plugin_del_router_interface(context, router)


    # IPAM API Handling
    def plugin_get_ipam(self, context, ipam):
        """
        IPAM get request
        """

        fields = ipam['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            ipam_info = cfgdb.ipam_read(ipam['id'])
            return ipam_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_create_ipam(self, context, ipam):
        """
        IPAM create request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            ipam_info = cfgdb.ipam_create(ipam['resource'])
            return ipam_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_update_ipam(self, context, ipam):
        """
        IPAM update request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            ipam_info = cfgdb.ipam_update(ipam['id'],
                                          ipam['resource'])
            return ipam_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_delete_ipam(self, context, ipam):
        """
        IPAM delete request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            cfgdb.ipam_delete(ipam['id'])
            LOG.debug("plugin_delete_ipam(): " + 
                pformat(ipam['id']))
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_ipams(self, context, ipam):
        """
        IPAM get request
        """

        filters = ipam['filters']
        fields = ipam['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            ipams_info = cfgdb.ipam_list(context, filters)
            return json.dumps(ipams_info)
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_ipams_count(self, context, ipam):
        """
        IPAM count request
        """

        filters = ipam['filters']

        try:
            cfgdb = self._get_user_cfgdb(context)
            ipams_count = cfgdb.ipam_count(context, filters)
            LOG.debug("plugin_get_ipams_count(): filters: "
                      + pformat(filters) + " data: " + str(ipams_count))
            return {'count': ipams_count}
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_http_post_ipam(self):
        """
        Bottle callback for IPAM POST
        """
        context, ipam = self._get_requests_data()

        if context['operation'] == 'READ':
            return self.plugin_get_ipam(context, ipam)
        elif context['operation'] == 'CREATE':
            return self.plugin_create_ipam(context, ipam)
        elif context['operation'] == 'UPDATE':
            return self.plugin_update_ipam(context, ipam)
        elif context['operation'] == 'DELETE':
            return self.plugin_delete_ipam(context, ipam)
        elif context['operation'] == 'READALL':
            return self.plugin_get_ipams(context, ipam)
        elif context['operation'] == 'READCOUNT':
            return self.plugin_get_ipams_count(context, ipam)

    # Policy IP API Handling
    def plugin_get_policy(self, context, policy):
        """
        Policy get request
        """

        fields = policy['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            pol_info = cfgdb.policy_read(policy['id'])
            return pol_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_create_policy(self, context, policy):
        """
        Policy create request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            pol_info = cfgdb.policy_create(policy['resource'])
            return pol_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_update_policy(self, context, policy):
        """
        Policy update request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            policy_info = cfgdb.policy_update(policy['id'],
                                              policy['resource'])
            return policy_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_delete_policy(self, context, policy):
        """
        Policy delete request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            cfgdb.policy_delete(policy['id'])
            LOG.debug("plugin_delete_policy(): " + 
                pformat(policy['id']))
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_policys(self, context, policy):
        """
        Policys get request
        """

        filters = policy['filters']
        fields = policy['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            policys_info = cfgdb.policy_list(context, filters)
            return json.dumps(policys_info)
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_policys_count(self, context, policy):
        """
        Policys count request
        """

        filters = policy['filters']

        try:
            cfgdb = self._get_user_cfgdb(context)
            policys_count = cfgdb.policy_count(context, filters)
            LOG.debug("plugin_get_policys_count(): filters: "
                      + pformat(filters) + " data: " + str(policys_count))
            return {'count': policys_count}
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_http_post_policy(self):
        """
        Bottle callback for Policy POST
        """
        context, policy = self._get_requests_data()

        if context['operation'] == 'READ':
            return self.plugin_get_policy(context, policy)
        elif context['operation'] == 'CREATE':
            return self.plugin_create_policy(context, policy)
        elif context['operation'] == 'UPDATE':
            return self.plugin_update_policy(context, policy)
        elif context['operation'] == 'DELETE':
            return self.plugin_delete_policy(context, policy)
        elif context['operation'] == 'READALL':
            return self.plugin_get_policys(context, policy)
        elif context['operation'] == 'READCOUNT':
            return self.plugin_get_policys_count(context, policy)

    def plugin_get_route_table(self, context, route_table):
        """
        Route table get request
        """

        fields = route_table['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            rt_info = cfgdb.route_table_read(route_table['id'])
            return rt_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_create_route_table(self, context, route_table):
        """
        Route table create request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            rt_info = cfgdb.route_table_create(route_table['resource'])
            return rt_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_update_route_table(self, context, route_table):
        """
        Route table update request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            rt_info = cfgdb.route_table_update(route_table['id'],
                                               route_table['resource'])
            return rt_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_delete_route_table(self, context, route_table):
        """
        Route table delete request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            cfgdb.route_table_delete(route_table['id'])
            LOG.debug("plugin_delete_route_table(): " +
                pformat(route_table['id']))
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_route_tables(self, context, route_table):
        """
        Route Tables get request
        """

        filters = route_table['filters']
        fields = route_table['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            rts_info = cfgdb.route_table_list(context, filters)
            return json.dumps(rts_info)
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_route_tables_count(self, context, route_table):
        """
        Route Tables count request
        """

        filters = route_table['filters']

        try:
            cfgdb = self._get_user_cfgdb(context)
            rts_count = cfgdb.route_table_count(filters)
            LOG.debug("plugin_get_route_tables_count(): filters: "
                      + pformat(filters) + " data: " + str(rts_count))
            return {'count': rts_count}
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_http_post_route_table(self):
        """
        Bottle callback for Route-table POST
        """
        context, route_table = self._get_requests_data()

        if context['operation'] == 'READ':
            return self.plugin_get_route_table(context, route_table)
        elif context['operation'] == 'CREATE':
            return self.plugin_create_route_table(context, route_table)
        elif context['operation'] == 'UPDATE':
            return self.plugin_update_route_table(context, route_table)
        elif context['operation'] == 'DELETE':
            return self.plugin_delete_route_table(context, route_table)
        elif context['operation'] == 'READALL':
            return self.plugin_get_route_tables(context, route_table)
        elif context['operation'] == 'READCOUNT':
            return self.plugin_get_route_tables_count(context, route_table)

    def plugin_get_svc_instance(self, context, svc_instance):
        """
        Service instance get request
        """

        fields = svc_instance['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            si_info = cfgdb.svc_instance_read(svc_instance['id'])
            return si_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_create_svc_instance(self, context, svc_instance):
        """
        Service instance create request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            si_info = cfgdb.svc_instance_create(svc_instance['resource'])
            return si_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_delete_svc_instance(self, context, svc_instance):
        """
        Service instance delete request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            cfgdb.svc_instance_delete(svc_instance['id'])
            LOG.debug("plugin_delete_svc_instance(): " +
                pformat(svc_instance['id']))
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_get_svc_instances(self, context, svc_instance):
        """
        Service instance get request
        """

        filters = svc_instance['filters']
        fields = svc_instance['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            sis_info = cfgdb.svc_instance_list(context, filters)
            return json.dumps(sis_info)
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_http_post_svc_instance(self):
        """
        Bottle callback for Route-table POST
        """
        context, svc_instance = self._get_requests_data()

        if context['operation'] == 'READ':
            return self.plugin_get_svc_instance(context, svc_instance)
        elif context['operation'] == 'CREATE':
            return self.plugin_create_svc_instance(context, svc_instance)
        elif context['operation'] == 'DELETE':
            return self.plugin_delete_svc_instance(context, svc_instance)
        elif context['operation'] == 'READALL':
            return self.plugin_get_svc_instances(context, svc_instance)

