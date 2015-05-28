import os
import sys

from haproxy_stats import HaproxyStats
from vrouter.loadbalancer.ttypes import \
    UveLoadbalancerTrace, UveLoadbalancer, UveLoadbalancerStats  

LB_BASE_DIR = '/var/lib/contrail/loadbalancer/'

def _uve_get_stats(stats):
    obj_stats = UveLoadbalancerStats()
    obj_stats.obj_name = stats['name']
    obj_stats.uuid = stats['name']
    obj_stats.status = stats['status']

    for attr in dir(obj_stats):
        if attr in stats and stats[attr].isdigit():
            setattr(obj_stats, attr, int(stats[attr]))

    return [obj_stats]

def _uve_get_member_stats(stats):
    member_stats = []
    for stat in stats:
        obj_stats = UveLoadbalancerStats()
        obj_stats.obj_name = stat['name']
        obj_stats.uuid = stat['name']
        obj_stats.status = stat['status']
        for attr in dir(obj_stats):
            if attr in stat and stat[attr].isdigit():
                setattr(obj_stats, attr, int(stat[attr]))
        member_stats.append(obj_stats)
    return member_stats

def _send_loadbalancer_uve(driver):
    try:
        pool_uuids = os.listdir(LB_BASE_DIR)
    except OSError:
        return

    for pool_uuid in pool_uuids:
        stats = driver.get_stats(pool_uuid)
        if not len(stats) or not stats.get('vip') or not stats.get('pool'):
            continue

        uve_lb = UveLoadbalancer()
        uve_lb.name = pool_uuid
        uve_lb.virtual_ip_stats = _uve_get_stats(stats['vip'])
        uve_lb.pool_stats = _uve_get_stats(stats['pool'])
        uve_lb.member_stats = _uve_get_member_stats(stats['members'])
        uve_trace = UveLoadbalancerTrace(data=uve_lb)
        uve_trace.send()

def send_loadbalancer_stats():
    driver = HaproxyStats()
    try:
        _send_loadbalancer_uve(driver)
    except Exception as e:
        sys.stderr.write('LB stats failure ' + str(e) + '\n')
