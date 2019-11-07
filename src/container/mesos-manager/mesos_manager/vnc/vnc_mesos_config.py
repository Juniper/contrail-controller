#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
from builtins import object
from mesos_manager.vnc.vnc_utils import (get_vn_fq_name_from_dict_string,
                                     get_domain_name_from_vn_dict_string,
                                     get_project_name_from_vn_dict_string,
                                     get_vn_name_from_vn_dict_string,
                                     get_domain_name_from_project_dict_string,
                                     get_project_name_from_project_dict_string)
class VncMesosConfig(object):
    """VNC mesos common config.

    This class holds all config that are common to all vnc processees
    in the mesos manager.
    """
    vnc_mesos_config = {}

    def __init__(self, **kwargs):
        VncMesosConfig.vnc_mesos_config = kwargs

    @classmethod
    def update(cls, **kwargs):
        VncMesosConfig.vnc_mesos_config.update(kwargs)

    @classmethod
    def cluster_ip_fabric_network_fq_name(cls):
        vn_fq_name = ['default-domain', 'default-project', 'ip-fabric']
        return vn_fq_name

    @classmethod
    def cluster_default_pod_task_network_name(cls):
        vn_name = cls.get_configured_pod_task_network_name()
        if vn_name:
            return vn_name
        return cls.cluster_name() + '-default-pod-task-network'

    @classmethod
    def cluster_default_pod_task_network_fq_name(cls):
        vn_fq_name = [cls.cluster_domain(), cls.cluster_default_project_name(),
                      cls.cluster_default_pod_task_network_name()]
        return vn_fq_name

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
    def pod_task_ipam_fq_name(cls):
        return cls.vnc_mesos_config.get("cluster_pod_task_ipam_fq_name", None)

    @classmethod
    def ip_fabric_ipam_fq_name(cls):
        return cls.vnc_kubernetes_config.get("cluster_ip_fabric_ipam_fq_name", None)


    @classmethod
    def cluster_owner(cls):
        return cls.args().mesos_cluster_owner

    @classmethod
    def cluster_name(cls):
        return cls.args().mesos_cluster_name

    @classmethod
    def cluster_domain(cls):
        domain_name = cls.get_configured_domain_name()
        if domain_name:
            return domain_name
        return cls.args().mesos_cluster_domain

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
    def args(cls):
        return cls.vnc_mesos_config.get("args", None)

    @classmethod
    def vnc_lib(cls):
        return cls.vnc_mesos_config.get("vnc_lib", None)

    @classmethod
    def queue(cls):
        return cls.vnc_mesos_config.get("queue", None)

    @classmethod
    def sync_queue(cls):
        return cls.vnc_mesos_config.get("sync_queue", None)

    @classmethod
    def logger(cls):
        return cls.vnc_mesos_config.get("logger", None)

    @classmethod
    def is_cluster_network_configured(cls):
        args = cls.args()
        if args.cluster_network and args.cluster_network != '{}':
            return True
        return False

    @classmethod
    def is_cluster_project_configured(cls):
        args = cls.args()
        if args.cluster_project and args.cluster_project != '{}':
            return True
        return False

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
    def get_configured_pod_task_network_name(cls):
        args = cls.args()
        if args.cluster_pod_task_network:
            return get_vn_name_from_vn_dict_string(args.cluster_pod_task_network)
        return None

    @classmethod
    def cluster_default_pod_task_network_name(cls):
        vn_name = cls.get_configured_pod_task_network_name()
        if vn_name:
            return vn_name
        return cls.cluster_name() + '-default-pod-task-network'

    @classmethod
    def get_mesos_agent_retry_sync_hold_time(cls):
        return cls.args().mesos_agent_retry_sync_hold_time

    @classmethod
    def get_mesos_agent_retry_sync_count(cls):
        return cls.args().mesos_agent_retry_sync_count
