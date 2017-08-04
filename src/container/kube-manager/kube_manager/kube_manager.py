#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Kubernetes network manager
"""

import gevent
import random
from gevent.queue import Queue
from vnc.config_db import *
from vnc.reaction_map import REACTION_MAP

from vnc_api.vnc_api import *

from cfgm_common.vnc_amqp import VncAmqpHandle
from cfgm_common.zkclient import ZookeeperClient
import common.logger as logger
import common.args as kube_args
import vnc.vnc_kubernetes as vnc_kubernetes
import kube.kube_monitor as kube_monitor
import kube.namespace_monitor as namespace_monitor
import kube.pod_monitor as pod_monitor
import kube.service_monitor as service_monitor
import kube.network_policy_monitor as network_policy_monitor
import kube.endpoint_monitor as endpoint_monitor
import kube.ingress_monitor as ingress_monitor

class KubeNetworkManager(object):

    _kube_network_manager = None

    def __init__(self, args=None, logger=None, kube_api_connected=False, queue=None):
        if queue:
            self.q = queue
        else:
            self.q = Queue()
        self.args = args
        self.logger = logger
        # All monitors supported by this manager.
        self.monitors = {}
        self.kube = None
        self.greenlets = []
        while not kube_api_connected:
            try:
                self.kube = kube_monitor.KubeMonitor(
                    args=self.args, logger=self.logger, q=self.q)

                self.monitors['namespace'] = namespace_monitor.NamespaceMonitor(
                    args=self.args, logger=self.logger, q=self.q)

                self.monitors['pod'] = pod_monitor.PodMonitor(args=self.args,
                    logger=self.logger, q=self.q)

                self.monitors['service'] = service_monitor.ServiceMonitor(
                    args=self.args, logger=self.logger, q=self.q)

                self.monitors['network_policy'] =\
                    network_policy_monitor.NetworkPolicyMonitor(args=self.args,
                        logger=self.logger, q=self.q)

                self.monitors['endpoint'] = \
                    endpoint_monitor.EndPointMonitor(args=self.args,
                        logger=self.logger, q=self.q)

                self.monitors['ingress'] = \
                    ingress_monitor.IngressMonitor(args=self.args,
                        logger=self.logger, q=self.q)

                kube_api_connected = True

            except Exception as e:
                time.sleep(30)

        # Register all the known monitors.
        for monitor in self.monitors.values():
            monitor.register_monitor()

        self.vnc = vnc_kubernetes.VncKubernetes(args=self.args,
            logger=self.logger, q=self.q, kube=self.kube)
    # end __init__

    def _kube_object_cache_enabled(self):
        return True if self.args.kube_object_cache == 'True' else False;

    def launch_timer(self):
        if not self.args.kube_timer_interval.isdigit():
            self.logger.emergency("set seconds for kube_timer_interval "
                                  "in contrail-kubernetes.conf. \
                                   example: kube_timer_interval=60")
            sys.exit()
        self.logger.info("KubeNetworkManager - kube_timer_interval(%ss)"
                          %self.args.kube_timer_interval)
        time.sleep(int(self.args.kube_timer_interval))
        while True:
            gevent.sleep(int(self.args.kube_timer_interval))
            try:
                self.timer_callback()
            except Exception:
                pass

    def timer_callback(self):
        self.vnc.vnc_timer()

    def start_tasks(self):
        self.logger.info("Starting all tasks.")
        self.greenlets = [gevent.spawn(self.vnc.vnc_process)]
        for monitor in self.monitors.values():
            self.greenlets.append(gevent.spawn(monitor.event_callback))
        self.greenlets.append(gevent.spawn(self.launch_timer))
        gevent.joinall(self.greenlets)

    def reset(self):
        for cls in DBBaseKM.get_obj_type_map().values():
            cls.reset()

    @classmethod
    def get_instance(cls):
        return KubeNetworkManager._kube_network_manager

    @classmethod
    def destroy_instance(cls):
        inst = cls.get_instance()
        if inst is None:
            return
        inst.logger.sandesh_uninit()
        for greenlet in inst.greenlets:
            greenlet.kill()
        inst.greenlets = []
        inst.vnc.destroy_instance()
        inst.vnc = None
        inst.q = None
        KubeNetworkManager._kube_network_manager = None

def run_kube_manager(km_logger, args, kube_api_skip, event_queue):
    kube_nw_mgr = KubeNetworkManager(args, km_logger, kube_api_connected=kube_api_skip, queue=event_queue)
    KubeNetworkManager._kube_network_manager = kube_nw_mgr
    kube_nw_mgr.start_tasks()

def main(args_str=None, kube_api_skip=False, event_queue=None):
    _zookeeper_client = None

    args = kube_args.parse_args(args_str)
    if 'kube_timer_interval' not in args:
        args.kube_timer_interval = '60'

    if args.cluster_id:
        client_pfx = args.cluster_id + '-'
        zk_path_pfx = args.cluster_id + '/'
    else:
        client_pfx = ''
        zk_path_pfx = ''

    # randomize collector list
    args.random_collectors = args.collectors
    if args.collectors:
        args.random_collectors = random.sample(args.collectors,
                                           len(args.collectors))

    km_logger = logger.KubeManagerLogger(args)

    if args.nested_mode == '0':
        # Initialize AMQP handler then close it to be sure remain queue of a
        # precedent run is cleaned
        rabbitmq_cfg = kube_args.rabbitmq_args(args)
        try:
            vnc_amqp = VncAmqpHandle(km_logger._sandesh, km_logger, DBBaseKM,
                                     REACTION_MAP, 'kube_manager',
                                     rabbitmq_cfg)
            vnc_amqp.establish()
            vnc_amqp.close()
        except Exception:
            pass
        finally:
            km_logger.debug("Removed remained AMQP queue")
 
        # Ensure zookeeper is up and running before starting kube-manager
        _zookeeper_client = ZookeeperClient(client_pfx+"kube-manager",
                                            args.zk_server_ip)

        km_logger.notice("Waiting to be elected as master...")
        _zookeeper_client.master_election(zk_path_pfx+"/kube-manager",
                                          os.getpid(), run_kube_manager,
                                          km_logger, args, kube_api_skip, 
                                          event_queue)
        km_logger.notice("Elected master kube-manager node. Initializing...")

    else: #nested mode, skip zookeeper mastership check
        run_kube_manager(km_logger, args, kube_api_skip, event_queue)
 
if __name__ == '__main__':
    main()
