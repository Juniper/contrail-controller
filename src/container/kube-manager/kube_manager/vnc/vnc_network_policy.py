#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
VNC service management for kubernetes
"""

from vnc_api.vnc_api import *
from config_db import *

class VncNetworkPolicy(object):

    def __init__(self, vnc_lib=None, label_cache=None):
        self._vnc_lib = vnc_lib
        self._label_cache = label_cache

    def process(self, event):
        name = event['object']['metadata'].get('name')
        project = event['object']['metadata'].get('namespace')
        podSelector = event['object']['spec'].get('podSelector', None)

        print("Put %s %s %s:%s:%s\ningress %s \n podSelector %s" % (event['type'],
                                                                    event['object'].get('kind'),
                                                                    project,
                                                                    name,
                                                                    event['object']['metadata'].get('uid'),
                                                                    event['object']['spec'].get('ingress'),
                                                                    podSelector))

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
            self.vnc_network_policy_add(name, project, podSelector['matchLabels'])
        elif event['type'] == 'DELETED':
            self.vnc_network_policy_delete(name, project)

    def vnc_network_policy_add(self, name, project, labels = None, domain = 'default-domain'):
        sg = self.security_group_create(name, project, domain)
        pods = self.select_pods(labels)
        self.apply_sg_2_pods(sg, pods)

    def vnc_network_policy_delete(self, name, project, domain = 'default-domain'):
        try:
            self._vnc_lib.security_group_delete(fq_name=[domain, project, name])
        except NoIdError:
            pass

    def security_group_create(self, name, project, domain = 'default-domain'):
        proj = self._vnc_lib.project_read(fq_name=[domain, project])
        sg = SecurityGroup(name = name, parent_obj = proj)

        try:
            self._vnc_lib.security_group_create(sg)
        except RefsExistError:
            sg = self._vnc_lib.security_group_read(fq_name=[domain, project, name])
        SecurityGroupKM.locate(sg.uuid)
        return sg

    def select_pods(self, labels):
        result = list()
        for label in labels.items():
            key = self._label_cache._get_key(label)
            pod_ids = self._label_cache.pod_label_cache.get(key, [])
            result.append(pod_ids)
        return result

    def apply_sg_2_pods(self, sg, pods):
        for pod in pods:
            pass
