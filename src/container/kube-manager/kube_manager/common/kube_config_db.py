#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Database for Kubernetes objects.
"""
from __future__ import absolute_import

from builtins import str
import json

from cfgm_common.vnc_db import DBBase
from kube_manager.sandesh.kube_introspect import ttypes as introspect
from ast import literal_eval
from .utils import (get_vn_fq_name_from_dict_string, get_dict_from_dict_string)
from .utils import get_fip_pool_fq_name_from_dict_string

class KubeDBBase(DBBase):
    obj_type = __name__

    @classmethod
    def evaluate(self):
        pass

    @staticmethod
    def get_uuid(obj):
        """ Get UUID of the kubernetes object."""
        if obj:
            return obj.get('metadata').get('uid')
        return None

    def get_vn_from_annotation(self, annotations):
        """ Get vn-fq-name if specified in annotations of a k8s object.
        """
        vn_ann = annotations.get('opencontrail.org/network', None)
        if vn_ann:
            return get_vn_fq_name_from_dict_string(vn_ann)
        return None

    def get_fip_pool_from_annotation(self, annotations):
        """ Get fip-pool-fq-name if specified in annotations of a k8s object.
        """
        fip_pool_ann = annotations.get('opencontrail.org/fip-pool', None)
        if fip_pool_ann:
            return get_fip_pool_fq_name_from_dict_string(fip_pool_ann)
        return None

#
# Kubernetes POD Object DB.
#
class PodKM(KubeDBBase):
    _dict = {}
    obj_type = 'Pod'

    def __init__(self, uuid, obj = None, request_id=None):
        self.uuid = uuid

        # Metadata.
        self.name = None
        self.namespace = None
        self.labels = {}
        self.annotations = None
        self.pod_vn_fq_name = None
        self.networks = []

        # Spec.
        self.nodename = None
        self.host_ip = None

        # Status.
        self.phase = None

        # If an object is provided, update self with contents of object.
        if obj:
            self.update(obj)

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self._update_metadata(obj.get('metadata'))
        self._update_spec(obj.get('spec'))
        self._update_status(obj.get('status'))

    def _update_metadata(self, md):
        if md is None:
            return
        self.name = md.get('name')
        self.namespace = md.get('namespace')
        self.labels = md.get('labels')
        self.annotations = md.get('annotations', None)
        self._parse_annotations(self.annotations)

    def _update_spec(self, spec):
        if spec is None:
            return
        self.nodename = spec.get('nodeName')

    def _update_status(self, status):
        if status is None:
            return
        self.host_ip = status.get('hostIP')
        self.phase = status.get('phase')

    def get_host_ip(self):
        return self.host_ip

    @staticmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Pod DB lookup/introspect request. """
        pod_resp = introspect.PodDatabaseListResp(pods=[])

        # Iterate through all elements of Pod DB.
        for pod in list(PodKM.values()):

            # If the request is for a specific entry, then locate the entry.
            if req.pod_uuid and req.pod_uuid != pod.uuid:
                continue

            # Construct response for an element.
            pod_instance = introspect.PodInstance(uuid=pod.uuid, name=pod.name,
                labels=pod.labels, nodename=pod.nodename, ip=pod.host_ip,
                phase=pod.phase)

            # Append the constructed element info to the response.
            pod_resp.pods.append(pod_instance)

        # Send the reply out.
        pod_resp.response(req.context())

    def _parse_annotations(self, annotations):
        if not annotations:
            return

        # Parse pod network annotations.
        if not self.pod_vn_fq_name:
            try:
                self.pod_vn_fq_name = self.get_vn_from_annotation(
                    annotations)
            except Exception as e:
                err_msg = "Failed to parse annotation for pod[%s].Error[%s]"%\
                    (self.name, str(e))
                raise Exception(err_msg)

        # Parse virtual network annotations.
        if 'k8s.v1.cni.cncf.io/networks' in annotations:
            self.networks = []
            if str(annotations['k8s.v1.cni.cncf.io/networks'])[:1] == '[':
                interfaceList = []
                networks_string_list = json.loads(
                            str(annotations['k8s.v1.cni.cncf.io/networks']))
                networkAnnotationWhiteList = {"namespace", "name", "interface"}
                duplicateInterfaces = False
                for network in networks_string_list:
                    if "interface" in network:
                        interfaceList.append(network["interface"])
                    network_tmp = {}
                    for element in network:
                        if element in networkAnnotationWhiteList:
                            network_tmp[element] = network[element]
                    network_tmp['network'] = network_tmp.pop('name')
                    self.networks.append(network_tmp)

                duplicateInterfaceCount = [i for i,
                    x in enumerate(interfaceList)
                    if interfaceList.count(x) > 1]
                if len(duplicateInterfaceCount) > 0:
                    err_msg = "Interface %s is defined more than once"%\
                        interfaceList[duplicateInterfaceCount[0]]
                    raise Exception(err_msg)
            else:
                networks_list = annotations['k8s.v1.cni.cncf.io/networks'].split(',')
                for network in networks_list:
                    if '/' in network:
                        network_namespace, network_name = network.split('/')
                        network_dict = {'network':network_name, 'namespace': network_namespace}
                    else:
                        network_name = network
                        network_dict = {'network':network_name}
                    self.networks.append(network_dict)

    def get_vn_fq_name(self):
        """Return virtual-network fq-name annotated on this pod."""
        return self.pod_vn_fq_name

    @classmethod
    def get_namespace_pods(cls, namespace):
        """ Return a list of pods from a namespace. """
        pod_uuids = []
        for pod_uuid, pod in cls._dict.items():
            if pod.namespace == namespace:
                pod_uuids.append(pod_uuid)
        return pod_uuids

