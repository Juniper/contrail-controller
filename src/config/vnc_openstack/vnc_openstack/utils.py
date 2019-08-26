#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

def filter_fields(resource, fields):
    if fields:
        return dict(((key, item) for key, item in resource.items()
                     if key in fields))
    return resource


def resource_is_in_use(resource):
    backref_map = resource.backref_field_types
    for backref_field, (_, _, is_derived) in backref_map.items():
        if is_derived:
            continue
        if getattr(resource, 'get_%s' % backref_field)():
            return True
    return False
