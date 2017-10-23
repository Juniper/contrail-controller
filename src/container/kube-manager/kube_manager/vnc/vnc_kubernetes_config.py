#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
from kube_manager.common.utils import (get_vn_fq_name_from_dict_string,
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
    def service_fip_pool(cls):
        return cls.vnc_kubernetes_config.get("cluster_service_fip_pool", None)

    @classmethod
    def cluster_owner(cls):
        return cls.args().kubernetes_cluster_owner

    @classmethod
    def cluster_name(cls):
        return cls.args().cluster_name

    @classmethod
    def is_cluster_project_configured(cls):
        args = cls.args()
        if args.cluster_project and args.cluster_project != '{}':
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
        if args.cluster_network:
            return get_domain_name_from_vn_dict_string(args.cluster_network)
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
        if args.cluster_network:
            return get_project_name_from_vn_dict_string(args.cluster_network)
        if cls.is_cluster_project_configured():
            return get_project_name_from_project_dict_string(
                args.cluster_project)
        return None

    @classmethod
    def cluster_project_name(cls, namespace):
        project_name = cls.get_configured_project_name()
        if project_name:
            return project_name
        return namespace

    @classmethod
    def cluster_project_fq_name(cls, namespace):
        return [cls.cluster_domain(), cls.cluster_project_name(namespace)]

    @classmethod
    def cluster_default_project_name(cls):
        project_name = cls.get_configured_project_name()
        if project_name:
            return project_name
        return "default"

    @classmethod
    def cluster_default_project_fq_name(cls):
        return [cls.cluster_domain(), cls.cluster_default_project_name()]

    @classmethod
    def get_configured_network_name(cls):
        args = cls.args()
        if args.cluster_network:
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
    def cluster_ip_fabric_network_fq_name(cls):
        vn_fq_name = ['default-domain', 'default-project', 'ip-fabric']
        return vn_fq_name
