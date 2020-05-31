#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from builtins import str


class VncCommon(object):
    """VNC kubernetes common functionality.
    """
    def __init__(self, kube_obj_kind):
        self.annotations = {}
        self.annotations['kind'] = kube_obj_kind

    def get_annotations(self):
        return self.annotations

    @staticmethod
    def make_name(*args):
        return "__".join(str(i) for i in args)

    @staticmethod
    def make_display_name(*args):
        return "__".join(str(i) for i in args)
