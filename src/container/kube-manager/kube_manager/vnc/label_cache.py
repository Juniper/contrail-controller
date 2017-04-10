#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

class LabelCache(object):

    def __init__(self):
        self.ns_label_cache = {}
        self.pod_label_cache = {}
        self.service_selector_cache = {}

    def _get_key(self, label):
        key = label[0] + ':' + label[1]
        return key

    def _locate_label(self, key, cache, label, uuid):
        key = label[0] + ':' + label[1]
        if key not in cache:
            cache[key] = set()
        cache[key].add(uuid)

    def _remove_label(self, key, cache, label, uuid):
        key = label[0] + ':' + label[1]
        if key in cache:
            try:
                cache[key].remove(uuid)
                if not len(cache[key]):
                    del cache[key]
            except KeyError:
                pass