#
# Kubernetes Namespace Object DB.
#
class NamespaceKM(KubeDBBase):
    _dict = {}
    obj_type = 'Namespace'

    def __init__(self, uuid, obj = None, request_id=None):
        self.uuid = uuid

        # Metadata.
        self.name = None
        self.labels = {}
        self.added_labels = {}
        self.removed_labels = {}
        self.isolated_vn_fq_name = None
        self.isolated_pod_vn_fq_name = None
        self.isolated_service_vn_fq_name = None
        self.annotated_vn_fq_name = None
        self.annotations = None
        self.np_annotations = None
        self.ip_fabric_forwarding = None
        self.ip_fabric_snat = None
        self.annotated_ns_fip_pool_fq_name = None

        # Status.
        self.phase = None

        # Config cache.
        self.isolated = False
        self.firewall_ingress_allow_rule_uuid = None
        self.firewall_egress_allow_rule_uuid = None

        # If an object is provided, update self with contents of object.
        if obj:
            self.update(obj)

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self._update_metadata(obj.get('metadata'))
        self._update_status(obj.get('status'))

    def _update_metadata(self, md):
        if md is None:
            return
        self.name = md.get('name')
        self.update_labels(md.get('labels'))

        # Parse annotations on this namespace.
        self.annotations = md.get('annotations')
        self._parse_annotations(self.annotations)

    def _parse_annotations(self, annotations):
        self.np_annotations = None
        if not annotations:
            return

        # Parse virtual network annotations.
        if 'opencontrail.org/network' in annotations:
            try:
                self.annotated_vn_fq_name = self.get_vn_from_annotation(
                    annotations)
            except Exception as e:
                err_msg = "Failed to parse annotations for namespace [%s]."\
                " Error[%s]" % (self.name, str(e))
                raise Exception(err_msg)

        # Cache namespace isolation directive.
        if 'opencontrail.org/isolation' in annotations:
            ann_val = annotations['opencontrail.org/isolation']
            if ann_val.lower() == 'true':
                # Namespace isolation is configured
                self.isolated = True
        # Cache namespace ip_fabric_forwarding directive
        if 'opencontrail.org/ip_fabric_forwarding' in annotations:
            ann_val = annotations['opencontrail.org/ip_fabric_forwarding']
            if ann_val.lower() == 'true':
                self.ip_fabric_forwarding = True
            elif ann_val.lower() == 'false':
                self.ip_fabric_forwarding = False
        else:
            self.ip_fabric_forwarding = None
        # Cache namespace ip_fabric_snat directive
        if 'opencontrail.org/ip_fabric_snat' in annotations:
            ann_val = annotations['opencontrail.org/ip_fabric_snat']
            if ann_val.lower() == 'true':
                self.ip_fabric_snat = True
            elif ann_val.lower() == 'false':
                self.ip_fabric_snat = False
        else:
            self.ip_fabric_snat = None
        # Cache k8s network-policy directive.
        if 'net.beta.kubernetes.io/network-policy' in annotations:
            self.np_annotations = json.loads(
                annotations['net.beta.kubernetes.io/network-policy'])

        # Parse fip pool annotations.
        if 'opencontrail.org/fip-pool' in annotations:
            try:
                self.annotated_ns_fip_pool_fq_name = self.get_fip_pool_from_annotation(
                    annotations)
            except Exception as e:
                err_msg = "Failed to parse annotations for fip-pool [%s]."\
                " Error[%s]" % (self.name, str(e))
                raise Exception(err_msg)

    def _update_status(self, status):
        if status is None:
            return
        self.phase = status.get('phase')

    def is_isolated(self):
        return self.isolated

    def get_ip_fabric_forwarding(self):
        return self.ip_fabric_forwarding

    def get_ip_fabric_snat(self):
        return self.ip_fabric_snat

    def get_network_policy_annotations(self):
        return self.np_annotations

    def set_isolated_network_fq_name(self, fq_name):
        self.isolated_vn_fq_name = fq_name

    def get_isolated_network_fq_name(self):
        return self.isolated_vn_fq_name

    def set_isolated_pod_network_fq_name(self, fq_name):
        self.isolated_pod_vn_fq_name = fq_name

    def get_isolated_pod_network_fq_name(self):
        return self.isolated_pod_vn_fq_name

    def set_isolated_service_network_fq_name(self, fq_name):
        self.isolated_service_vn_fq_name = fq_name

    def get_isolated_service_network_fq_name(self):
        return self.isolated_service_vn_fq_name

    def get_annotated_network_fq_name(self):
        return self.annotated_vn_fq_name

    def get_annotated_ns_fip_pool_fq_name(self):
        return self.annotated_ns_fip_pool_fq_name

    @staticmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Namespace DB lookup/introspect request. """
        ns_resp = introspect.NamespaceDatabaseListResp(namespaces=[])

        # Iterate through all elements of Namespace DB.
        for ns in list(NamespaceKM.values()):

            # If the request is for a specific entry, then locate the entry.
            if req.namespace_uuid and req.namespace_uuid != ns.uuid:
                continue

            # Construct response for a namespace element.
            ns_instance = introspect.NamespaceInstance(uuid=ns.uuid,
                            labels=ns.labels, name=ns.name,
                            phase=ns.phase, isolated=ns.isolated)

            # Append the constructed element info to the response.
            ns_resp.namespaces.append(ns_instance)

        # Send the reply out.
        ns_resp.response(req.context())

    def update_labels(self, labels):
        """
        Update labels.
        If this update removes or add new labels to a previous list
        cache the diff for futher processing.
        """
        self.added_labels = {}
        self.removed_labels = {}
        new_labels = labels if labels else {}

        for k,v in new_labels.items():
            if k not in self.labels or v != self.labels[k]:
                self.added_labels[k] = v

        for k,v in self.labels.items():
            if k not in new_labels or v != new_labels[k]:
                self.removed_labels[k] = v

        # Finally update this namespace's labels.
        self.labels = new_labels

    def get_changed_labels(self):
        """ Return labels changed by the last update. """
        return self.added_labels, self.removed_labels
#
# Kubernetes Service Object DB.
#
class ServiceKM(KubeDBBase):
    _dict = {}
    obj_type = 'Service'

    def __init__(self, uuid, obj = None, request_id=None):
        self.uuid = uuid

        # Metadata.
        self.name = None
        self.namespace = None
        self.labels = {}

        # Spec.
        self.cluster_ip = None
        self.service_type = None

        #
        # Run time Config.
        #
        self.firewall_rule_uuid = None

        # If an object is provided, update self with contents of object.
        if obj:
            self.update(obj)

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self._update_metadata(obj.get('metadata'))
        self._update_spec(obj.get('spec'))

    def _update_metadata(self, md):
        if md is None:
            return
        self.name = md.get('name')
        self.namespace = md.get('namespace')
        self.labels = md.get('labels', {})

    def _update_spec(self, spec):
        if spec is None:
            return
        self.service_type= spec.get('type')
        self.cluster_ip = spec.get('clusterIP')
        self.ports = spec.get('ports')

    def get_service_ip(self):
        return self.cluster_ip

    def get_service_ports(self):
        return self.ports

    @staticmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Service DB lookup/introspect request. """
        svc_resp = introspect.ServiceDatabaseListResp(services=[])

        # Iterate through all elements of Pod DB.
        for svc in list(ServiceKM.values()):

            # If the request is for a specific entry, then locate the entry.
            if req.service_uuid and req.service_uuid != svc.uuid:
                continue

            # Construct response for an element.
            svc_instance = introspect.ServiceInstance(uuid=svc.uuid,
                name=svc.name, name_space=svc.namespace, labels=svc.labels,
                cluster_ip=svc.cluster_ip, service_type=svc.service_type)

            # Append the constructed element info to the response.
            svc_resp.services.append(svc_instance)

        # Send the reply out.
        svc_resp.response(req.context())

