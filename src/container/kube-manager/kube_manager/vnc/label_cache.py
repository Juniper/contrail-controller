#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#


from enum import Enum
from kube_manager.vnc.vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config


class LabelCache(object):

    def __init__(self):
        self.ns_label_cache = {}
        self.pod_label_cache = {}
        self.service_selector_cache = {}

    def _get_key(self, label):
        key = label[0] + ':' + label[1]
        return key

    def _get_namespace_label(self, namespace):
        label = {'namespace': namespace}
        return label

    def _validate_key_value(self, key, value):
        if '/' in key:
            key = key.replace("/", "__slash__")
        if '/' in value:
            value = value.replace("/", "__slash__")
        return key, value

    def _locate_label(self, key, cache, label, uuid):
        label = list(label)
        label[0], label[1] = self._validate_key_value(label[0], label[1])
        key = label[0] + ':' + label[1]
        if key not in cache:
            cache[key] = set()
        cache[key].add(uuid)

    def _remove_label(self, key, cache, label, uuid):
        label = list(label)
        label[0], label[1] = self._validate_key_value(label[0], label[1])
        key = label[0] + ':' + label[1]
        if key in cache:
            try:
                cache[key].remove(uuid)
                if not len(cache[key]):
                    del cache[key]
            except KeyError:
                pass


