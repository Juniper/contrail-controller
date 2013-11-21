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


class VerificationCollector(VerificationUtilBase):
    def __init__(self, ip, port):
        super(VerificationCollector, self).__init__(ip, port, XmlDrv)
    #end __init__

    def get_generators(self):
        path = 'Snh_ShowCollectorServerReq'
        xpath = '/ShowCollectorServerResp/generators'
        p = self.dict_get(path)
        return EtreeToDict(xpath).get_all_entry(p)
    #end get_collector_connection_status

    def get_redis_uve_master(self):
        path = 'Snh_RedisUVEMasterRequest'
        xpath = '/RedisUVEMasterResponse/redis_uve_master'
        p = self.dict_get(path)
        return EtreeToDict(xpath).get_all_entry(p)
    # end get_redis_uve_master

#end class VerificationCollector
