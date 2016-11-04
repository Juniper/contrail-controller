#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
Kubernetes network manager
"""

import sys
sys.path.insert(0, '/root/kube')
sys.path.insert(0, '/usr/lib/python2.7/dist-packages')

import gevent
from gevent.queue import Queue

import argparse
import cgitb

from vnc_api.vnc_api import *
import vnc.vnc_kubernetes as vnc_kubernetes
import common.logger as logger

import kube.namespace_monitor as namespace_monitor
import kube.pod_monitor as pod_monitor
import kube.service_monitor as service_monitor
import kube.network_policy_monitor as network_policy_monitor

def cgitb_error_log(monitor):
    string_buf = cStringIO.StringIO()
    cgitb_hook(file=string_buf, format="text")
    monitor.logger.log(string_buf.getvalue(), level=SandeshLevel.SYS_ERR)

class KubeNetworkManager(object):
    def __init__(self, args=None):
        self.args = args
        self.logger = logger.Logger(args)
        self.q = Queue()
        self.vnc = vnc_kubernetes.VncKubernetes(args=self.args,
            logger=self.logger, q=self.q)
        self.namespace = namespace_monitor.NamespaceMonitor(args=self.args,
            logger=self.logger, q=self.q)
        self.pod = pod_monitor.PodMonitor(args=self.args,
            logger=self.logger, q=self.q)
        self.service = service_monitor.ServiceMonitor(args=self.args,
            logger=self.logger, q=self.q)
        self.network_policy = network_policy_monitor.NetworkPolicyMonitor(
            args=self.args, logger=self.logger, q=self.q)

    def start_tasks(self):
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
        'disc_server_ip': 'localhost',
        'disc_server_port': '5998',
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
    cgitb.enable(format='text')
    args = parse_args()
    kube_nw_mgr = KubeNetworkManager(args)
    kube_nw_mgr.start_tasks()


if __name__ == '__main__':
    main()
