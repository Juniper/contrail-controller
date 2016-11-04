#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

class LabelCache(object):

    def __init__(self):
        self.pod_label_cache = {}
        self.service_label_cache = {}

    def _get_key(self, label):
        key = label[0] + ':' + label[1]
        return key

    def _locate_label(self, key, cache, label, uuid):
        key = label[0] + ':' + label[1]
        if key not in cache:
            cache[key] = set()
        cache[key].add(uuid)
