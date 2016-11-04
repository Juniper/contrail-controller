#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
VNC service management for kubernetes
"""

from vnc_api.vnc_api import *
from config_db import *

class VncNetworkPolicy(object):

    def __init__(self, vnc_lib=None):
        self._vnc_lib = vnc_lib

    def vnc_network_policy_add(self, name):
        pass

    def vnc_network_policy_delete(self, name):
        pass

    def process(self, event):
        name = event['object']['metadata'].get('name')

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
            self.vnc_network_policy_add(name)
        elif event['type'] == 'DELETED':
            self.vnc_network_policy_delete(name)