#
# Kubernetes Network Policy Object DB.
#
class NetworkPolicyKM(KubeDBBase):
    _dict = {}
    obj_type = 'NetworkPolicy'

    # List to track sequence in which network policy is created.
    #
    # This is useful during periodic evaluations to track and fix
    # inconsistencies between k8s and contrail databases.
    create_sequence = []

    def __init__(self, uuid, obj = None, request_id=None):
        self.uuid = uuid
        # Metadata.
        self.name = None
        self.namespace = None
        self.vnc_fq_name = None
        self.add_entry()

        # Spec.
        self.spec = {}

        # If an object is provided, update self with contents of object.
        if obj:
            self.update(obj)

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self._update_metadata(obj.get('metadata'))
        self._update_spec(obj.get('spec'))

    def _update_metadata(self, md):
        if md is None:
            return
        self.name = md.get('name')
        self.namespace = md.get('namespace')

    def _update_spec(self, spec):
        if spec is None:
            return
        self.spec = spec

    def get_pod_selector(self, pod_selector):
        labels = pod_selector.get('matchLabels') \
            if pod_selector.get('matchLabels') else {}
        return introspect.NetworkPolicyLabelSelectors(matchLabels=labels)

    def get_namespace_selector(self, ns_selector):
        labels = ns_selector.get('matchLabels') \
            if ns_selector.get('matchLabels') else {}
        return introspect.NetworkPolicyLabelSelectors(matchLabels=labels)

    def set_vnc_fq_name(self, fqname):
        self.vnc_fq_name = fqname

    def get_vnc_fq_name(self):
        return self.vnc_fq_name

    def add_entry(self):
        # Add if entry not already present.
        # This handled duplicate add/mod requests.
        if self.uuid not in self.create_sequence:
            self.create_sequence.append(self.uuid)

    def remove_entry(self):
        """
        Handler for pre-delete processing of network policy delete events.
        """
        # Remove if entry is present.
        if self.uuid in self.create_sequence:
            self.create_sequence.remove(self.uuid)

    @classmethod
    def get_configured_policies(cls):
        return list(cls.create_sequence)

    @staticmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Network Policy DB lookup/introspect request. """
        np_resp = introspect.NetworkPolicyDatabaseListResp(network_policies=[])

        # Iterate through all elements of Network Policy DB.
        for np in list(NetworkPolicyKM.values()):

            # If the request is for a specific entry, then locate the entry.
            if req.network_policy_uuid and req.network_policy_uuid != np.uuid:
                continue
            # Parse "ingress" attribute.
            np_ingress_list = []
            if np.spec.get('ingress'):
                for ingress in np.spec.get('ingress'):
                    # Parse "from" attribute.
                    from_list = []
                    if ingress.get('from'):
                        for each_from in ingress.get('from'):

                            np_ns_selector = None
                            if each_from.get('namespaceSelector'):
                                np_ns_selector = np.get_namespace_selector(
                                    each_from.get('namespaceSelector'))

                            np_pod_selector = None
                            if each_from.get('podSelector'):
                                np_pod_selector = np.get_pod_selector(
                                    each_from.get('podSelector'))

                            np_ip_block = None
                            if each_from.get('ipBlock'):
                                ipblock = each_from.get('ipBlock')
                                cidr = ipblock.get('cidr', None)
                                except_cidr_list = []
                                for except_cidr in ipblock.get('except', []):
                                    except_cidr_list.append(except_cidr)
                                np_ip_block = introspect.NetworkPolicyIpBlock(
                                    cidr=cidr, except_cidr = except_cidr_list)

                            from_list.append(introspect.NetworkPolicyFromRules(
                                podSelector=np_pod_selector,
                                namespaceSelector=np_ns_selector,
                                ipBlock=np_ip_block))

                    # Parse "ports" attribute.
                    np_port_list = []
                    if ingress.get('ports'):
                        for port in ingress.get('ports'):

                            np_port = introspect.NetworkPolicyPort(
                                port=port.get('port').__str__(),
                                    protocol=port.get('protocol'))

                            np_port_list.append(np_port)

                    np_ingress_list.append(\
                        introspect.NetworkPolicyIngressPolicy(\
                            fromPolicy=from_list, ports=np_port_list))

            # Parse "egress" attribute.
            np_egress_list = []
            if np.spec.get('egress'):
                for egress in np.spec.get('egress'):
                    # Parse "to" attribute.
                    to_list = []
                    if egress.get('to'):
                        for each_to in egress.get('to'):
                            np_ip_block = None
                            if each_to.get('ipBlock'):
                                ipblock = each_to.get('ipBlock')
                                cidr = ipblock.get('cidr', None)
                                except_cidr_list = []
                                for except_cidr in ipblock.get('except', []):
                                    except_cidr_list.append(except_cidr)
                                np_ip_block = introspect.NetworkPolicyIpBlock(
                                    cidr=cidr, except_cidr = except_cidr_list)

                            to_list.append(introspect.NetworkPolicyToRules(
                                ipBlock=np_ip_block))

                    # Parse "ports" attribute.
                    np_port_list = []
                    if egress.get('ports'):
                        for port in egress.get('ports'):
                            np_port = introspect.NetworkPolicyPort(
                                port=port.get('port').__str__(),
                                    protocol=port.get('protocol'))
                            np_port_list.append(np_port)

                    np_egress_list.append(\
                        introspect.NetworkPolicyEgressPolicy(\
                            toPolicy=to_list, ports=np_port_list))

            # Parse "pod selector" attribute.
            np_pod_selector = None
            if np.spec.get('podSelector'):
                pod_selector = np.spec.get('podSelector')
                np_pod_selector = introspect.NetworkPolicyLabelSelectors(
                    matchLabels=pod_selector.get('matchLabels'))

            np_spec = introspect.NetworkPolicySpec(ingress=np_ingress_list,
                egress = np_egress_list,
                podSelector=np_pod_selector)

            np_instance = introspect.NetworkPolicyInstance(uuid=np.uuid,
                name=np.name, name_space=np.namespace,
                vnc_firewall_policy_fqname=np.get_vnc_fq_name().__str__(),
                    spec_string=np.spec.__str__(), spec=np_spec)

            # Append the constructed element info to the response.
            np_resp.network_policies.append(np_instance)

        # Send the reply out.
        np_resp.response(req.context())

#
# Kubernetes Ingress Object DB.
#
class IngressKM(KubeDBBase):
    _dict = {}
    obj_type = 'Ingress'

    def __init__(self, uuid, obj=None, request_id=None):
        self.uuid = uuid

        # Metadata.
        self.name = None
        self.namespace = None
        self.labels = {}
        self.spec = {}

        # Spec.
        self.rules = []
        self.default_backend = {}

        # If an object is provided, update self with contents of object.
        if obj:
            self.update(obj)

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self._update_metadata(obj.get('metadata'))
        self._update_spec(obj.get('spec'))

    def _update_metadata(self, md):
        if md is None:
            return
        self.name = md.get('name')
        self.namespace = md.get('namespace')
        self.labels = md.get('labels', {})

    def _update_spec(self, spec):
        if spec is None:
            return
        self.spec = spec
        self.rules = spec.get('rules', {})
        self.default_backend = spec.get('backend', {})

    @staticmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Ingress DB lookup/introspect request. """
        ingress_resp = introspect.IngressDatabaseListResp(ingress=[])

        # Iterate through all elements of Ingress DB.
        for ingress in list(IngressKM.values()):

            # If the request is for a specific entry, then locate the entry.
            if req.ingress_uuid and req.ingress_uuid != ingress.uuid:
                continue

            # Get default backend info.
            def_backend = introspect.IngressBackend(
                name=ingress.default_backend.get('serviceName'),
                port=str(ingress.default_backend.get('servicePort')))

            # Get rules.
            rules = []
            for rule in ingress.rules:
                ingress_rule = introspect.IngressRule(spec=[])
                for key,value in rule.items():
                    if key == 'host':
                        # Get host info from rule.
                        ingress_rule.host = value
                    else:
                        # Get proto spec from rule.
                        proto_spec = introspect.IngressProtoSpec(paths=[])
                        proto_spec.proto = key
                        for path in value.get('paths', []):
                            backend = path.get('backend')
                            proto_backend = None
                            if backend:
                                proto_backend = introspect.IngressBackend(
                                    name=backend.get('serviceName'),
                                    port=str(backend.get('servicePort')))

                            proto_path = introspect.IngressRuleProtoPath(
                                backend=proto_backend, path=path.get('path'))

                            proto_spec.paths.append(proto_path)

                        ingress_rule.spec.append(proto_spec)

                rules.append(ingress_rule)

            # Construct response for an element.
            ingress_instance = introspect.IngressInstance(
                uuid=ingress.uuid, name=ingress.name,
                labels=ingress.labels, name_space=ingress.namespace,
                rules=rules, default_backend=def_backend)

            # Append the constructed element info to the response.
            ingress_resp.ingress.append(ingress_instance)

        # Send the reply out.
        ingress_resp.response(req.context())

