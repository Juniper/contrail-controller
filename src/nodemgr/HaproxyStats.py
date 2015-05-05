import os
import socket
import sys

LB_BASE_DIR = '/var/lib/contrail/loadbalancer/'

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

STATS_NAME = 'name'

FRONTEND_REQUEST = 1
BACKEND_REQUEST  = 2
SERVER_REQUEST   = 4

class HaproxyStats(object):
    def __init__(self):
        pass

    def get_stats(self, pool_id):
        sock_path = os.path.join(LB_BASE_DIR, pool_id, 'sock')
        if not os.path.exists(sock_path):
            sys.stderr.write('\nStats socket not found for pool ' + pool_id)
            return {}

        parsed_stats = self._get_stats_from_socket(sock_path)
        lb_stats = {}
        lb_stats['vip'] = self._get_frontend_stats(parsed_stats)
        lb_stats['pool'] = self._get_backend_stats(parsed_stats)
        lb_stats['members'] = self._get_member_stats(parsed_stats)
        return lb_stats

    def _get_frontend_stats(self, parsed_stats):
        TYPE_FRONTEND_RESPONSE = '0'
        for stats in parsed_stats:
            if stats.get('type') == TYPE_FRONTEND_RESPONSE:
                unified_stats = dict((k, stats.get(v, ''))
                                     for k, v in STATS_MAP.items())
                unified_stats[STATS_NAME] = stats['pxname']
                if unified_stats['status'] in ['no check', 'UP', 'OPEN']:
                    unified_stats['status'] = 'ACTIVE'
                else:
                    unified_stats['status'] = 'DOWN'
                return unified_stats

        return {}

    def _get_backend_stats(self, parsed_stats):
        TYPE_BACKEND_RESPONSE = '1'
        for stats in parsed_stats:
            if stats.get('type') == TYPE_BACKEND_RESPONSE:
                unified_stats = dict((k, stats.get(v, ''))
                                     for k, v in STATS_MAP.items())
                unified_stats[STATS_NAME] = stats['pxname']
                if unified_stats['status'] in ['no check', 'UP', 'OPEN']:
                    unified_stats['status'] = 'ACTIVE'
                else:
                    unified_stats['status'] = 'DOWN'
                return unified_stats

        return {}

    def _get_member_stats(self, parsed_stats):
        TYPE_SERVER_RESPONSE = '2'
        res = {}
        for stats in parsed_stats:
            if stats.get('type') == TYPE_SERVER_RESPONSE:
                member_stats = dict((k, stats.get(v, ''))
                                    for k, v in STATS_MAP.items())
                member_stats[STATS_NAME] = stats['svname']
                if member_stats['status'] in ['no check', 'UP', 'OPEN']:
                    member_stats['status'] = 'ACTIVE'
                else:
                    member_stats['status'] = 'DOWN'
                res[stats['svname']] = member_stats
        return res

    def _get_stats_from_socket(self, socket_path):
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(socket_path)
            request_type = FRONTEND_REQUEST | BACKEND_REQUEST | SERVER_REQUEST
            s.send('show stat -1 %s -1\n' % request_type)
            raw_stats = ''
            chunk_size = 1024
            while True:
                chunk = s.recv(chunk_size)
                raw_stats += chunk
                if len(chunk) < chunk_size:
                    break

            return self._parse_stats(raw_stats)
        except socket.error as e:
            sys.stderr.write('\nError while connecting to stats socket: ' + str(e))
            return {}

    def _parse_stats(self, raw_stats):
        stat_lines = raw_stats.splitlines()
        if len(stat_lines) < 2:
            return []
        stat_names = [name.strip('# ') for name in stat_lines[0].split(',')]
        res_stats = []
        for raw_values in stat_lines[1:]:
            if not raw_values:
                continue
            stat_values = [value.strip() for value in raw_values.split(',')]
            res_stats.append(dict(zip(stat_names, stat_values)))

        return res_stats
