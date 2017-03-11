#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

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
    def cluster_domain(cls):
        return cls.args().kubernetes_cluster_domain

    @classmethod
    def cluster_project_name(cls, namespace):
        args = cls.args()
        if args.cluster_project:
            return args.cluster_project
        return namespace

    @classmethod
    def cluster_project_fq_name(cls, namespace):
        return [cls.cluster_domain(), cls.cluster_project_name(namespace)]

    @classmethod
    def cluster_default_project_name(cls):
        args = cls.args()
        if args.cluster_project:
            return args.cluster_project
        return "default"

    @classmethod
    def cluster_default_project_fq_name(cls):
        return [cls.cluster_domain(), cls.cluster_default_project_name()]

    @classmethod
    def cluster_default_network_name(cls):
        return "cluster-network"
