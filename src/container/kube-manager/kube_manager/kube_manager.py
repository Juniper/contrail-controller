#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Kubernetes network manager
"""

import sys
import socket
import gevent
from gevent.queue import Queue

import argparse

from vnc_api.vnc_api import *
import vnc.vnc_kubernetes as vnc_kubernetes
import common.logger as logger
from cfgm_common import vnc_cgitb
from cfgm_common.utils import cgitb_hook
from sandesh_common.vns.constants import (ModuleNames, Module2NodeType,
                                          NodeTypeNames, INSTANCE_ID_DEFAULT,
                                          HttpPortKubemgr)
from sandesh_common.vns.ttypes import Module
from pysandesh.sandesh_base import Sandesh, SandeshSystem
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh.kube_manager import ttypes as sandesh

import kube.namespace_monitor as namespace_monitor
import kube.pod_monitor as pod_monitor
import kube.service_monitor as service_monitor
import kube.network_policy_monitor as network_policy_monitor
import discoveryclient.client as client
from common.kube_config_db import (NamespaceKM, ServiceKM,
                                   PodKM, NetworkPolicyKM)

def cgitb_error_log(monitor):
    string_buf = cStringIO.StringIO()
    cgitb_hook(file=string_buf, format="text")
    monitor.logger.log(string_buf.getvalue(), level=SandeshLevel.SYS_ERR)

class KubeNetworkManager(object):
    def __init__(self, args=None):
        self.args = args

        #
        # Initialize module parameters.
        #
        self._module = {}
        self._module["id"] = Module.KUBE_MANAGER
        self._module["name"] = ModuleNames[self._module["id"]]
        self._module["node_type"] = Module2NodeType[self._module["id"]]
        self._module["node_type_name"] = NodeTypeNames[self._module["node_type"]]
        self._module["hostname"] = socket.gethostname()
        self._module["table"] = "ObjectConfigNode"

        if self.args.worker_id:
            self._module["instance_id"] = self.args.worker_id
        else:
            self._module["instance_id"] = INSTANCE_ID_DEFAULT

        # Initialize discovery client
        self._disc = None
        if self.args.disc_server_ip and self.args.disc_server_port:
            self._module["discovery"] = client.DiscoveryClient(
                                        self.args.disc_server_ip,
                                        self.args.disc_server_port,
                                        self._module["name"])

        # Instantiate and initialize logger.
        self.logger = logger.KubeManagerLogger(
                             logger.KubeManagerLoggerParams(**self._module),
                             args)

        self.q = Queue()

        self.vnc = vnc_kubernetes.VncKubernetes(args=self.args,
            logger=self.logger, q=self.q)

        kube_api_connected = False
        while not kube_api_connected:
            try:
                self.namespace = namespace_monitor.NamespaceMonitor(
                    args=self.args, logger=self.logger, q=self.q,
                        namespace_db=NamespaceKM)
                self.pod = pod_monitor.PodMonitor(args=self.args,
                    logger=self.logger, q=self.q, pod_db=PodKM)
                self.service = service_monitor.ServiceMonitor(
                    args=self.args, logger=self.logger, q=self.q,
                        service_db=ServiceKM)
                self.network_policy =\
                    network_policy_monitor.NetworkPolicyMonitor(args=self.args,
                        logger=self.logger, q=self.q,
                            network_policy_db=NetworkPolicyKM)
                kube_api_connected = True
            except Exception as e:
                time.sleep(5)

    def start_tasks(self):

        self.logger.info("Starting all tasks.")

        gevent.joinall([
            gevent.spawn(self.vnc.vnc_process),
            gevent.spawn(self.namespace.namespace_callback),
            gevent.spawn(self.service.service_callback),
            gevent.spawn(self.pod.pod_callback),
            gevent.spawn(self.network_policy.network_policy_callback)
        ])

def parse_args():
    conf_parser = argparse.ArgumentParser(add_help=False)
    conf_parser.add_argument("-c", "--config-file", action='append',
        help="Specify config file", metavar="FILE")
    args, remaining_argv = conf_parser.parse_known_args(sys.argv)

    defaults = {
        'http_server_port': HttpPortKubemgr,
        'worker_id': '0',
        'sandesh_send_rate_limit': SandeshSystem.get_sandesh_send_rate_limit(),
        'collectors': None,
        'logger_class': None,
        'logging_conf': '',
        'log_local': False,
        'log_category': '',
        'use_syslog': False,
        'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
    }

    vnc_opts = {
        'rabbit_server': 'localhost',
        'rabbit_port': '5672',
        'rabbit_user': 'guest',
        'rabbit_password': 'guest',
        'rabbit_vhost': None,
        'rabbit_ha_mode': False,
        'rabbit_use_ssl': False,
        'kombu_ssl_version': '',
        'kombu_ssl_keyfile': '',
        'kombu_ssl_certfile': '',
        'kombu_ssl_ca_certs': '',
        'cassandra_user': None,
        'cassandra_password': None,
        'cluster_id': '',
    }
    k8s_opts = {}

    config = ConfigParser.SafeConfigParser()
    if args.config_file:
        config.read(args.config_file)
        if 'VNC' in config.sections():
            vnc_opts.update(dict(config.items("VNC")))
        if 'KUBERNETES' in config.sections():
            k8s_opts.update(dict(config.items("KUBERNETES")))
        if 'DEFAULTS' in config.sections():
            defaults.update(dict(config.items("DEFAULTS")))

    parser = argparse.ArgumentParser(
        parents=[conf_parser],
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    defaults.update(vnc_opts)
    defaults.update(k8s_opts)
    parser.set_defaults(**defaults)
    args = parser.parse_args()

    if type(args.cassandra_server_list) is str:
        args.cassandra_server_list = args.cassandra_server_list.split()
    if type(args.pod_subnets) is str:
        args.pod_subnets = args.pod_subnets.split()
    if type(args.service_subnets) is str:
        args.service_subnets = args.service_subnets.split()
    return args


def main():
    vnc_cgitb.enable(format='text')
    args = parse_args()
    kube_nw_mgr = KubeNetworkManager(args)
    kube_nw_mgr.start_tasks()


if __name__ == '__main__':
    main()
