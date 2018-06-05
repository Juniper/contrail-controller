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

TFTP_SERVER = 'b2s32.englab.juniper.net'
DHCP_SERVER = 'b2s32.englab.juniper.net'

class FilterModule(object):

    def filters(self):
        return {
            'ztpcfg': self.get_ztp_config,
        }

    @classmethod
    def get_ztp_config(cls, job_ctx, fabric_uuid):
        ztp_config = {}
        try:
            vncapi = VncApi(auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
                            auth_token=job_ctx.get('auth_token'))
            fabric = vncapi.fabric_read(id=fabric_uuid)
            fabric_dict = vncapi.obj_to_dict(fabric)
            fabric_creds = fabric_dict.get('fabric_credentials')
            if fabric_creds:
                device_creds = fabric_creds.get('device_credential')
                if device_creds:
                    dev_cred = device_creds[0]
                    ztp_config['password'] = dev_cred['credential']['password']

            # From here we get the 'management' type virtual network
            vn_uuid = None
            virtual_network_refs = fabric_dict.get('virtual_network_refs') or []
            for virtual_net_ref in virtual_network_refs:
                if "management" in virtual_net_ref['attr']['network_type']:
                    vn_uuid = virtual_net_ref['uuid']
                    break
            if vn_uuid is None:
                raise NoIdError("Cannot find mgmt virtual network on fabric")

            virtual_net = vncapi.virtual_network_read(id=vn_uuid)
            virtual_net_dict = vncapi.obj_to_dict(virtual_net)

            # Get the IPAM attached to the virtual network
            ipam_refs = virtual_net_dict.get('network_ipam_refs')
            if ipam_refs:
                ipam_ref = ipam_refs[0]
                ipam_subnets = ipam_ref['attr'].get('ipam_subnets')
                if ipam_subnets:
                    ztp_config['ipam_subnets'] = ipam_subnets
        except NoIdError:
            logging.error("Cannot find mgmt virtual network")
        except Exception as ex:
            logging.error("Error getting ZTP configuration: {}".format(ex))

        return ztp_config
