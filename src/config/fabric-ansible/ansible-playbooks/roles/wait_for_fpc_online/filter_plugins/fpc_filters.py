#!/usr/bin/python

#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from builtins import object


class FilterModule(object):
    def filters(self):
        return {
            'is_juniper_device_online': self.is_juniper_device_online
        }
    # end filters

    @classmethod
    def is_juniper_device_online(cls, fpc_pic_status_info):
        if not fpc_pic_status_info or \
                not fpc_pic_status_info.get('fpc-information') or \
                not fpc_pic_status_info.get('fpc-information').get('fpc'):
            return False
        fpc_slots = fpc_pic_status_info.get('fpc-information').get('fpc')
        if isinstance(fpc_slots, dict):
            fpc_slots = [fpc_slots]
        for fpc in fpc_slots:
            if not fpc.get('state') or fpc.get('state').lower() != 'online':
                return False
            pic_slots = fpc.get('pic')
            if pic_slots is None:
                return False
            if isinstance(pic_slots, dict):
                pic_slots = [pic_slots]
            for pic in pic_slots:
                if not pic.get('pic-state') or \
                        pic.get('pic-state').lower() != 'online':
                    return False
        return True
    # end is_juniper_device_online
# end FilterModule
