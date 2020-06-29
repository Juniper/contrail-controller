import os
import socket
import csv
from pysandesh.sandesh_logger import SandeshLogger
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

HAPROXY_DIR = '/var/lib/contrail/loadbalancer/haproxy/'

STATS_MAP = {
    'active_connections': 'qcur',
    'max_connections': 'qmax',
    'current_sessions': 'scur',
    'max_sessions': 'smax',
    'total_sessions': 'stot',
    'bytes_in': 'bin',
    'bytes_out': 'bout',
    'connection_errors': 'econ',
    'response_errors': 'eresp',
    'status': 'status',
    'health': 'check_status',
    'failed_checks': 'chkfail'
}

# 1 + 2 + 4 = 7 for frontend + backend + server
REQUEST_TYPE = 7

# response types
TYPE_FRONTEND_RESPONSE = '0'
TYPE_BACKEND_RESPONSE = '1'
TYPE_SERVER_RESPONSE = '2'


class HaproxyStats(object):
    def __init__(self, logger, host_ip, hostname=None):
        self.lbaas_dir = HAPROXY_DIR
        self.logger = logger
        self.host_ip = host_ip
        self.hostname = hostname
        pass

    def msg_log(self, msg, level):
        self.logger.log(SandeshLogger.get_py_logger_level(level), msg)

    def get_stats(self, pool_id):
        sock_path = os.path.join(self.lbaas_dir, pool_id, 'haproxy.sock')
        if not os.path.exists(sock_path):
            self.msg_log('Stats socket not found for pool ' + pool_id, SandeshLevel.SYS_ERR)
            return {}

        lb_stats = {}
        lb_stats.setdefault('listener', [])
        lb_stats.setdefault('pool', [])
        lb_stats.setdefault('member', [])
        raw_stats = self._read_stats(sock_path)
        row_count = 0
        for row in csv.DictReader(raw_stats.lstrip('# ').splitlines()):
            row_count = row_count + 1
            if row.get('type') == TYPE_FRONTEND_RESPONSE:
                lb_stats['listener'].append(self._get_stats(row, row['pxname']))
            elif row.get('type') == TYPE_BACKEND_RESPONSE:
                lb_stats['pool'].append(self._get_stats(row, row['pxname']))
            elif row.get('type') == TYPE_SERVER_RESPONSE:
                lb_stats['member'].append(self._get_stats(row, row['svname']))
        if (row_count == 0):
            return {}
        return lb_stats

    def _get_stats(self, row, name):
        stats = dict((k, row.get(v, ''))
                     for k, v in STATS_MAP.items())
        stats['name'] = name
        stats['vrouter'] = socket.getfqdn(self.host_ip) if self.hostname is None \
            else self.hostname
        if stats['status'] in ['no check', 'UP', 'OPEN']:
            stats['status'] = 'ACTIVE'
        else:
            stats['status'] = 'DOWN'
        return stats

    def _read_stats(self, socket_path):
        raw_stats = ''
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(socket_path)
            s.send('show stat -1 %s -1\n' % REQUEST_TYPE)
            chunk_size = 1024
            while True:
                chunk = s.recv(chunk_size)
                raw_stats += chunk
                if len(chunk) < chunk_size:
                    break
        except socket.error as e:
            self.msg_log('stats socket error: ' + str(e), SandeshLevel.SYS_ERR)

        return raw_stats
