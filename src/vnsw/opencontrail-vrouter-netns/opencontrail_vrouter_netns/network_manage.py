#!/bin/env python

from __future__ import print_function
from builtins import object
import argparse
from vnc_api.vnc_api import *


class NetworkManager(object):
    def __init__(self, api_server, api_port, project=None):
        self._client = VncApi(api_server_host=api_server,
                              api_server_port=api_port)
        self._project = project

    def _netname(self, name):
        fqn = name.split(':')
        if len(fqn) == 1:
            return "%s:%s" % (self._project, name)
        return name
    # end _netname

    def _add_subnet(self, vnet, subnet):
        ipam = self._client.network_ipam_read(
            fq_name=['default-domain',
                     'default-project',
                     'default-network-ipam'])

        (prefix, plen) = subnet.split('/')
        subnet = IpamSubnetType(subnet=SubnetType(prefix, int(plen)))
        vnet.add_network_ipam(ipam, VnSubnetsType([subnet]))

    # end _add_subnet

    def create(self, name, subnet=None):
        netname = self._netname(name)
        fq_name = netname.split(':')
        try:
            vnet = self._client.virtual_network_read(fq_name=fq_name)
            print('Network %s already exists' % netname)
            sys.exit(1)
        except NoIdError:
            pass

        vnet = VirtualNetwork(fq_name[-1], parent_type='project',
                              fq_name=fq_name)
        if subnet:
            self._add_subnet(vnet, subnet)

        self._client.virtual_network_create(vnet)
    # end create

    def delete(self, name):
        netname = self._netname(name)
        fq_name = netname.split(':')
        try:
            vnet = self._client.virtual_network_read(fq_name=fq_name)
        except NoIdError:
            print('Network %s does not exist' % netname)
            sys.exit(1)

        self._client.virtual_network_delete(id=vnet.uuid)
    # end delete

    def show(self, name):
        netname = self._netname(name)
        fq_name = netname.split(':')
        try:
            vnet = self._client.virtual_network_read(fq_name=fq_name)
        except NoIdError:
            print('Network %s does not exist' % netname)
            sys.exit(1)

        print('name: %s' % ':'.join(vnet.fq_name))
        print('uuid: %s' % vnet.uuid)

        ipam_refs = vnet.get_network_ipam_refs()
        if ipam_refs is None:
            ipam_refs = []
        for iref in ipam_refs:
            subnets = iref['attr'].ipam_subnets
            for snet in subnets:
                print('    ', end=' ')
                print(snet.subnet.__dict__)

        instance_list = vnet.get_routing_instances()
        if len(instance_list):
            rt_instance = self._client.routing_instance_read(
                id=instance_list[0]['uuid'])
            for rt in rt_instance.route_target_refs:
                print('    ', end=' ')
                print((rt['to'][0], rt['attr'].__dict__))
    # end show

    def _rti_rtarget_add(self, vnet, rtarget_str, direction):
        instance_list = vnet.get_routing_instances()
        if len(instance_list) == 0:
            print('Routing instance not found')
            sys.exit(1)

        rt_instance = self._client.routing_instance_read(
            id=instance_list[0]['uuid'])
        for rt in rt_instance.route_target_refs:
            if rt['to'][0] == rtarget_str:
                sys.exit(1)

        try:
            rt_obj = self._client.route_target_read(fq_name=rtarget_str.split(':'))
        except NoIdError:
            rt_obj = RouteTarget(rtarget_str)
            self._client.route_target_create(rt_obj)

        rt_instance.add_route_target(RouteTarget(rtarget_str),
                                     InstanceTargetType(
                                     import_export=direction))
        self._client.routing_instance_update(rt_instance)

    def _rti_rtarget_del(self, vnet, rtarget_str, direction):
        instance_list = vnet.get_routing_instances()
        if len(instance_list) == 0:
            print('Routing instance not found')
            sys.exit(1)

        rt_instance = self._client.routing_instance_read(
            id=instance_list[0]['uuid'])
        for rt in rt_instance.route_target_refs:
            if rt['to'][0] == rtarget_str:
                rt_obj = self._client.route_target_read(id=rt['uuid'])

        rt_instance.del_route_target(rt_obj)
        self._client.routing_instance_update(rt_instance)
    # end _rti_rtarget_add

    def rtarget_add(self, name, rtarget, direction=None):
        netname = self._netname(name)
        fq_name = netname.split(':')
        try:
            vnet = self._client.virtual_network_read(fq_name=fq_name)
        except NoIdError:
            print('Network %s does not exist' % netname)
            sys.exit(1)

        rtarget_str = 'target:%s' % rtarget

        if direction:
            self._rti_rtarget_add(vnet, rtarget_str, direction)
            return

        target_list = vnet.get_route_target_list()
        if target_list:
            for rt in target_list:
                if rt['to'][0] == rtarget_str:
                    sys.exit(1)

        if target_list:
            target_list.add_route_target(rtarget_str)
        else:
            target_list = RouteTargetList([rtarget_str])
        vnet.set_route_target_list(target_list)
        self._client.virtual_network_update(vnet)
    # end rtarget_add

    def rtarget_del(self, name, rtarget, direction=None):
        netname = self._netname(name)
        fq_name = netname.split(':')
        try:
            vnet = self._client.virtual_network_read(fq_name=fq_name)
        except NoIdError:
            print('Network %s does not exist' % netname)
            sys.exit(1)

        rtarget_str = 'target:%s' % rtarget

        if direction:
            self._rti_rtarget_del(vnet, rtarget_str, direction)
            return

        # target_list = vnet.get_route_target_list()
        # if target_list:
        #     for rt in target_list:
        #         if rt['to'][0] == rtarget_str:
        #             sys.exit(1)

        # if target_list:
        #     target_list.add_route_target(rtarget_str)
        # else:
        #     target_list = RouteTargetList([rtarget_str])
        # vnet.set_route_target_list(target_list)
        # self._client.virtual_network_update(vnet)
    # end rtarget_del


