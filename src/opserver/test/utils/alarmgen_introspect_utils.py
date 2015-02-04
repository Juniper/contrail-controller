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


class VerificationAlarmGen(VerificationUtilBase):
    def __init__(self, ip, port):
        super(VerificationAlarmGen, self).__init__(ip, port, XmlDrv)
    #end __init__

    def get_PartitionStatus(self, partno):
        path = 'Snh_PartitionStatusReq?partition=%d' % partno
        xpath = '/PartitionStatusResp'
        p = self.dict_get(path)
        return EtreeToDict(xpath).get_all_entry(p)

    def get_PartitionOwnership(self, partno, status):
        path = 'Snh_PartitionOwnershipReq?partition=%d&ownership=%d' % \
               (partno, status)
        xpath = '/PartitionOwnershipResp/status'
        p = self.dict_get(path)
        return EtreeToDict(xpath).get_all_entry(p)

#end class VerificationAlarmGen
