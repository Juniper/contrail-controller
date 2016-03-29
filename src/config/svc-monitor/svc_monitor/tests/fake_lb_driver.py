#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import svc_monitor.services.loadbalancer.drivers.abstract_driver as abstract_driver

class OpencontrailFakeLoadbalancerDriver(
        abstract_driver.ContrailLoadBalancerAbstractDriver):
    def __init__(self, name, manager, api, db, args=None):
        self._name = name
        self._api = api
        self._svc_manager = manager
        self._lb_template = None
        self.db = db
        self._pools = {}
        self._members = {}
        self._vips = {}
        self._hms = {}
        self._hms_pools = {}

    def create_vip(self, vip):
        self._vips[vip['id']] = vip
        pass

    def update_vip(self, old_vip, vip):
        self._vips[vip['id']] = vip
        pass

    def delete_vip(self, vip):
        del self._vips[vip['id']]
        pass

    def create_pool(self, pool):
        self._pools[pool['id']] = pool
        self.db.pool_driver_info_insert(pool['id'], {'fake-driver-data': 'junk'})
        pass

    def update_pool(self, old_pool, pool):
        self._pools[pool['id']] = pool
        pass

    def delete_pool(self, pool):
        del self._pools[pool['id']]
        self.db.pool_remove(pool['id'], ['fake-driver-data'])
        pass

    def create_member(self, member):
        self._members[member['id']] = member
        pass

    def update_member(self, old_member, member):
        self._members[member['id']] = member
        pass

    def delete_member(self, member):
        del self._members[member['id']]
        pass

    def update_pool_health_monitor(self,
                                   old_health_monitor,
                                   health_monitor,
                                   pool_id):
        pass

    def create_pool_health_monitor(self,
                                   health_monitor,
                                   pool_id):
        if health_monitor['id'] not in self._hms:
            self._hms_pools[health_monitor['id']] = set()
        self._hms_pools[health_monitor['id']].add(pool_id)
        self._hms[health_monitor['id']] = health_monitor
        pass

    def delete_pool_health_monitor(self, health_monitor, pool_id):
        if health_monitor['id'] not in self._hms:
            raise ValueError("could not find %s" % (health_monitor['id']))
        self._hms_pools[health_monitor['id']].remove(pool_id)
        if self._hms_pools[health_monitor['id']].empty():
            del self._hms[health_monitor['id']]
            del self._hms_pools[health_monitor['id']]
        else:
            self._hms[health_monitor['id']] = health_monitor
        pass

    def update_health_monitor(self, id, health_monitor):
        self._hms[id] = health_monitor
        pass

    def stats(self, pool_id):
        pass

    def set_config_v1(self, pool_id):
        pass

    def set_config_v2(self, lb_id):
        pass
