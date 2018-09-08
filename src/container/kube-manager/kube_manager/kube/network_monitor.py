#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import json
import gevent
from kube_monitor import KubeMonitor
from kube_manager.common.kube_config_db import NetworkKM

class NetworkMonitor(KubeMonitor):

    def __init__(self, args=None, logger=None, q=None, network_policy_db=None):
        super(NetworkMonitor, self).__init__(args, logger, q,
            NetworkKM, resource_name='network-attachment-definitions',
            api_group='apis/k8s.cni.cncf.io', api_version='v1')

        # Check if Network CustomResourceDefinition is already created,
        # If not create it internally
        (crd_info) = self.get_resource(resource_type= \
                "customresourcedefinitions", resource_name='',
                api_group="apis/apiextensions.k8s.io", api_version="v1beta1")
        if crd_info is None:
            self.logger.error("%s - Could not get CRD list.  CRD INFO = %s"
                  %(self.name, crd_info))
            return
        else:
            current_crds = [x['metadata']['name'].lower() \
                                            for x in crd_info['items']]
            self.logger.debug("%s - Retrieved following CRD list = %s"
                  %(self.name, current_crds))
            if 'network-attachment-definitions.k8s.cni.cncf.io' \
                                                    not in current_crds:
                # Create Networks CRD - networks.kubernetes.cni.cncf.io
                self.logger.debug("%s - Creating Network CRD" %(self.name))
                network_crd_body = self.create_network_crd_yaml()
                self.post_resource(
                    resource_type="customresourcedefinitions",
                    resource_name='', api_group="apis/apiextensions.k8s.io",
                    api_version="v1beta1", body_params=network_crd_body)

        self.init_monitor()
        self.logger.info("NetworkMonitor init done.");

    def create_network_crd_yaml(self):
        network_crd_dict = dict(
            apiVersion = 'apiextensions.k8s.io/v1beta1',
            kind = 'CustomResourceDefinition',
            metadata = dict(
                name = 'network-attachment-definitions.k8s.cni.cncf.io'
            ),
            spec = dict(
                group = 'k8s.cni.cncf.io',
                version = 'v1',
                scope = 'Namespaced',
                names = dict(
                    plural = 'network-attachment-definitions',
                    singular = 'network-attachment-definition',
                    kind = 'NetworkAttachmentDefinition',
                    shortNames = ['net-attach-def']
                )
            )
        )
        
        return network_crd_dict

    def process_event(self, event):
        nw_data = event['object']
        event_type = event['type']
        kind = event['object'].get('kind')
        namespace = event['object']['metadata'].get('namespace')
        name = event['object']['metadata'].get('name')

        if self.db:
            nw_uuid = self.db.get_uuid(nw_data)
            if event_type != 'DELETED':
                # Update Network DB.
                nw = self.db.locate(nw_uuid)
                nw.update(nw_data)
            else:
                # Remove the entry from Network DB.
                self.db.delete(nw_uuid)
        else:
            nw_uuid = event['object']['metadata'].get('uid')

        print("%s - Got %s %s %s:%s:%s"
              %(self.name, event_type, kind, namespace, name, nw_uuid))
        self.logger.debug("%s - Got %s %s %s:%s:%s"
              %(self.name, event_type, kind, namespace, name, nw_uuid))
        self.q.put(event)

    def event_callback(self):
        while True:
            self.process()
            gevent.sleep(0)
