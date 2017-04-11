#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Database for Kubernetes objects.
"""

from cfgm_common.vnc_db import DBBase
from kube_manager.sandesh.kube_introspect import ttypes as introspect
from ast import literal_eval

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
        """ Get virtual network annotations from a k8s object.
        """
        fq_name_key = ['domain','project','name']
        vn_fq_name = []
        vn_ann = annotations.get('network', None)
        if vn_ann:
            vn = literal_eval(vn_ann)
            if not vn:
                err_msg = "No virtual network annotations were found."
                raise Exception(err_msg)

            # Virtual network annotation found.
            for key in fq_name_key:
                value = vn.get(key, None)
                if value:
                    vn_fq_name.append(value)
                else:
                    err_msg = "[%s] not specified in annotations." %\
                        (key)
                    raise Exception(err_msg)
        return vn_fq_name
#
# Kubernetes POD Object DB.
#
class PodKM(KubeDBBase):
    _dict = {}
    obj_type = 'Pod'

    def __init__(self, uuid, obj = None):
        self.uuid = uuid

        # Metadata.
        self.name = None
        self.namespace = None
        self.labels = {}
        self.annotations = None
        self.pod_vn_fq_name = None

        # Spec.
        self.nodename = None
        self.host_ip = None

        # Status.
        self.phase = None

        # If an object is provided, update self with contents of object.
        if obj:
            self.update(obj)

    def update(self, obj=None):
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
        for pod in PodKM.values():

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

    def get_vn_fq_name(self):
        """Return virtual-network fq-name annotated on this pod."""
        return self.pod_vn_fq_name

#
# Kubernetes Namespace Object DB.
#
class NamespaceKM(KubeDBBase):
    _dict = {}
    obj_type = 'Namespace'

    def __init__(self, uuid, obj = None):
        self.uuid = uuid

        # Metadata.
        self.name = None
        self.labels = {}
        self.isolated_vn_fq_name = None
        self.annotated_vn_fq_name = None
        self.annotations = None

        # Status.
        self.phase = None

        # Config cache.
        self.isolated = False

        # If an object is provided, update self with contents of object.
        if obj:
            self.update(obj)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self._update_metadata(obj.get('metadata'))
        self._update_status(obj.get('status'))

    def _update_metadata(self, md):
        if md is None:
            return
        self.name = md.get('name')
        self.labels = md.get('labels')

        # Parse annotations on this namespace.
        self.annotations = md.get('annotations')
        self._parse_annotations(self.annotations)

    def _parse_annotations(self, annotations):
        if not annotations:
            return

        # Parse virtual network annotations.
        if not self.annotated_vn_fq_name:
            try:
                self.annotated_vn_fq_name = self.get_vn_from_annotation(
                    annotations)
            except Exception as e:
                err_msg = "Failed to parse annotations for namespace [%s]."\
                " Error[%s]" % (self.name, str(e))
                raise Exception(err_msg)

        # Cache isolated namespace directive.
        if 'isolated' in annotations and annotations['isolated'] == "true":
            # Namespace is configured as isolated.
            self.isolated = True
        else:
            self.isolated = False

    def _update_status(self, status):
        if status is None:
            return
        self.phase = status.get('phase')

    def is_isolated(self):
        return self.isolated

    def set_isolated_network_fq_name(self, fq_name):
        self.isolated_vn_fq_name = fq_name

    def get_isolated_network_fq_name(self):
        return self.isolated_vn_fq_name

    def get_annotated_network_fq_name(self):
        return self.annotated_vn_fq_name

    @staticmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Namespace DB lookup/introspect request. """
        ns_resp = introspect.NamespaceDatabaseListResp(namespaces=[])

        # Iterate through all elements of Namespace DB.
        for ns in NamespaceKM.values():

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

#
# Kubernetes Service Object DB.
#
class ServiceKM(KubeDBBase):
    _dict = {}
    obj_type = 'Service'

    def __init__(self, uuid, obj = None):
        self.uuid = uuid

        # Metadata.
        self.name = None
        self.namespace = None
        self.labels = {}

        # Spec.
        self.cluster_ip = None
        self.service_type = None

        # If an object is provided, update self with contents of object.
        if obj:
            self.update(obj)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self._update_metadata(obj.get('metadata'))
        self._update_spec(obj.get('spec'))

    def _update_metadata(self, md):
        if md is None:
            return
        self.name = md.get('name')
        self.namespace = md.get('namespace')
        self.labels = md.get('labels')

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
        for svc in ServiceKM.values():

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

    def __init__(self, uuid, obj = None):
        self.uuid = uuid

        # Metadata.
        self.name = None
        self.namespace = None

        # Spec.
        self.spec = {}

        # If an object is provided, update self with contents of object.
        if obj:
            self.update(obj)

    def update(self, obj=None):
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
        return introspect.NetworkPolicyPodSelectors(matchLabels=labels)

    @staticmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Network Policy DB lookup/introspect request. """
        np_resp = introspect.NetworkPolicyDatabaseListResp(network_policies=[])

        # Iterate through all elements of Network Policy DB.
        for np in NetworkPolicyKM.values():

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

                            np_pod_selector = None
                            if each_from.get('podSelector'):

                                np_pod_selector = np.get_pod_selector(
                                    each_from.get('podSelector'))

                            from_list.append(introspect.NetworkPolicyFromRules(
                                podSelector=np_pod_selector))

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

            # Parse "pod selector" attribute.
            np_pod_selector = None
            if np.spec.get('podSelector'):
                pod_selector = np.spec.get('podSelector')
                np_pod_selector = introspect.NetworkPolicyPodSelectors(
                    matchLabels=pod_selector.get('matchLabels'))

            np_spec = introspect.NetworkPolicySpec(ingress=np_ingress_list,
                podSelector=np_pod_selector)

            np_instance = introspect.NetworkPolicyInstance(uuid=np.uuid,
                name=np.name, name_space=np.namespace,
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

    def __init__(self, uuid, obj=None):
        self.uuid = uuid

        # Metadata.
        self.name = None
        self.namespace = None
        self.labels = {}

        # Spec.
        self.rules = []
        self.default_backend = {}

        # If an object is provided, update self with contents of object.
        if obj:
            self.update(obj)

    def update(self, obj=None):
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
        self.rules = spec.get('rules', {})
        self.default_backend = spec.get('backend', {})

    @staticmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Ingress DB lookup/introspect request. """
        ingress_resp = introspect.IngressDatabaseListResp(ingress=[])

        # Iterate through all elements of Ingress DB.
        for ingress in IngressKM.values():

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
                for key,value in rule.iteritems():
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
