#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
VNC endpoints management for kubernetes
"""

import uuid

from vnc_api.vnc_api import *
from config_db import *

class VncEndpoints(object):

    def __init__(self, vnc_lib=None, label_cache=None):
        self._vnc_lib = vnc_lib

    def vnc_endpoint_add(self, uid, name, namespace, event):
        pass

    def vnc_endpoint_delete(self, uid, name):
        pass

    def process(self, event):
        uid = event['object']['metadata'].get('uid')
        name = event['object']['metadata'].get('name')
        namespace = event['object']['metadata'].get('namespace')

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
            self.vnc_endpoint_add(uid, name, namespace, event)
        elif event['type'] == 'DELETED':
            self.vnc_endpoint_delete(uid, name)
