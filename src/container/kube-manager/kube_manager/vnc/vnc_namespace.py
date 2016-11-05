#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
VNC service management for kubernetes
"""

from vnc_api.vnc_api import *
from config_db import *

class VncNamespace(object):

    def __init__(self, vnc_lib=None):
        self._vnc_lib = vnc_lib

    def vnc_namespace_add(self, name):
        proj_fq_name = ['default-domain', name]
        proj_obj = Project(name=name, fq_name=proj_fq_name)
        try:
            self._vnc_lib.project_create(proj_obj)
        except RefsExistError:
            proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        ProjectKM.locate(proj_obj.uuid)
        return proj_obj

    def vnc_namespace_delete(self, name):
        try:
            self._vnc_lib.project_delete(fq_name=['default-domain', name])
        except NoIdError:
            pass

    def process(self, event):
        name = event['object']['metadata'].get('name')

        if event['type'] == 'ADDED':
            self.vnc_namespace_add(name)
        elif event['type'] == 'DELETED':
            self.vnc_namespace_delete(name)
