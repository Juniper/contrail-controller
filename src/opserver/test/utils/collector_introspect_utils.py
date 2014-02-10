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

    def get_redis_uve_info(self):
        path = 'Snh_RedisUVERequest'
        xpath = '/RedisUVEResponse/redis_uve_info'
        p = self.dict_get(path)
        return EtreeToDict(xpath).get_all_entry(p)
    # end get_redis_uve_info

#end class VerificationCollector
