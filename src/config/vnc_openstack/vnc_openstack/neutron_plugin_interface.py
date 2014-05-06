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

from vnc_api import vnc_api
from neutron_plugin_db import DBInterface


LOG = logging.getLogger(__name__)

@bottle.error(404)
def error_404(err):
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
        self._multi_tenancy = None
        self._vnc_lib = None
        self._cfgdb = None
        self._cfgdb_map = {}

    def _get_api_server_connection(self):
        while True:
            try:
                self._vnc_lib = vnc_api.VncApi(
                    api_server_host=self._vnc_api_ip,
                    api_server_port=self._vnc_api_port,
                    username=self._auth_user,
                    password=self._auth_passwd,
                    tenant_name=self._auth_tenant)
                break
            except requests.ConnectionError:
                gevent.sleep(1)

    def _connect_to_db(self):
        """
        Many instantiations of plugin (base + extensions) but need to have
        only one config db conn (else error from ifmap-server)
        """
        if self._cfgdb is None:
            # Initialize connection to DB and add default entries
            self._cfgdb = DBInterface(self._auth_user,
                                      self._auth_passwd,
                                      self._auth_tenant,
                                      self._vnc_api_ip,
                                      self._vnc_api_port)
            self._cfgdb.manager = self
    #end _connect_to_db

    def _get_user_cfgdb(self, context):

        self._connect_to_db()
        self._get_api_server_connection()

        if not self._multi_tenancy:
            return self._cfgdb
        user_id = context['user_id']
        role = string.join(context['roles'], ",")
        if not user_id in self._cfgdb_map:
            self._cfgdb_map[user_id] = DBInterface(
                self._auth_user, self._auth_passwd, self._auth_tenant,
                self._vnc_api_ip, self._vnc_api_port,
                user_info={'user_id': user_id, 'role': role})
            self._cfgdb_map[user_id].manager = self

        return self._cfgdb_map[user_id]

    def _get_requests_data(self):
        ctype = bottle.request.headers['content-type']
        try:
            if ctype == 'application/json':
                req = bottle.request.json
                return req['context'], req['data']
        except Exception as e:
            bottle.abort(400, 'Unable to parse request data')

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

    def plugin_get_subnet(self, context, subnet):
        """
        Subnet get request
        """

        fields = subnet['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            subnet_info = cfgdb.subnet_read(subnet['id'], fields)
            return subnet_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_create_subnet(self, context, subnet):
        """
        Subnet create request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            net_info = cfgdb.subnet_create(subnet['resource'])
            return net_info
        except Exception as e:
            cgitb.Hook(format="text").handle(sys.exc_info())
            raise e

    def plugin_update_subnet(self, context, subnet):
        """
        Subnet update request
        """

        try:
            cfgdb = self._get_user_cfgdb(context)
            net_info = cfgdb.subnet_update(subnet['id'],
                                           subnet['resource'])
            return net_info
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
            return json.dumps(subnets_info)
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

    def plugin_get_port(self, context, port):
        """
        Port get request
        """

        fields = port['fields']

        try:
            cfgdb = self._get_user_cfgdb(context)
            port_info = cfgdb.port_read(port['id'], fields)
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
            net_info = cfgdb.port_create(port['resource'])
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