class XLabelCache(object):
    """
    K8s Label Management.

    This class processes lables in K8s events and manages the relationship
    between labels in differnt K8s objects.

    Any K8s object type can avail the services of this class by instantiating
    it and using this class instance to feed labels on its events.

    This class mantains the following mapping:
    1. A labels to all K8s object (i.e their guids) that reference the lable.
    2. A K8s object( i.e its guid) to labels on the object.

    This class also allows for registration of notify methods for a label add
    or a delete event. An add event is triggered when a label is referenced for
    the first time by a K8s object. A delete event is triggered when there are
    no more references to a k8s object.

    """

    # Label to refencing Guid's mapping.
    k8s_label_to_guid_cache = {}

    # Guid to its labels mapping.
    k8s_guid_to_label_cache = {}

    # Registered Add callback method.
    label_add_cb = None

    # Registered Delete callback method.
    label_delete_cb = None

    class PredefinedTags(Enum):
        # Tags that have special meaning to Contrail.
        APPLICATION = 'application'
        TIER = "tier"

    def __init__(self, resource_type):

        self.resource_type = resource_type
        if resource_type in XLabelCache.k8s_label_to_guid_cache:
            return

        XLabelCache.k8s_label_to_guid_cache[resource_type] = {}

    def reset_resource(self):
        XLabelCache.k8s_label_to_guid_cache[self.resource_type] = {}

    @classmethod
    def _validate_key_value(cls, key, value):
        if '/' in key:
            key = key.replace("/", "__slash__")
        if '/' in value:
            value = value.replace("/", "__slash__")
        return key, value

    @classmethod
    def get_key(cls, k, v):
        k, v = cls._validate_key_value(k, v)
        return k + ':' + v

    def get_key_value(self, label):
        kv = label.split(':')
        kv[0], kv[1] = self._validate_key_value(kv[0], kv[1])
        return kv[0], kv[1]

    def _update_label_to_guid_cache(self, key, value, obj_uuid):

        # Construct the label key.
        label_key = self.get_key(key, value)

        # If an entry exists for this label, add guid to the existing entry.
        # If not, create one.
        ltg_cache = XLabelCache.k8s_label_to_guid_cache[self.resource_type]
        if label_key in ltg_cache:
            ltg_cache[label_key].add(obj_uuid)
        else:
            ltg_cache[label_key] = {obj_uuid}
            XLabelCache.label_add_cb(key, value)

        return label_key

    def process(self, obj_uuid, curr_labels={}, list_curr_labels_dict=[]):
        """
        Process addition/update/deletion of labels on an object.

        This is the main interface for label management in this class.
        This method requires all current labels on an object to  be specified.
        This method compares the current and previous labels on an object
        and adds/removes label association with the object as needed.

        curr_labels: Labels that are to be associated with the object.
        list_curr_labels_dict: Labels that are to be associated with the
                               object in a list format.
        """
        all_labels = set()

        # Update label to k8s guid cache.
        if curr_labels:
            for key, value in curr_labels.items():

                key, value = self._validate_key_value(key, value)
                # Construct the label key.
                label_key = self._update_label_to_guid_cache(key, value, obj_uuid)

                # Construct a set of all input label keys.
                all_labels.add(label_key)

        if list_curr_labels_dict:
            for labels_dict in list_curr_labels_dict:
                for key, value in labels_dict.items():
                    key, value = self._validate_key_value(key, value)
                    # Construct the label key.
                    label_key = self._update_label_to_guid_cache(key, value, obj_uuid)
                    # Construct a set of all input label keys.
                    all_labels.add(label_key)

        # Update the k8s guid to label cache.
        gtl_cache = XLabelCache.k8s_guid_to_label_cache

        # If an entry exists for the k8s guid, then determine if there is
        # a modification to the existing labels on the guid.
        # if yes: Update the this and add/remove entries by label to guid cache
        # as needed.
        if obj_uuid not in gtl_cache:
            gtl_cache[obj_uuid] = all_labels
        else:
            # Add any new labels that may been added to thie object.
            gtl_cache[obj_uuid] = gtl_cache[obj_uuid].union(all_labels)

            # Some labels got removed from this object.
            # Invoke delete to disassociate the guid from removed labels.
            removed_labels = gtl_cache[obj_uuid].difference(all_labels)

            if len(removed_labels):
                # Some labels got removed from this object.
                # Invoke delete to disassociate the guid from removed labels.
                self._delete(obj_uuid, removed_labels)
                gtl_cache[obj_uuid] = gtl_cache[obj_uuid].difference(removed_labels)

    def _delete(self, obj_uuid, labels):
        """
        Delete labels from an object.
        """
        for label in labels:
            res_labels = XLabelCache.k8s_label_to_guid_cache[self.resource_type]
            if label in res_labels:
                k, v = self.get_key_value(label)

                # Hack: Need an efficient way to skip namespace label delete
                # by other resouce types.
                if k == 'namespace' and self.resource_type != "Namespace":
                    continue

                res_labels[label].remove(obj_uuid)
                if not len(res_labels[label]):
                    del res_labels[label]

                    XLabelCache.label_delete_cb(k, v)

    def append(self, obj_guid, label):
        """
        Convience api to add a label to an existing object.

        This method differs from the process() method in that, process()
        method requires all labels on the object to be provided to it can
        deduce latest label associations on the object.

        This append() method provides an interface to add a specific label
        to an object. It does not require all current labels on that object
        to be specified.
        """
        curr_labels = self.get_labels(obj_guid)
        new_labels = {}
        for curr_label in curr_labels:
            k, v = self.get_key_value(curr_label)
            new_labels[k] = v
        new_labels.update(dict(label))
        self.process(obj_guid, new_labels)

    def remove(self, obj_guid, removed_labels):
        """ Convience api to remove a label from an existing object. """
        curr_labels = self.get_labels(obj_guid)
        new_labels = {}
        for curr_label in curr_labels:
            k, v = self.get_key_value(curr_label)
            if k in removed_labels and removed_labels[k] == v:
                continue
            new_labels[k] = v
        self.process(obj_guid, new_labels)

    @classmethod
    def register_label_add_callback(cls, cb_func):
        cls.label_add_cb = cb_func

    @classmethod
    def register_label_delete_callback(cls, cb_func):
        cls.label_delete_cb = cb_func

    @classmethod
    def get_labels(cls, obj_uuid):
        return XLabelCache.k8s_guid_to_label_cache.get(obj_uuid, {})

    @classmethod
    def get_namespace_label(cls, namespace):
        """ Construct a namespace label. """
        label = {'namespace': namespace}
        return label

    @classmethod
    def get_namespace_label_kv(cls, namespace):
        return 'namespace', namespace

    @classmethod
    def get_service_label(cls, service_name):
        """ Construct a service label. """
        key = "-".join([vnc_kube_config.cluster_name(), 'svc'])
        value = service_name
        return {key: value}

    @classmethod
    def get_service_member_label(cls, service_name):
        """ Construct a service memner label. """
        svc_label = cls.get_service_label(service_name)
        svc_member_label_dict = {}
        for k, v in svc_label.items():
            member_key = "-".join([k, "member"])
            svc_member_label_dict[member_key] = v
        return svc_member_label_dict

    @classmethod
    def get_cluster_label(cls, cluster_name):
        """ Construct a cluster label. """
        label = {'application': cluster_name}
        return label

    @classmethod
    def get_ingress_label(cls, ingress_name):
        """ Construct a ingress label. """
        label = {'ingress': ingress_name}
        return label

    def get_labels_dict(self, obj_guid, no_value=False):
        """ Construct labels in Contrail format. """
        labels_dict = {}
        # predefined_values = [item.value for item in self.PredefinedTags]
        curr_labels = self.get_labels(obj_guid)
        is_global = vnc_kube_config.is_global_tags()
        for label in curr_labels:
            k, v = self.get_key_value(label)

            # TBD: uncomment if custom is different from predefined.
            # if k in predefined_values:

            labels_dict[k] = {
                'is_global': is_global,
                'value': None if no_value else v
            }

        return labels_dict

    def get_delete_labels_dict(self, labels):
        """ Construct labels for delete in Contrail format. """
        labels_dict = {}
        for k in labels.keys():
            labels_dict[k] = None
        return labels_dict
