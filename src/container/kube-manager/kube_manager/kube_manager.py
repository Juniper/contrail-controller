#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Kubernetes network manager.
"""

import random
import os
import sys
import time
import gc
import traceback
import greenlet

from gevent.queue import Queue
import gevent

from cfgm_common.vnc_amqp import VncAmqpHandle
from cfgm_common.zkclient import ZookeeperClient
from .common import logger as common_logger
from .common import args as kube_args
from .kube import (
    kube_monitor,
    namespace_monitor,
    pod_monitor,
    service_monitor,
    network_policy_monitor,
    endpoint_monitor,
    ingress_monitor,
    network_monitor
)
from .vnc import config_db
from .vnc import reaction_map
from .vnc import vnc_kubernetes
from .sandesh.kube_introspect import ttypes as introspect


class KubeNetworkManager(object):

    _kube_network_manager = None

    def __init__(self, args=None, logger=None, kube_api_connected=False,
                 queue=None, vnc_kubernetes_config_dict=None, is_master=True):

        self.q = queue if queue else Queue()
        self.args = args
        self.logger = logger
        self.is_master = is_master
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

                self.monitors['network'] = \
                    network_monitor.NetworkMonitor(
                        args=self.args, logger=self.logger, q=self.q)

                self.monitors['pod'] = pod_monitor.PodMonitor(
                    args=self.args, logger=self.logger, q=self.q)

                self.monitors['service'] = service_monitor.ServiceMonitor(
                    args=self.args, logger=self.logger, q=self.q)

                self.monitors['network_policy'] =\
                    network_policy_monitor.NetworkPolicyMonitor(
                        args=self.args, logger=self.logger, q=self.q)

                self.monitors['endpoint'] = \
                    endpoint_monitor.EndPointMonitor(
                        args=self.args, logger=self.logger, q=self.q)

                self.monitors['ingress'] = \
                    ingress_monitor.IngressMonitor(
                        args=self.args, logger=self.logger, q=self.q)

                kube_api_connected = True

            except Exception:  # FIXME: Except clause is too broad
                time.sleep(30)

        # Register all the known monitors.
        for monitor in list(self.monitors.values()):
            monitor.register_monitor()

        self.vnc = vnc_kubernetes.VncKubernetes(
            args=self.args, logger=self.logger, q=self.q, kube=self.kube,
            vnc_kubernetes_config_dict=vnc_kubernetes_config_dict)

    # end __init__

    def _kube_object_cache_enabled(self):
        return self.args.kube_object_cache == 'True'

    def launch_timer(self):
        self.logger.info("KubeNetworkManager - kube_timer_interval(%ss)"
                         % self.args.kube_timer_interval)
        time.sleep(int(self.args.kube_timer_interval))
        while True:
            gevent.sleep(int(self.args.kube_timer_interval))
            try:
                self.timer_callback()
            except Exception:  # FIXME: Except clause is too broad
                pass

    def timer_callback(self):
        self.vnc.vnc_timer()

    def start_tasks(self):
        self.logger.info("Starting all tasks.")
        self.greenlets = [gevent.spawn(self.vnc.vnc_process)]
        for monitor in list(self.monitors.values()):
            self.greenlets.append(gevent.spawn(monitor.event_callback))

        if not self.args.kube_timer_interval.isdigit():
            self.logger.emergency("set seconds for kube_timer_interval "
                                  "in contrail-kubernetes.conf. \
                                   example: kube_timer_interval=60")
            sys.exit()
        if int(self.args.kube_timer_interval) > 0:
            self.greenlets.append(gevent.spawn(self.launch_timer))

        gevent.joinall(self.greenlets)

    @staticmethod
    def reset():
        for cls in list(config_db.DBBaseKM.get_obj_type_map().values()):
            cls.reset()

    @classmethod
    def get_instance(cls):
        return cls._kube_network_manager

    @classmethod
    def destroy_instance(cls):
        inst = cls.get_instance()
        if inst is None:
            return
        inst.logger.sandesh_uninit()
        for gl in inst.greenlets:
            gl.kill()
        inst.greenlets = []
        inst.vnc.destroy_instance()
        inst.vnc = None
        inst.q = None
        KubeNetworkManager._kube_network_manager = None

    @classmethod
    def sandesh_handle_greenlet_stack_list_request(cls, request):
        greenlets = [introspect.KubeGreenletStackInstance(stack=stack)
                     for stack in [traceback.format_stack(ob.gr_frame)
                                   for ob in gc.get_objects()
                                   if isinstance(ob, greenlet.greenlet)]]
        resp = introspect.KubeGreenletStackListResp(greenlets=greenlets)
        resp.response(request.context())

    @classmethod
    def sandesh_handle_kube_api_connection_status_request(cls, request):
        statuses = {
            'endpoint_monitor': False,
            'ingress_monitor': False,
            'namespace_monitor': False,
            'network_policy_monitor': False,
            'pod_monitor': False,
            'service_monitor': False
        }

        kube_manager = cls.get_instance()
        if kube_manager is not None:
            for key, value in list(kube_manager.monitors.items()):
                statuses[key + '_monitor'] = value._is_kube_api_server_alive()

        response = introspect.KubeApiConnectionStatusResp(
            connections=introspect.KubeApiConnections(**statuses))
        response.response(request.context())

    @classmethod
    def sandesh_handle_mastership_status_request(cls, request):
        kube_manager = cls.get_instance()
        response = introspect.MastershipStatusResp(
            is_master=(kube_manager.is_master if kube_manager else False))
        response.response(request.context())


def run_kube_manager(km_logger, args, kube_api_skip, event_queue,
                     vnc_kubernetes_config_dict=None):
    km_logger.notice("Elected master kube-manager node. Initializing...")
    km_logger.introspect_init()
    kube_nw_mgr = KubeNetworkManager(
        args, km_logger,
        kube_api_connected=kube_api_skip,
        queue=event_queue,
        vnc_kubernetes_config_dict=vnc_kubernetes_config_dict,
        is_master=True)
    KubeNetworkManager._kube_network_manager = kube_nw_mgr

    # Register introspect handlers
    introspect.KubeGreenletStackList.handle_request = \
        KubeNetworkManager.sandesh_handle_greenlet_stack_list_request
    introspect.KubeApiConnectionStatus.handle_request =\
        KubeNetworkManager.sandesh_handle_kube_api_connection_status_request
    introspect.MastershipStatus.handle_request =\
        KubeNetworkManager.sandesh_handle_mastership_status_request

    kube_nw_mgr.start_tasks()


def main(args_str=None, kube_api_skip=False, event_queue=None,
         vnc_kubernetes_config_dict=None):
    _zookeeper_client = None

    args = kube_args.parse_args(args_str)

    client_pfx = ''
    zk_path_pfx = ''
    if args.cluster_id:
        client_pfx += args.cluster_id + '-'
        zk_path_pfx += args.cluster_id + '/'
    client_pfx += args.cluster_name + '-'
    zk_path_pfx += args.cluster_name + '/'

    # randomize collector list
    args.random_collectors = args.collectors
    if args.collectors:
        args.random_collectors = random.sample(args.collectors,
                                               len(args.collectors))

    km_logger = common_logger.KubeManagerLogger(args, http_server_port=-1)

    if args.nested_mode == '0':
        # Initialize AMQP handler then close it to be sure remain queue of a
        # precedent run is cleaned
        rabbitmq_cfg = kube_args.rabbitmq_args(args)
        try:
            vnc_amqp = VncAmqpHandle(
                km_logger._sandesh,
                km_logger,
                config_db.DBBaseKM,
                reaction_map.REACTION_MAP,
                client_pfx + 'kube_manager',
                rabbitmq_cfg,
                args.host_ip
            )
            vnc_amqp.establish()
            vnc_amqp.close()
        except Exception:  # FIXME: Except clause is too broad
            pass
        finally:
            km_logger.debug("Removed remained AMQP queue")

        # Ensure zookeeper is up and running before starting kube-manager
        _zookeeper_client = ZookeeperClient(client_pfx + "kube-manager",
                                            args.zk_server_ip, args.host_ip)

        km_logger.notice("Waiting to be elected as master...")
        _zookeeper_client.master_election(
            zk_path_pfx + "kube-manager",
            os.getpid(),
            run_kube_manager,
            km_logger,
            args,
            kube_api_skip,
            event_queue,
            vnc_kubernetes_config_dict)

    else:  # nested mode, skip zookeeper mastership check
        run_kube_manager(km_logger, args, kube_api_skip, event_queue,
                         vnc_kubernetes_config_dict)


if __name__ == '__main__':
    main()
