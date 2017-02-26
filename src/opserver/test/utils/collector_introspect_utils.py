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
from opserver.introspect_util import *


class VerificationCollector(IntrospectUtilBase):
    def __init__(self, ip, port, config=None):
        super(VerificationCollector, self).__init__(ip, port, XmlDrv, config)
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

    def get_db_info(self):
        path = 'Snh_DbInfoGetRequest?'
        xpath = '/DbInfoResponse'
        p = self.dict_get(path)
        return EtreeToDict(xpath).get_all_entry(p)
    # end get_db_info

#end class VerificationCollector
