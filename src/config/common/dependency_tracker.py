#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of dependency tracker
for physical router configuration manager
"""


class DependencyTracker(object):

    def __init__(self, object_class_map, reaction_map):
        self._reaction_map = reaction_map
        self._object_class_map = object_class_map
        self.resources = {}
    # end __init__

    def _add_resource(self, obj_type, obj_uuid):
        if obj_type in self.resources:
            if obj_uuid in self.resources[obj_type]:
                # already visited
                return False
            self.resources[obj_type].append(obj_uuid)
        else:
            self.resources[obj_type] = [obj_uuid]
        return True
    # end _add_resource

    def evaluate(self, obj_type, obj, from_type='self'):
        if obj_type not in self._reaction_map:
            return
        if not self._add_resource(obj_type, obj.uuid):
            return

        for ref_type in self._reaction_map[obj_type][from_type]:
            ref = getattr(obj, ref_type, None)
            if ref is None:
                refs = getattr(obj, ref_type+'s', [])
            else:
                refs = [ref]

            ref_class = self._object_class_map[ref_type]
            for ref in refs:
                ref_obj = ref_class.get(ref)
                if ref_obj is None:
                    return
                self.evaluate(ref_type, ref_obj, obj_type)
    # end evaluate
# end DependencyTracker
