#!/usr/bin/python

#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains code to gather IPAM config from the fabric management virtual network
"""

from cfgm_common.exceptions import (
    RefsExistError,
    NoIdError
)
from vnc_api.vnc_api import VncApi
import logging

VIRTUAL_NETWORK_UUID = 'e704945c-d035-46bf-9d6e-850a14e58f05'

class FilterModule(object):

    def filters(self):
        return {
            'ipam': self.get_ipam_config,
        }

    @classmethod
    def get_ipam_config(cls, job_ctx, fabric_uuid):
        ipam_config = {}
        try:
            vncapi = VncApi(auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
                            auth_token=job_ctx.get('auth_token'))
            fabric = vncapi.fabric_read(id=fabric_uuid)
            fabric_dict = vncapi.obj_to_dict(fabric)
  
            # From here we get the 'management' type virtual network

            # TEMP: hardcode virtual network ID until schema changes checked in
            vn_uuid = VIRTUAL_NETWORK_UUID
            virtual_net = vncapi.virtual_network_read(id=vn_uuid)
            virtual_net_dict = vncapi.obj_to_dict(virtual_net)

            # Get the IPAM attached to the virtual network

            ipam_refs = virtual_net_dict.get('network_ipam_refs')
            if ipam_refs:
                ipam_ref = ipam_refs[0]
                ipam_subnets = ipam_ref['attr'].get('ipam_subnets')
                if ipam_subnets:
                    ipam_config['ipam_subnets'] = ipam_subnets
        except NoIdError:
            logging.error("Cannot find mgmt virtual network")
        except Exception as ex:
            logging.error("Error getting IPAM configuration: {}".format(ex))

        return ipam_config

