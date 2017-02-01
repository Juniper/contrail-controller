#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Kubernetes network manager
"""

import gevent
from gevent.queue import Queue

from vnc_api.vnc_api import *

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
    def __init__(self, args=None):
        self.args = args
        if 'kube_timer_interval' not in self.args:
            self.args.kube_timer_interval = '60'
        self.logger = logger.KubeManagerLogger(args)
        self.q = Queue()
        # All monitors supported by this manager.
        self.monitors = {}
        kube_api_connected = False
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
                time.sleep(5)

        # Register all the known monitors.
        for monitor in self.monitors.values():
            monitor.register_monitor()

        self.vnc = vnc_kubernetes.VncKubernetes(args=self.args,
            logger=self.logger, q=self.q, kube=self.kube)

    def _kube_object_cache_enabled(self):
        return True if self.args.kube_object_cache == 'True' else False;

    def launch_timer(self):
        if not self.args.kube_timer_interval.isdigit():
            self.logger.emergency("set seconds for kube_timer_interval "
                                  "in contrail-kubernetes.conf. \
                                   example: kube_timer_interval=60")
            sys.exit()
        self.logger.notice("kube_timer_interval set to %s seconds" %
                            self.args.kube_timer_interval)
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
        greenlets = [gevent.spawn(self.vnc.vnc_process)]
        for monitor in self.monitors.values():
            greenlets.append(gevent.spawn(monitor.event_callback))
        greenlets.append(gevent.spawn(self.launch_timer))
        gevent.joinall(greenlets)

def main():
    args = kube_args.parse_args()
    kube_nw_mgr = KubeNetworkManager(args)
    kube_nw_mgr.start_tasks()

if __name__ == '__main__':
    main()
