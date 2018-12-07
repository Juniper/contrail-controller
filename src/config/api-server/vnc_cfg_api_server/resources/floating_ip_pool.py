#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import FloatingIpPool

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class FloatingIpPoolServer(ResourceMixin, FloatingIpPool):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        #
        # Floating-ip-pools corresponding to a virtual-network can be
        # 'optionally' configured to be specific to a ipam subnet on a virtual-
        # network.
        #
        # If the subnet is specified, sanity check the config to verify that
        # the subnet exists in the virtual-network.
        #

        # If subnet info is not specified in the floating-ip-pool object, then
        # there is nothing to validate. Just return.
        try:
            if (obj_dict['parent_type'] != 'virtual-network' or
                obj_dict['floating_ip_pool_subnets'] is None or
                obj_dict['floating_ip_pool_subnets']['subnet_uuid'] is None or
                    not obj_dict['floating_ip_pool_subnets']['subnet_uuid']):
                return True, ""
        except (KeyError, TypeError):
            return True, ""

        try:
            # Get the virtual-network object.
            vn_fq_name = obj_dict['fq_name'][:-1]
            vn_id = db_conn.fq_name_to_uuid('virtual_network', vn_fq_name)
            ok, vn_dict = cls.dbe_read(db_conn, 'virtual_network', vn_id)
            if not ok:
                return ok, vn_dict

            # Iterate through configured subnets on this FloatingIpPool.
            # Validate the requested subnets are found on the virtual-network.
            for fip_pool_subnet in \
                    obj_dict['floating_ip_pool_subnets']['subnet_uuid']:
                vn_ipams = vn_dict['network_ipam_refs']
                subnet_found = False
                for ipam in vn_ipams:
                    if not ipam['attr']:
                        continue
                    for ipam_subnet in ipam['attr']['ipam_subnets']:
                        if ipam_subnet['subnet_uuid']:
                            if ipam_subnet['subnet_uuid'] != fip_pool_subnet:
                                # Subnet uuid does not match. This is not the
                                # requested subnet. Keep looking.
                                continue

                            # Subnet of interest was found.
                            subnet_found = True
                            break

                if not subnet_found:
                    # Specified subnet was not found on the virtual-network.
                    # Return failure.
                    msg = "Subnet %s was not found in virtual-network %s" %\
                        (fip_pool_subnet, vn_id)
                    return (False, (400, msg))

        except KeyError:
            msg = "Incomplete info to create a floating-ip-pool"
            return False, (400, msg)

        return True, ''
