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


class FilterModule(object):

    ZTP_EXCHANGE = 'device_ztp_exchange'
    ZTP_EXCHANGE_TYPE = 'direct'
    CONFIG_FILE_ROUTING_KEY = 'device_ztp.config.file'
    TFTP_FILE_ROUTING_KEY = 'device_ztp.tftp.file'
    ZTP_REQUEST_ROUTING_KEY = 'device_ztp.request'
    ZTP_RESPONSE_ROUTING_KEY = 'device_ztp.response.'

    def filters(self):
        return {
            'ztpcfg': self.get_ztp_config,
            'create_tftp_file': self.create_tftp_file,
            'delete_tftp_file': self.delete_tftp_file,
            'create_dhcp_file': self.create_dhcp_file,
            'delete_dhcp_file': self.delete_dhcp_file,
            'read_dhcp_leases': self.read_dhcp_leases,
            'restart_dhcp_server': self.restart_dhcp_server,
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
                ipam = vncapi.network_ipam_read(id=ipam_ref['uuid'])
                ipam_dict = vncapi.obj_to_dict(ipam)
                ipam_subnets = ipam_dict.get('ipam_subnets')
                if ipam_subnets:
                    ztp_config['ipam_subnets'] = ipam_subnets.get('subnets')
        except NoIdError:
            logging.error("Cannot find mgmt virtual network")
        except Exception as ex:
            logging.error("Error getting ZTP configuration: {}".format(ex))

        return ztp_config
    # end get_ztp_config

    @classmethod
    def create_tftp_file(cls, file_contents, file_name,
                         fabric_name, job_ctx):
        return cls._publish_file(file_name, file_contents, 'create',
            cls.TFTP_FILE_ROUTING_KEY, fabric_name, job_ctx)
    # end create_tftp_file

    @classmethod
    def delete_tftp_file(cls, file_name, fabric_name, job_ctx):
        return cls._publish_file(file_name, '', 'delete', cls.TFTP_FILE_ROUTING_KEY,
            fabric_name, job_ctx)
    # end delete_tftp_file

    @classmethod
    def create_dhcp_file(cls, file_contents, file_name,
                         fabric_name, job_ctx):
        return cls._publish_file(file_name, file_contents, 'create',
            cls.CONFIG_FILE_ROUTING_KEY, fabric_name, job_ctx)
    # end create_dhcp_file

    @classmethod
    def delete_dhcp_file(cls, file_name, fabric_name, job_ctx):
        return cls._publish_file(file_name, '', 'delete', cls.CONFIG_FILE_ROUTING_KEY,
            fabric_name, job_ctx)
    # end delete_dhcp_file

    @classmethod
    def read_dhcp_leases(cls, device_count, ipam_subnets, file_name,
                          fabric_name, job_ctx):
        vnc_api = VncApi(auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
                         auth_token=job_ctx.get('auth_token'))
        headers = {
            'fabric_name': fabric_name,
            'file_name': file_name,
            'action': 'create'
        }
        payload = {
            'device_count': int(device_count),
            'ipam_subnets': ipam_subnets
        }
        vnc_api.amqp_request(exchange=cls.ZTP_EXCHANGE,
            exchange_type=cls.ZTP_EXCHANGE_TYPE,
            routing_key=cls.ZTP_REQUEST_ROUTING_KEY,
            response_key=cls.ZTP_RESPONSE_ROUTING_KEY + fabric_name,
            headers=headers, payload=payload)
        return { 'status': 'success' }
    # end read_dhcp_leases

    @classmethod
    def restart_dhcp_server(cls, file_name, fabric_name, job_ctx):
        vnc_api = VncApi(auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
                         auth_token=job_ctx.get('auth_token'))
        headers = {
            'fabric_name': fabric_name,
            'file_name': file_name,
            'action': 'delete'
        }
        vnc_api.amqp_publish(exchange=cls.ZTP_EXCHANGE,
            exchange_type=cls.ZTP_EXCHANGE_TYPE,
            routing_key=cls.ZTP_REQUEST_ROUTING_KEY, headers=headers,
            payload={})
        return { 'status': 'success' }
    # end restart_dhcp_server

    @classmethod
    def _publish_file(cls, name, contents, action, routing_key,
                      fabric_name, job_ctx):
        vnc_api = VncApi(auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
                         auth_token=job_ctx.get('auth_token'))
        headers = {
            'fabric_name': fabric_name,
            'file_name': name,
            'action': action
        }
        vnc_api.amqp_publish(exchange=cls.ZTP_EXCHANGE,
            exchange_type=cls.ZTP_EXCHANGE_TYPE,
            routing_key=routing_key, headers=headers,
            payload=contents)
        return { 'status': 'success' }
    # end _publish_file
