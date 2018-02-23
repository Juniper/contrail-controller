#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of dependency tracker
for contrail config daemons
"""

from collections import OrderedDict

# This class tracks dependencies among different objects based on a reaction map.
# Objects could be derived from DBBase. Each object has an object_type and the
# mapping from object_type to the class is specified using object_class_map
class DependencyTracker(object):

    def __init__(self, object_class_map, reaction_map):
        self._reaction_map = reaction_map
        self._object_class_map = object_class_map
        self.resources = OrderedDict()
    # end __init__

    def _add_resource(self, obj_type, obj_key):
        if obj_type in self.resources:
            if obj_key in self.resources[obj_type]:
                # already visited
                return False
            self.resources[obj_type].append(obj_key)
        else:
            self.resources[obj_type] = [obj_key]
        return True
    # end _add_resource

    def evaluate(self, obj_type, obj, from_type='self'):
        if obj_type not in self._reaction_map:
            return
        if not self._add_resource(obj_type, obj.get_key()):
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
