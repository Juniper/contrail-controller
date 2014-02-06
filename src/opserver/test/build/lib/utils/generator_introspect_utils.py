#!/usr/bin/env python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# generator_introspect_utils.py
#
# Python generator introspect utils
#

from lxml import etree
from verification_util import *


class VerificationGenerator(VerificationUtilBase):
    def __init__(self, ip, port):
        super(VerificationGenerator, self).__init__(ip, port, XmlDrv)
    #end __init__

    def get_collector_connection_status(self):
        path = 'Snh_CollectorInfoRequest'
        xpath = '/CollectorInfoResponse/status'
        p = self.dict_get(path)
        return EtreeToDict(xpath).get_all_entry(p)
    #end get_collector_connection_status

    def get_uve(self, tname):
        path = 'Snh_SandeshUVECacheReq?x=%s' % (tname)
        xpath = './/' + tname
        p = self.dict_get(path)
        return EtreeToDict(xpath).get_all_entry(p)
    #end get_uve
#end class VerificationGenerator