class NetworkKM(KubeDBBase):
    _dict = {}
    obj_type = 'Network'

    def __init__(self, uuid, obj = None, request_id=None):
        self.uuid = uuid

        # Metadata.
        self.name = None
        self.annotations = None
        self.annotated_vn_fq_name = None

        # If an object is provided, update self with contents of object.
        if obj:
            self.update(obj)

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self._update_metadata(obj.get('metadata'))

    def _update_metadata(self, md):
        if md is None:
            return
        self.name = md.get('name')
        self.namespace = md.get('namespace')
        self.annotations = md.get('annotations', None)
        self._parse_annotations(self.annotations)

    def _parse_annotations(self, annotations):
        self.annotations = None
        if not annotations:
            return

        # Check if FQ name has already been updated, If so ignore further
        # update calls.
        if self.annotated_vn_fq_name:
            return

        # Parse virtual network from annotations.
        if 'opencontrail.org/network' in annotations:
            # CASE 1: Network already exists in Contrail. Parse the Network
            # FQ name from the annotation and store
            try:
                self.annotated_vn_fq_name = self.get_vn_from_annotation(
                    annotations)
            except Exception as e:
                err_msg = "Failed to parse annotations for network [%s]."\
                " Error[%s]" % (self.name, str(e))
                raise Exception(err_msg)
        elif 'opencontrail.org/cidr' in annotations:
            # CASE 2: Network does not exists in Contrail. The network FQ name
            # will be generated later when processing the request.
            self.annotated_vn_fq_name = None

    def is_contrail_nw(self):
        return True if self.annotated_vn_fq_name else False

    @classmethod
    def get_network_fq_name(cls, name, namespace):
        for key, value in cls._dict.items():
            if value.name == name and value.namespace == namespace:
                return value
        return None
