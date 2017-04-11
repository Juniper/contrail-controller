#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
from vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config
from vnc_api.vnc_api import (KeyValuePair,KeyValuePairs)
from config_db import DBBaseKM

class VncCommon(object):
    """VNC kubernetes common functionality.
    """
    def __init__(self, kube_obj_kind):
        self.annotations = {}
        self.annotations['kind'] = kube_obj_kind
        self.annotations['owner'] = vnc_kube_config.cluster_owner()
        self.annotations['cluster'] = vnc_kube_config.cluster_name()

    def _get_annotations(self, namespace, name, k8s_event_type=None, **kwargs):
        annotations = self.annotations.copy()

        infra_anns = {}
        infra_anns['project'] = vnc_kube_config.cluster_project_name(namespace)
        infra_anns['namespace'] = namespace
        infra_anns['name'] = name
        if k8s_event_type:
            infra_anns['kind'] = k8s_event_type

        # Update annotations with infra specific annotations.
        annotations.update(infra_anns)

        # Append other custom annotations.
        annotations.update(kwargs)

        return annotations

    def add_annotations(self, obj, kube_fq_name_ann_keys, namespace,
                        name, k8s_event_type=None, **kwargs):
        """Add annotations on the input object.

        Given an object, this method will add annotations on that object
        per the fq-name annotation keys specified by the caller.
        """
        annotations = self._get_annotations(namespace, name, k8s_event_type,
                        **kwargs)

        if not set(kube_fq_name_ann_keys).issubset(annotations):
            err_msg = "Annotations required to contruct kube_fq_name for" +\
                " this object (%s:%s) was not found in input keyword args." %\
                (namespace,name)
            raise Exception(err_msg)

        for ann_key, ann_value in annotations.iteritems():
            obj.add_annotations(KeyValuePair(key=ann_key, value=ann_value))

    def get_kube_fq_name(self, kube_fq_name_template, namespace, name,
        k8s_event_type=None,**kwargs):
        annotations = self._get_annotations(namespace, name, k8s_event_type,
            **kwargs)

        if not set(kube_fq_name_template).issubset(annotations):
            err_msg = "Annotations required to contruct kube_fq_name for" +\
                " this object (%s:%s) was not found in input keyword args." %\
                (namespace,name)
            raise Exception(err_msg)

        return DBBaseKM.get_kube_fq_name(kube_fq_name_template, **annotations)

    def get_kube_fq_name_to_uuid(self, config_db_cls, namespace, name,
        k8s_event_type=None,**kwargs):
        kube_fq_name = self.get_kube_fq_name(config_db_cls.kube_fq_name_key,
            namespace, name, k8s_event_type, **kwargs)
        return config_db_cls.get_kube_fq_name_to_uuid(kube_fq_name)

    @staticmethod
    def make_name(*args):
        return "__".join(str(i) for i in args)

    @staticmethod
    def make_display_name(*args):
        return "__".join(str(i) for i in args)
