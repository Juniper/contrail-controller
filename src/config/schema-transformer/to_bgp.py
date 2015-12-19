#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of transforming user-exposed VNC
configuration model/schema to a representation needed by VNC Control Plane
(BGP-based)
"""

import gevent
# Import kazoo.client before monkey patching
from cfgm_common.zkclient import ZookeeperClient
from gevent import monkey
monkey.patch_all()
import sys
reload(sys)
sys.setdefaultencoding('UTF8')
import requests
import ConfigParser
import cgitb

import copy
import argparse
import socket
import uuid

import cfgm_common as common
from cfgm_common import vnc_cpu_info

from cfgm_common.exceptions import *
from cfgm_common import svc_info
from config_db import *

from pysandesh.sandesh_base import *
from pysandesh.sandesh_logger import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from cfgm_common.uve.virtual_network.ttypes import *
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, Module2NodeType, \
    NodeTypeNames, INSTANCE_ID_DEFAULT
from schema_transformer.sandesh.st_introspect import ttypes as sandesh
from schema_transformer.sandesh.traces.ttypes import MessageBusNotifyTrace,\
    DependencyTrackerResource
import discoveryclient.client as client
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from cfgm_common.uve.cfgm_cpuinfo.ttypes import NodeStatusUVE, NodeStatus
from cStringIO import StringIO
from db import SchemaTransformerDB
from cfgm_common.vnc_kombu import VncKombuClient
from cfgm_common.dependency_tracker import DependencyTracker

# connection to api-server
_vnc_lib = None
# zookeeper client connection
_zookeeper_client = None


class SchemaTransformer(object):

    _REACTION_MAP = {
        'routing_instance': {
            'self': ['virtual_network'],
        },
        'virtual_machine_interface': {
            'self': ['virtual_machine', 'virtual_network', 'bgp_as_a_service'],
            'virtual_network': ['virtual_machine', 'bgp_as_a_service'],
            'logical_router': ['virtual_network'],
            'instance_ip': ['virtual_machine'],
            'floating_ip': ['virtual_machine'],
            'virtual_machine': [],
            'bgp_as_a_service': [],
        },
        'virtual_network': {
            'self': ['network_policy'],
            'routing_instance': ['network_policy'],
            'network_policy': [],
            'virtual_machine_interface': [],
            'route_table': [],
        },
        'virtual_machine': {
            'self': ['service_instance'],
            'virtual_machine_interface': ['service_instance'],
            'service_instance': ['virtual_machine_interface']
        },
        'service_instance': {
            'self': ['network_policy'],
            'routing_policy': ['network_policy'],
            'virtual_machine': ['network_policy'],
            'network_policy': ['virtual_machine']
        },
        'network_policy': {
            'self': ['virtual_network', 'network_policy', 'service_instance'],
            'service_instance': ['virtual_network'],
            'network_policy': ['virtual_network'],
            'virtual_network': ['virtual_network', 'network_policy']
        },
        'security_group': {
            'self': ['security_group'],
            'security_group': [],
        },
        'route_table': {
            'self': ['virtual_network'],
        },
        'logical_router': {
            'self': [],
            'virtual_machine_interface': [],
        },
        'floating_ip': {
            'self': ['virtual_machine_interface'],
        },
        'instance_ip': {
            'self': ['virtual_machine_interface'],
        },
        'bgp_as_a_service': {
            'self': ['bgp_router'],
            'virtual_machine_interface': ['bgp_router']
        },
        'bgp_router': {
            'self': [],
            'bgp_as_a_service': [],
        },
        'global_system_config': {
            'self': [],
        },
        'routing_policy': {
            'self': ['service_instance'],
        }
    }

    def __init__(self, args=None):
        self._args = args
        self._fabric_rt_inst_obj = None

        # Initialize discovery client
        self._disc = None
        if self._args.disc_server_ip and self._args.disc_server_port:
            self._disc = client.DiscoveryClient(
                self._args.disc_server_ip,
                self._args.disc_server_port,
                ModuleNames[Module.SCHEMA_TRANSFORMER])

        self._sandesh = Sandesh()
        # Reset the sandesh send rate limit value
        if args.sandesh_send_rate_limit is not None:
            SandeshSystem.set_sandesh_send_rate_limit(
                args.sandesh_send_rate_limit)
        sandesh.VnList.handle_request = self.sandesh_vn_handle_request
        sandesh.RoutintInstanceList.handle_request = \
            self.sandesh_ri_handle_request
        sandesh.ServiceChainList.handle_request = \
            self.sandesh_sc_handle_request
        sandesh.StObjectReq.handle_request = \
            self.sandesh_st_object_handle_request
        module = Module.SCHEMA_TRANSFORMER
        module_name = ModuleNames[module]
        node_type = Module2NodeType[module]
        node_type_name = NodeTypeNames[node_type]
        instance_id = INSTANCE_ID_DEFAULT
        hostname = socket.gethostname()
        self._sandesh.init_generator(
            module_name, hostname, node_type_name, instance_id,
            self._args.collectors, 'to_bgp_context',
            int(args.http_server_port),
            ['cfgm_common', 'schema_transformer.sandesh'], self._disc,
            logger_class=args.logger_class,
            logger_config_file=args.logging_conf)
        self._sandesh.set_logging_params(enable_local_log=args.log_local,
                                    category=args.log_category,
                                    level=args.log_level,
                                    file=args.log_file,
                                    enable_syslog=args.use_syslog,
                                    syslog_facility=args.syslog_facility)
        ConnectionState.init(self._sandesh, hostname, module_name, instance_id,
                staticmethod(ConnectionState.get_process_state_cb),
                NodeStatusUVE, NodeStatus)

        rabbit_servers = self._args.rabbit_server
        rabbit_port = self._args.rabbit_port
        rabbit_user = self._args.rabbit_user
        rabbit_password = self._args.rabbit_password
        rabbit_vhost = self._args.rabbit_vhost
        rabbit_ha_mode = self._args.rabbit_ha_mode

        self._db_resync_done = gevent.event.Event()

        q_name = 'schema_transformer.%s' % (socket.gethostname())
        self._vnc_kombu = VncKombuClient(rabbit_servers, rabbit_port,
                                         rabbit_user, rabbit_password,
                                         rabbit_vhost, rabbit_ha_mode,
                                         q_name, self._vnc_subscribe_callback,
                                         self.config_log)
        self._cassandra = SchemaTransformerDB(self, _zookeeper_client)
        DBBaseST.init(self, self._sandesh.logger(), self._cassandra)
        DBBaseST._sandesh = self._sandesh
        DBBaseST._vnc_lib = _vnc_lib
        ServiceChain.init()
        self.reinit()
        # create cpu_info object to send periodic updates
        sysinfo_req = False
        cpu_info = vnc_cpu_info.CpuInfo(
            module_name, instance_id, sysinfo_req, self._sandesh, 60)
        self._cpu_info = cpu_info
        self._db_resync_done.set()

    # end __init__

    def config_log(self, msg, level):
        self._sandesh.logger().log(SandeshLogger.get_py_logger_level(level),
                                   msg)

    def _vnc_subscribe_callback(self, oper_info):
        self._db_resync_done.wait()
        dependency_tracker = None
        try:
            msg = "Notification Message: %s" % (pformat(oper_info))
            self.config_log(msg, level=SandeshLevel.SYS_DEBUG)
            obj_type = oper_info['type'].replace('-', '_')
            obj_class = DBBaseST.get_obj_type_map().get(obj_type)
            if obj_class is None:
                return

            oper = oper_info['oper']
            obj_id = oper_info['uuid']
            notify_trace = MessageBusNotifyTrace(
                request_id=oper_info.get('request_id'),
                operation=oper, uuid=obj_id)
            if oper == 'CREATE':
                obj_dict = oper_info['obj_dict']
                obj_fq_name = ':'.join(obj_dict['fq_name'])
                obj = obj_class.locate(obj_fq_name)
                if obj is None:
                    self.config_log('%s id %s fq_name %s not found' % (
                                    obj_type, obj_id, obj_fq_name),
                                    level=SandeshLevel.SYS_INFO)
                    return
                dependency_tracker = DependencyTracker(
                    DBBaseST.get_obj_type_map(), self._REACTION_MAP)
                dependency_tracker.evaluate(obj_type, obj)
            elif oper == 'UPDATE':
                obj = obj_class.get_by_uuid(obj_id)
                old_dt = None
                if obj is not None:
                    old_dt = DependencyTracker(
                        DBBaseST.get_obj_type_map(), self._REACTION_MAP)
                    old_dt.evaluate(obj_type, obj)
                else:
                    self.config_log('%s id %s not found' % (obj_type, obj_id),
                                    level=SandeshLevel.SYS_INFO)
                    return
                try:
                    obj.update()
                except NoIdError:
                    self.config_log('%s id %s update caused NoIdError' % (obj_type, obj_id),
                                    level=SandeshLevel.SYS_INFO)
                    return
                dependency_tracker = DependencyTracker(
                    DBBaseST.get_obj_type_map(), self._REACTION_MAP)
                dependency_tracker.evaluate(obj_type, obj)
                if old_dt:
                    for resource, ids in old_dt.resources.items():
                        if resource not in dependency_tracker.resources:
                            dependency_tracker.resources[resource] = ids
                        else:
                            dependency_tracker.resources[resource] = list(
                                set(dependency_tracker.resources[resource]) |
                                set(ids))
            elif oper == 'DELETE':
                obj = obj_class.get_by_uuid(obj_id)
                if obj is None:
                    return
                dependency_tracker = DependencyTracker(
                    DBBaseST.get_obj_type_map(), self._REACTION_MAP)
                dependency_tracker.evaluate(obj_type, obj)
                obj_class.delete(obj.name)
            else:
                # unknown operation
                self.config_log('Unknown operation %s' % oper,
                                level=SandeshLevel.SYS_ERR)
                return

            if obj is None:
                self.config_log('Error while accessing %s uuid %s' % (
                                obj_type, obj_id))
                return

            notify_trace.fq_name = obj.name
            if not dependency_tracker:
                return

            notify_trace.dependency_tracker_resources = []
            for res_type, res_id_list in dependency_tracker.resources.items():
                if not res_id_list:
                    continue
                dtr = DependencyTrackerResource(obj_type=res_type, obj_keys=res_id_list)
                notify_trace.dependency_tracker_resources.append(dtr)
                cls = DBBaseST.get_obj_type_map().get(res_type)
                if cls is None:
                    continue
                for res_id in res_id_list:
                    res_obj = cls.get(res_id)
                    if res_obj is not None:
                        res_obj.evaluate()

            for vn_id in dependency_tracker.resources.get('virtual_network', []):
                vn = VirtualNetworkST.get(vn_id)
                if vn is not None:
                    vn.uve_send()
            # end for vn_id
        except Exception as e:
            string_buf = cStringIO.StringIO()
            cgitb.Hook(file=string_buf, format="text").handle(sys.exc_info())
            notify_trace.error = string_buf.getvalue()
            try:
                with open(self._args.trace_file, 'a') as err_file:
                    err_file.write(string_buf.getvalue())
            except IOError:
                self.config_log(string_buf.getvalue(), level=SandeshLevel.SYS_ERR)
        finally:
            try:
                trace_msg(notify_trace, 'MessageBusNotifyTraceBuf', self._sandesh)
            except Exception:
                pass


    # end _vnc_subscribe_callback

    # Clean up stale objects
    def reinit(self):
        for gsc in GlobalSystemConfigST.list_vnc_obj():
            GlobalSystemConfigST.locate(gsc.uuid, gsc)
        for bgpr in BgpRouterST.list_vnc_obj():
            if bgpr.get_bgp_router_parameters():
                BgpRouterST.locate(bgpr.get_fq_name_str(), bgpr)
        for lr in LogicalRouterST.list_vnc_obj():
           LogicalRouterST.locate(lr.get_fq_name_str(), lr)
        vn_list = list(VirtualNetworkST.list_vnc_obj())
        vn_id_list = [vn.uuid for vn in vn_list]
        ri_dict = {}
        for ri in DBBaseST.list_vnc_obj('routing_instance'):
            delete = False
            if ri.parent_uuid not in vn_id_list:
                delete = True
            else:
                # if the RI was for a service chain and service chain no
                # longer exists, delete the RI
                sc_id = RoutingInstanceST._get_service_id_from_ri(ri.get_fq_name_str())
                if sc_id and sc_id not in ServiceChain:
                    delete = True
                else:
                    ri_dict[ri.get_fq_name_str()] = ri
            if delete:
                try:
                    ri_obj = RoutingInstanceST(ri.get_fq_name_str(), ri)
                    ri_obj.delete_obj()
                except NoIdError:
                    pass
                except Exception as e:
                    self._sandesh._logger.error(
                            "Error while deleting routing instance %s: %s",
                            ri.get_fq_name_str(), str(e))

        # end for ri

        sg_list = list(SecurityGroupST.list_vnc_obj())
        sg_id_list = [sg.uuid for sg in sg_list]
        sg_acl_dict = {}
        vn_acl_dict = {}
        for acl in DBBaseST.list_vnc_obj('access_control_list'):
            delete = False
            if acl.parent_type == 'virtual-network':
                if acl.parent_uuid in vn_id_list:
                    vn_acl_dict[acl.uuid] = acl
                else:
                    delete = True
            elif acl.parent_type == 'security-group':
                if acl.parent_uuid in sg_id_list:
                    sg_acl_dict[acl.uuid] = acl
                else:
                    delete = True
            else:
                delete = True

            if delete:
                try:
                    _vnc_lib.access_control_list_delete(id=acl.uuid)
                except NoIdError:
                    pass
                except Exception as e:
                    self._sandesh._logger.error(
                            "Error while deleting acl %s: %s",
                            acl.uuid, str(e))
        # end for acl

        gevent.sleep(0.001)
        for sg in sg_list:
            SecurityGroupST.locate(sg.get_fq_name_str(), sg, sg_acl_dict)

        # update sg rules after all SG objects are initialized to avoid
        # rewriting of ACLs multiple times
        for sg in SecurityGroupST.values():
            sg.update_policy_entries()

        gevent.sleep(0.001)
        for rt in RouteTargetST.list_vnc_obj():
            rt_name = rt.get_fq_name_str()
            RouteTargetST.locate(rt_name, RouteTarget(rt_name))
        for vn in vn_list:
            VirtualNetworkST.locate(vn.get_fq_name_str(), vn, vn_acl_dict)
        for ri_name, ri_obj in ri_dict.items():
            RoutingInstanceST.locate(ri_name, ri_obj)

        for policy in NetworkPolicyST.list_vnc_obj():
            NetworkPolicyST.locate(policy.get_fq_name_str(), policy)
        gevent.sleep(0.001)
        for vmi in VirtualMachineInterfaceST.list_vnc_obj():
            VirtualMachineInterfaceST.locate(vmi.get_fq_name_str(), vmi)

        gevent.sleep(0.001)
        for iip in InstanceIpST.list_vnc_obj():
            InstanceIpST.locate(iip.get_fq_name_str(), iip)
        gevent.sleep(0.001)
        for fip in FloatingIpST.list_vnc_obj():
            FloatingIpST.locate(fip.get_fq_name_str(), fip)

        gevent.sleep(0.001)
        for si in ServiceInstanceST.list_vnc_obj():
            si_st = ServiceInstanceST.locate(si.get_fq_name_str(), si)
            if si_st is None:
                continue
            for ref in si.get_virtual_machine_back_refs() or []:
                vm_name = ':'.join(ref['to'])
                vm = VirtualMachineST.locate(vm_name)
                si_st.virtual_machines.add(vm_name)
            props = si.get_service_instance_properties()
            if not props.auto_policy:
                continue
            si_st.add_properties(props)

        for cls in DBBaseST.get_obj_type_map().values():
            for obj in cls.values():
                obj.evaluate()
        self.process_stale_objects()
    # end reinit

    def cleanup(self):
        # TODO cleanup sandesh context
        pass
    # end cleanup

    def process_stale_objects(self):
        for sc in ServiceChain.values():
            if sc.created_stale:
                sc.destroy()
            if sc.present_stale:
                sc.delete()
    # end process_stale_objects

    def sandesh_ri_build(self, vn_name, ri_name):
        vn = VirtualNetworkST.get(vn_name)
        sandesh_ri_list = []
        for riname in vn.routing_instances:
            ri = RoutingInstanceST.get(riname)
            sandesh_ri = sandesh.RoutingInstance(name=ri.obj.get_fq_name_str())
            sandesh_ri.service_chain = ri.service_chain
            sandesh_ri.connections = list(ri.connections)
            sandesh_ri_list.append(sandesh_ri)
        return sandesh_ri_list
    # end sandesh_ri_build

    def sandesh_ri_handle_request(self, req):
        # Return the list of VNs
        ri_resp = sandesh.RoutingInstanceListResp(routing_instances=[])
        if req.vn_name is None:
            for vn in VirtualNetworkST:
                sandesh_ri = self.sandesh_ri_build(vn, req.ri_name)
                ri_resp.routing_instances.extend(sandesh_ri)
        elif req.vn_name in VirtualNetworkST:
            self.sandesh_ri_build(req.vn_name, req.ri_name)
            ri_resp.routing_instances.extend(sandesh_ri)
        ri_resp.response(req.context())
    # end sandesh_ri_handle_request

    def sandesh_vn_build(self, vn_name):
        vn = VirtualNetworkST.get(vn_name)
        sandesh_vn = sandesh.VirtualNetwork(name=vn_name)
        sandesh_vn.policies = vn.network_policys.keys()
        sandesh_vn.connections = list(vn.connections)
        sandesh_vn.routing_instances = vn.routing_instances
        if vn.acl:
            sandesh_vn.acl = vn.acl.get_fq_name_str()
        if vn.dynamic_acl:
            sandesh_vn.dynamic_acl = vn.dynamic_acl.get_fq_name_str()

        return sandesh_vn
    # end sandesh_vn_build

    def sandesh_vn_handle_request(self, req):
        # Return the list of VNs
        vn_resp = sandesh.VnListResp(vn_names=[])
        if req.vn_name is None:
            for vn in VirtualNetworkST:
                sandesh_vn = self.sandesh_vn_build(vn)
                vn_resp.vn_names.append(sandesh_vn)
        elif req.vn_name in VirtualNetworkST:
            sandesh_vn = self.sandesh_vn_build(req.vn_name)
            vn_resp.vn_names.append(sandesh_vn)
        vn_resp.response(req.context())
    # end sandesh_vn_handle_request

    def sandesh_sc_handle_request(self, req):
        sc_resp = sandesh.ServiceChainListResp(service_chains=[])
        if req.sc_name is None:
            for sc in ServiceChain.values():
                sandesh_sc = sc.build_introspect()
                sc_resp.service_chains.append(sandesh_sc)
        elif req.sc_name in ServiceChain:
            sandesh_sc = ServiceChain[req.sc_name].build_introspect()
            sc_resp.service_chains.append(sandesh_sc)
        sc_resp.response(req.context())
    # end sandesh_sc_handle_request

    def sandesh_st_object_handle_request(self, req):
        st_resp = sandesh.StObjectListResp()
        obj_type_map = DBBaseST.get_obj_type_map()
        if req.object_type is not None:
            if req.object_type not in obj_type_map:
                return st_resp
            obj_cls_list = [obj_type_map[req.object_type]]
        else:
            obj_cls_list = obj_type_map.values()
        for obj_cls in obj_cls_list:
            id_or_name = req.object_id_or_fq_name
            if id_or_name:
                obj = obj_cls.get(id_or_name) or obj_cls.get_by_uuid(id_or_name)
                if obj is None:
                    continue
                st_resp.objects.append(obj.handle_st_object_req())
            else:
                for obj in obj_cls.values():
                    st_resp.objects.append(obj.handle_st_object_req())
        return st_resp
    # end sandesh_st_object_handle_request

    def reset(self):
        for cls in DBBaseST.get_obj_type_map().values():
            cls.reset()
        self._vnc_kombu.shutdown()
    # end reset
# end class SchemaTransformer


def parse_args(args_str):
    '''
    Eg. python to_bgp.py --rabbit_server localhost
                         --rabbit_port 5672
                         --rabbit_user guest
                         --rabbit_password guest
                         --cassandra_server_list 10.1.2.3:9160
                         --api_server_ip 10.1.2.3
                         --api_server_port 8082
                         --api_server_use_ssl False
                         --zk_server_ip 10.1.2.3
                         --zk_server_port 2181
                         --collectors 127.0.0.1:8086
                         --disc_server_ip 127.0.0.1
                         --disc_server_port 5998
                         --http_server_port 8090
                         --log_local
                         --log_level SYS_DEBUG
                         --log_category test
                         --log_file <stdout>
                         --trace_file /var/log/contrail/schema.err
                         --use_syslog
                         --syslog_facility LOG_USER
                         --cluster_id <testbed-name>
                         [--reset_config]
    '''

    # Source any specified config/ini file
    # Turn off help, so we      all options in response to -h
    conf_parser = argparse.ArgumentParser(add_help=False)

    conf_parser.add_argument("-c", "--conf_file", action='append',
                             help="Specify config file", metavar="FILE")
    args, remaining_argv = conf_parser.parse_known_args(args_str.split())

    defaults = {
        'rabbit_server': 'localhost',
        'rabbit_port': '5672',
        'rabbit_user': 'guest',
        'rabbit_password': 'guest',
        'rabbit_vhost': None,
        'rabbit_ha_mode': False,
        'cassandra_server_list': '127.0.0.1:9160',
        'api_server_ip': '127.0.0.1',
        'api_server_port': '8082',
        'api_server_use_ssl': False,
        'zk_server_ip': '127.0.0.1',
        'zk_server_port': '2181',
        'collectors': None,
        'disc_server_ip': None,
        'disc_server_port': None,
        'http_server_port': '8087',
        'log_local': False,
        'log_level': SandeshLevel.SYS_DEBUG,
        'log_category': '',
        'log_file': Sandesh._DEFAULT_LOG_FILE,
        'trace_file': '/var/log/contrail/schema.err',
        'use_syslog': False,
        'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
        'cluster_id': '',
        'logging_conf': '',
        'logger_class': None,
        'sandesh_send_rate_limit': SandeshSystem.get_sandesh_send_rate_limit(),
        'bgpaas_port_start': 50000,
        'bgpaas_port_end': 50256,
    }
    secopts = {
        'use_certs': False,
        'keyfile': '',
        'certfile': '',
        'ca_certs': '',
    }
    ksopts = {
        'admin_user': 'user1',
        'admin_password': 'password1',
        'admin_tenant_name': 'default-domain'
    }
    cassandraopts = {
        'cassandra_user': None,
        'cassandra_password': None,
    }

    if args.conf_file:
        config = ConfigParser.SafeConfigParser()
        config.read(args.conf_file)
        defaults.update(dict(config.items("DEFAULTS")))
        if ('SECURITY' in config.sections() and
                'use_certs' in config.options('SECURITY')):
            if config.getboolean('SECURITY', 'use_certs'):
                secopts.update(dict(config.items("SECURITY")))
        if 'KEYSTONE' in config.sections():
            ksopts.update(dict(config.items("KEYSTONE")))

        if 'CASSANDRA' in config.sections():
                cassandraopts.update(dict(config.items('CASSANDRA')))


    # Override with CLI options
    # Don't surpress add_help here so it will handle -h
    parser = argparse.ArgumentParser(
        # Inherit options from config_parser
        parents=[conf_parser],
        # print script description with -h/--help
        description=__doc__,
        # Don't mess with format of description
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    defaults.update(secopts)
    defaults.update(ksopts)
    defaults.update(cassandraopts)
    parser.set_defaults(**defaults)

    parser.add_argument(
        "--cassandra_server_list",
        help="List of cassandra servers in IP Address:Port format",
        nargs='+')
    parser.add_argument(
        "--reset_config", action="store_true",
        help="Warning! Destroy previous configuration and start clean")
    parser.add_argument("--api_server_ip",
                        help="IP address of API server")
    parser.add_argument("--api_server_port",
                        help="Port of API server")
    parser.add_argument("--api_server_use_ssl",
                        help="Use SSL to connect with API server")
    parser.add_argument("--zk_server_ip",
                        help="IP address:port of zookeeper server")
    parser.add_argument("--collectors",
                        help="List of VNC collectors in ip:port format",
                        nargs="+")
    parser.add_argument("--disc_server_ip",
                        help="IP address of the discovery server")
    parser.add_argument("--disc_server_port",
                        help="Port of the discovery server")
    parser.add_argument("--http_server_port",
                        help="Port of local HTTP server")
    parser.add_argument("--log_local", action="store_true",
                        help="Enable local logging of sandesh messages")
    parser.add_argument(
        "--log_level",
        help="Severity level for local logging of sandesh messages")
    parser.add_argument(
        "--log_category",
        help="Category filter for local logging of sandesh messages")
    parser.add_argument("--log_file",
                        help="Filename for the logs to be written to")
    parser.add_argument("--trace_file", help="Filename for the error "
                        "backtraces to be written to")
    parser.add_argument("--use_syslog", action="store_true",
                        help="Use syslog for logging")
    parser.add_argument("--syslog_facility",
                        help="Syslog facility to receive log lines")
    parser.add_argument("--admin_user",
                        help="Name of keystone admin user")
    parser.add_argument("--admin_password",
                        help="Password of keystone admin user")
    parser.add_argument("--admin_tenant_name",
                        help="Tenant name for keystone admin user")
    parser.add_argument("--cluster_id",
                        help="Used for database keyspace separation")
    parser.add_argument(
        "--logging_conf",
        help=("Optional logging configuration file, default: None"))
    parser.add_argument(
        "--logger_class",
        help=("Optional external logger class, default: None"))
    parser.add_argument("--cassandra_user", help="Cassandra user name")
    parser.add_argument("--cassandra_password", help="Cassandra password")
    parser.add_argument("--sandesh_send_rate_limit", type=int,
            help="Sandesh send rate limit in messages/sec")
    parser.add_argument("--rabbit_server", help="Rabbitmq server address")
    parser.add_argument("--rabbit_port", help="Rabbitmq server port")
    parser.add_argument("--rabbit_user", help="Username for rabbit")
    parser.add_argument("--rabbit_vhost", help="vhost for rabbit")
    parser.add_argument("--rabbit_password", help="password for rabbit")
    parser.add_argument("--rabbit_ha_mode", action='store_true',
        help="True if the rabbitmq cluster is mirroring all queue")
    parser.add_argument("--bgpaas_port_start", type=int,
                        help="Start port for bgp-as-a-service proxy")
    parser.add_argument("--bgpaas_port_end", type=int,
                        help="End port for bgp-as-a-service proxy")

    args = parser.parse_args(remaining_argv)
    if type(args.cassandra_server_list) is str:
        args.cassandra_server_list = args.cassandra_server_list.split()
    if type(args.collectors) is str:
        args.collectors = args.collectors.split()

    return args
# end parse_args

transformer = None


def run_schema_transformer(args):
    global _vnc_lib

    def connection_state_update(status, message=None):
        ConnectionState.update(
            conn_type=ConnType.APISERVER, name='ApiServer',
            status=status, message=message or '',
            server_addrs=['%s:%s' % (args.api_server_ip,
                                     args.api_server_port)])
    # end connection_state_update

    # Retry till API server is up
    connected = False
    connection_state_update(ConnectionStatus.INIT)
    while not connected:
        try:
            _vnc_lib = VncApi(
                args.admin_user, args.admin_password, args.admin_tenant_name,
                args.api_server_ip, args.api_server_port, api_server_use_ssl=args.api_server_use_ssl)
            connected = True
            connection_state_update(ConnectionStatus.UP)
        except requests.exceptions.ConnectionError as e:
            # Update connection info
            connection_state_update(ConnectionStatus.DOWN, str(e))
            time.sleep(3)
        except (RuntimeError, ResourceExhaustionError):
            # auth failure or haproxy throws 503
            time.sleep(3)

    global transformer
    transformer = SchemaTransformer(args)
    gevent.joinall(transformer._vnc_kombu.greenlets())
# end run_schema_transformer


def main(args_str=None):
    global _zookeeper_client
    if not args_str:
        args_str = ' '.join(sys.argv[1:])
    args = parse_args(args_str)
    if args.cluster_id:
        client_pfx = args.cluster_id + '-'
        zk_path_pfx = args.cluster_id + '/'
    else:
        client_pfx = ''
        zk_path_pfx = ''
    _zookeeper_client = ZookeeperClient(client_pfx+"schema", args.zk_server_ip)
    _zookeeper_client.master_election(zk_path_pfx + "/schema-transformer",
                                      os.getpid(), run_schema_transformer,
                                      args)
# end main


def server_main():
    cgitb.enable(format='text')
    main()
# end server_main

if __name__ == '__main__':
    server_main()
