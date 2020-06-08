#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from kube_manager.common.utils import (
    get_domain_name_from_vn_dict_string,
    get_project_name_from_vn_dict_string,
    get_vn_name_from_vn_dict_string,
    get_domain_name_from_project_dict_string,
    get_project_name_from_project_dict_string)


class VncKubernetesConfig(object):
    """VNC kubernetes common config.

    This class holds all config that are common to all vnc processees
    in the kube manager.
    """
    vnc_kubernetes_config = {}

    def __init__(self, **kwargs):
        VncKubernetesConfig.vnc_kubernetes_config = kwargs

    @classmethod
    def update(cls, **kwargs):
        VncKubernetesConfig.vnc_kubernetes_config.update(kwargs)

    @classmethod
    def logger(cls):
        return cls.vnc_kubernetes_config.get("logger", None)

    @classmethod
    def vnc_lib(cls):
        return cls.vnc_kubernetes_config.get("vnc_lib", None)

    @classmethod
    def label_cache(cls):
        return cls.vnc_kubernetes_config.get("label_cache", None)

    @classmethod
    def args(cls):
        return cls.vnc_kubernetes_config.get("args", None)

    @classmethod
    def queue(cls):
        return cls.vnc_kubernetes_config.get("queue", None)

    @classmethod
    def kube(cls):
        return cls.vnc_kubernetes_config.get("kube", None)

    @classmethod
    def pod_ipam_fq_name(cls):
        return cls.vnc_kubernetes_config.get("cluster_pod_ipam_fq_name", None)

    @classmethod
    def service_ipam_fq_name(cls):
        return cls.vnc_kubernetes_config.get("cluster_service_ipam_fq_name", None)

    @classmethod
    def ip_fabric_ipam_fq_name(cls):
        return cls.vnc_kubernetes_config.get("cluster_ip_fabric_ipam_fq_name", None)

    @classmethod
    def cluster_owner(cls):
        return cls.args().kubernetes_cluster_owner

    @classmethod
    def cluster_name(cls):
        return cls.args().cluster_name

    @classmethod
    def application_policy_set_name(cls):
        if cls.args().aps_name:
            return cls.args().aps_name
        elif cls.cluster_name():
            return cls.cluster_name()
        else:
            return 'default-application-policy-set'

    @classmethod
    def is_cluster_project_configured(cls):
        args = cls.args()
        if args.cluster_project and args.cluster_project != '{}':
            return True
        return False

    @classmethod
    def is_cluster_network_configured(cls):
        args = cls.args()
        if args.cluster_network and args.cluster_network != '{}':
            return True
        return False

    @classmethod
    def is_public_fip_pool_configured(cls):
        args = cls.args()
        if args.public_fip_pool and args.public_fip_pool != '{}':
            return True
        return False

    @classmethod
    def get_configured_domain_name(cls):
        args = cls.args()
        if cls.is_cluster_network_configured():
            return get_domain_name_from_vn_dict_string(
                args.cluster_network)
        if cls.is_cluster_project_configured():
            return get_domain_name_from_project_dict_string(
                args.cluster_project)
        return None

    @classmethod
    def cluster_domain(cls):
        domain_name = cls.get_configured_domain_name()
        if domain_name:
            return domain_name
        return cls.args().kubernetes_cluster_domain

    @classmethod
    def get_configured_project_name(cls):
        args = cls.args()
        if cls.is_cluster_network_configured():
            return get_project_name_from_vn_dict_string(
                args.cluster_network)
        if cls.is_cluster_project_configured():
            return get_project_name_from_project_dict_string(
                args.cluster_project)
        return None

    @classmethod
    def construct_project_name_for_namespace(cls, namespace):
        return "-".join([cls.cluster_name(), namespace])

    @classmethod
    def cluster_project_name(cls, namespace=None):
        project_name = cls.get_configured_project_name()
        if project_name:
            return project_name
        if namespace:
            return cls.construct_project_name_for_namespace(namespace)
        return None

    @classmethod
    def get_project_name_for_namespace(cls, namespace):
        """
        Given a namespace name, construct a project name for the namespace.

        If a project name is pre-configured, then configured project will be
        used and no project will be created for the namespace. So this method
        will return None, if a project name is already configued.
        """
        if cls.get_configured_project_name():
            return None
        return cls.construct_project_name_for_namespace(namespace)

    @classmethod
    def cluster_project_fq_name(cls, namespace=None):
        return [cls.cluster_domain(), cls.cluster_project_name(namespace)]

    @classmethod
    def cluster_default_project_name(cls):
        project_name = cls.get_configured_project_name()
        if project_name:
            return project_name
        return cls.construct_project_name_for_namespace("default")

    @classmethod
    def cluster_default_project_fq_name(cls):
        return [cls.cluster_domain(), cls.cluster_default_project_name()]

    @classmethod
    def get_configured_network_name(cls):
        args = cls.args()
        if cls.is_cluster_network_configured():
            return get_vn_name_from_vn_dict_string(args.cluster_network)
        return None

    @classmethod
    def cluster_default_network_name(cls):
        vn_name = cls.get_configured_network_name()
        if vn_name:
            return vn_name
        return "cluster-network"

    @classmethod
    def cluster_default_network_fq_name(cls):
        vn_fq_name = [cls.cluster_domain(), cls.cluster_default_project_name(),
                      cls.cluster_default_network_name()]
        return vn_fq_name

    @classmethod
    def get_pod_network_name(cls, vn_name):
        return cls.cluster_name() + "-" + vn_name + "-pod-network"

    @classmethod
    def get_configured_pod_network_name(cls):
        args = cls.args()
        if args.cluster_pod_network:
            return get_vn_name_from_vn_dict_string(args.cluster_pod_network)
        return None

    @classmethod
    def cluster_default_pod_network_name(cls):
        vn_name = cls.get_configured_pod_network_name()
        if vn_name:
            return vn_name
        return cls.cluster_name() + '-default-pod-network'

    @classmethod
    def cluster_default_pod_network_fq_name(cls):
        vn_fq_name = [cls.cluster_domain(), cls.cluster_default_project_name(),
                      cls.cluster_default_pod_network_name()]
        return vn_fq_name

    @classmethod
    def get_configured_service_network_name(cls):
        args = cls.args()
        if args.cluster_service_network:
            return get_vn_name_from_vn_dict_string(args.cluster_service_network)
        return None

    @classmethod
    def cluster_default_service_network_name(cls):
        vn_name = cls.get_configured_service_network_name()
        if vn_name:
            return vn_name
        return cls.cluster_name() + '-default-service-network'

    @classmethod
    def cluster_default_service_network_fq_name(cls):
        vn_fq_name = [cls.cluster_domain(), cls.cluster_default_project_name(),
                      cls.cluster_default_service_network_name()]
        return vn_fq_name

    @classmethod
    def cluster_default_service_network_policy_fq_name(cls):
        np_fq_name = [cls.cluster_domain(), cls.cluster_default_project_name(),
                      cls.cluster_name() + '-default-service-np']
        return np_fq_name

    @classmethod
    def cluster_ip_fabric_network_fq_name(cls):
        vn_fq_name = ['default-domain', 'default-project', 'ip-fabric']
        return vn_fq_name

    @classmethod
    def cluster_ip_fabric_policy_fq_name(cls):
        np_fq_name = [cls.cluster_domain(), cls.cluster_default_project_name(),
                      cls.cluster_name() + '-default-ip-fabric-np']
        return np_fq_name

    @classmethod
    def cluster_nested_underlay_policy_name(cls):
        return cls.cluster_name() + '-nested-underlay-np'

    @classmethod
    def cluster_nested_underlay_policy_fq_name(cls):
        np_fq_name = [cls.cluster_domain(), cls.cluster_default_project_name(),
                      cls.cluster_nested_underlay_policy_name()]
        return np_fq_name

    @classmethod
    def is_global_tags(cls):
        if cls.args().global_tags == '1':
            return True
        return False

    @classmethod
    def get_default_sg_name(cls, ns_name='default'):
        sg_name = "-".join([cls.cluster_name(), ns_name, 'default-sg'])
        return sg_name

    @classmethod
    def is_secure_project_enabled(cls):
        if cls.args().secure_project == 'True':
            return True
        return False
