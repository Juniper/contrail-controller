"""
Monitors the namespace created by daemon_start.
"""

import sys
import socket
import argparse

import gevent
import gevent.lock
import gevent.monkey

gevent.monkey.patch_all()

from vrouter_control import interface_register
from contrail_vrouter_api.vrouter_api import ContrailVRouterApi


INTERVAL = 10

class NetnsMonitor(object):
    def __init__(self, project, vm_obj, vmi_obj, ifname, **kwargs):
        self.project = project
        self.vm = vm_obj
        self.vmi = vmi_obj
        self.ifname = ifname
        self.vmi_out = kwargs.get('vmi_out', None)
        self.ifname_out = kwargs.get('ifname_out', None)

        vrouter_semaphore = gevent.lock.Semaphore()
        self.vrouter_client = ContrailVRouterApi(doconnect=True,
                                                 semaphore=vrouter_semaphore)
        self.vrouter_client.connect()
        self.vrouter_agent_connection = self.vrouter_client._client

    def keepalive(self):
         self.vrouter_client.periodic_connection_check()
         current_agent_connection = self.vrouter_client._client
         if not current_agent_connection:
             # vrouter agent is down, Try checking...
             return
         if self.vrouter_agent_connection != current_agent_connection:
             # Initial connection object to vrouter agent is different from
             # the current object, vrouter agent is restarted, plug the netns
             # vif's again.
             interface_register(self.vm, self.vmi, self.ifname,
                                project=self.project)
             if self.vmi_out:
                 interface_register(self.vm, self.vmi_out, self.ifname_out,
                                    project=self.project)
             self.vrouter_agent_connection = current_agent_connection

    def monitor(self):
        while True:
            gevent.sleep(INTERVAL)
            self.keepalive()