def main(argv):
    parser = argparse.ArgumentParser()
    defaults = {
        'api-server': '127.0.0.1',
        'api-port': '8082',
        'project': 'default-domain:default-project',
    }
    parser.set_defaults(**defaults)
    parser.add_argument(
        "-s", "--api-server", help="API server address")
    parser.add_argument("-p", "--api-port", help="API server port")
    parser.add_argument("--project", help="OpenStack project name")
    parser.add_argument("--rtarget", help="Router target")
    parser.add_argument("--import-only", action='store_true')
    parser.add_argument("--export-only", action='store_true')
    parser.add_argument("--subnet", help="Subnet prefix")
    parser.add_argument("command", choices=['create', 'delete', 'show',
                                            'rtarget-add', 'rtarget-del'])
    parser.add_argument("network", help="Network name")

    arguments = parser.parse_args(argv)

    manager = NetworkManager(arguments.api_server, arguments.api_port,
                             project=arguments.project)

    if arguments.command == "create":
        manager.create(arguments.network, subnet=arguments.subnet)
    elif arguments.command == "delete":
        manager.delete(arguments.network)
    elif arguments.command == "show":
        manager.show(arguments.network)
    elif arguments.command == "rtarget-add":
        direction = None
        if arguments.import_only:
            direction = 'import'
        elif arguments.export_only:
            direction = 'export'
        manager.rtarget_add(arguments.network, arguments.rtarget, direction)
    elif arguments.command == "rtarget-del":
        direction = None
        if arguments.import_only:
            direction = 'import'
        elif arguments.export_only:
            direction = 'export'
        manager.rtarget_del(arguments.network, arguments.rtarget, direction)


if __name__ == '__main__':
    main(sys.argv[1:])
