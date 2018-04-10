#!/usr/bin/python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
This file containamespace sanity test for all major workflows supported by
fabric ansible
"""

from sanity.sanity_base import SanityBase


# pylint: disable=E1101
class SanityTest01(SanityBase):
    """
    Sanity test on full EMS workflows:
     - device discovery
     - device import
     - device underlay config
    """

    def __init__(self, prouter_ip, prouter_password, vnc_password):
        SanityBase.__init__(self, "sanity_test_01", vnc_password)
        self._prouter_ip = prouter_ip
        self._prouter_password = prouter_password
    # end __init__

    def _validate_discovered_prouters(self, prouters):
        assert len(prouters) == 1
        assert prouters[0].physical_router_management_ip == self._prouter_ip
        assert prouters[0].physical_router_user_credentials.username == 'root'
        assert prouters[0].physical_router_vendor_name == 'juniper'
        assert prouters[0].physical_router_product_name
        assert prouters[0].physical_router_device_family
    # end _validate_discovered_prouters

    def _validate_imported_prouters(self, prouters):
        for prouter in prouters:
            ifd_refs = self._api.physical_interfaces_list(
                parent_id=prouter.uuid)
            assert ifd_refs.get('physical-interfaces') > 0
    # end _validate_imported_prouters

    def _validate_underlay_config(self, prouters):
        for prouter in prouters:
            prouter = self._api.physical_router_read(prouter.fq_name)
            bgp_router_refs = prouter.get_bgp_router_refs() or []
            assert len(bgp_router_refs) == 1
    # end _validate_underlay_config

    def test(self):
        try:
            self.delete_fabric('fab01')
            fabric = self.create_fabric('fab01', [self._prouter_password])
            self.add_mgmt_ip_namespace(fabric, self._prouter_ip + '/32')
            self.add_asn_namespace(fabric, 64512)

            prouters = self.discover_fabric_device(fabric)
            self._validate_discovered_prouters(prouters)

            self.device_import(prouters)
            self._validate_imported_prouters(prouters)

            self.underlay_config(prouters)
            self._validate_underlay_config(prouters)

        except Exception as ex:
            self._exit_with_error(
                "Test failed due to unexpected error: %s" % str(ex))
    # end test


if __name__ == "__main__":
    SanityTest01('10.155.67.5', 'Embe1mpls', 'contrail123').test()
# end __main__
