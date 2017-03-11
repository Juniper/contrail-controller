#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
from vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config
from vnc_api.vnc_api import (KeyValuePair,KeyValuePairs)

class VncCommon(object):
    """VNC kubernetes common functionality.
    """
    def __init__(self, kube_obj_kind):
        self.annotations = {}
        self.annotations['kind'] = kube_obj_kind
        self.annotations['owner'] = vnc_kube_config.cluster_owner()
        self.annotations['cluster'] = vnc_kube_config.cluster_name()

    def add_annotations(self, obj, kube_fq_name_ann_keys, **kwargs):
        """Add annotations on the input object.

        Given an object, this method will add annotations on that object
        per the fq-name annotation keys specified by the caller.
        """
        self.annotations.update(kwargs)
        if not set(kube_fq_name_ann_keys).issubset(self.annotations):
            err_msg = "Annotations required to contruct kube_fq_name for" +\
                " this object was not found in input keyword args."
            raise Exception(err_msg)

        for ann_key, ann_value in self.annotations.iteritems():
            obj.add_annotations(KeyValuePair(key=ann_key, value=ann_value))

    def add_display_name(self, obj, display_name_format, **kwargs):
        """Set display name on the input object.

        Given an object, this method will set the display name on the object
        per the format specified by the caller.
        """
        display_keys = dict(self.annotations)
        display_keys.update(kwargs)
        if not set(display_name_format).issubset(display_keys):
            err_msg = "Values required to contruct display name for this" +\
                " object was not found in input keyword args."
            raise Exception(err_msg)

        display_name = []
        for elem in display_name_format:
            display_name.append(display_keys[elem])
        obj.set_display_name(":".join(display_name))
