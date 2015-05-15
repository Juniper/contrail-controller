import itertools
import os
import shlex
import subprocess
import haproxy_config
from opencontrail_vrouter_netns.linux import child_monitor

SUPERVISOR_BASE_DIR = '/etc/contrail/supervisord_vrouter_files/lbaas-haproxy-'

def stop_haproxy(ip_ns, conf_file, daemon_mode=False):
    pool_id = os.path.split(os.path.dirname(conf_file))[1]
    try:
        if daemon_mode:
            _stop_haproxy_daemon(ip_ns, pool_id, conf_file)
        else:
            _stop_supervisor_haproxy(pool_id)
    except Exception as e:
        pass

def start_update_haproxy(ip_ns, conf_file, daemon_mode=False):
    netns = ip_ns.namespace
    pool_id = os.path.split(os.path.dirname(conf_file))[1]
    haproxy_cfg_file = haproxy_config.build_config(conf_file)
    try:
        if daemon_mode:
            _start_haproxy_daemon(ip_ns, pool_id, haproxy_cfg_file)
        else:
            _start_supervisor_haproxy(pool_id, netns, haproxy_cfg_file)
    except Exception as e:
        pass

def _get_lbaas_pid(ip_ns, pool_id, conf_file):
    cmd = "pidof haproxy"
    out = ip_ns.netns.execute(shlex.split(cmd), check_exit_code=False)
    pids = shlex.split(out)
    for pid in pids:
        with open("/proc/%s/cmdline" % pid, "r") as fd:
            if pool_id in fd.read():
                return pid

def _stop_haproxy_daemon(ip_ns, pool_id, conf_file):
    last_pid = _get_lbaas_pid(ip_ns, pool_id, conf_file)
    if last_pid:
        cmd_list = shlex.split('kill -10 ' + last_pid)
        ip_ns.netns.execute(cmd_list)

def _start_haproxy_daemon(ip_ns, pool_id, conf_file):
    last_pid = _get_lbaas_pid(ip_ns, pool_id, conf_file)
    if last_pid:
        sf_opt = '-sf ' + last_pid
    else:
        sf_opt = ''
    conf_dir = os.path.dirname(conf_file)
    pid_file = conf_dir + '/haproxy.pid'

    cm_file = child_monitor.__file__
    cmd = 'python %s ' % cm_file
    cmd += ('haproxy -f %s -p %s -db %s' %
        (conf_file, pid_file, sf_opt))
    cmd_list = shlex.split(cmd)
    ip_ns.netns.execute(cmd_list)

def _stop_supervisor_haproxy(pool_id):
    pool_suffix = _get_pool_suffix(pool_id)
    file_name = SUPERVISOR_BASE_DIR + pool_suffix + '.ini'
    cmd = "rm " + file_name
    cmd_list = shlex.split(cmd)
    subprocess.Popen(cmd_list)
    _update_supervisor()

def _start_supervisor_haproxy(pool_id, netns, conf_file):
    data = []
    data.extend(_set_config(pool_id, netns, conf_file))
    pool_suffix = _get_pool_suffix(pool_id)
    with open(SUPERVISOR_BASE_DIR + pool_suffix + '.ini', "w") as f:
        f.write('\n'.join(data) + '\n')
    _update_supervisor()

def _get_pool_suffix(pool_id):
    return pool_id.split('-')[0]

def _update_supervisor():
    cmd = "supervisorctl -s unix:///tmp/supervisord_vrouter.sock update"
    cmd_list = shlex.split(cmd)
    subprocess.Popen(cmd_list)

def _set_config(pool_id, netns, conf_file):
    pool_suffix = _get_pool_suffix(pool_id)
    program_name = 'lbaas-haproxy-%s' % pool_suffix
    cmd = "supervisorctl -s unix:///tmp/supervisord_vrouter.sock pid "
    cmd += program_name
    cmd_list = shlex.split(cmd)
    p = subprocess.Popen(cmd_list, stdout=subprocess.PIPE)
    last_pid, err = p.communicate()
    try:
        int(last_pid)
        sf_opt = '-sf ' + last_pid
    except ValueError:
        sf_opt = ''

    opts = [
        '[program:%s]' % program_name,
        'command=ip netns exec %s haproxy -f %s -db %s' % \
            (netns, conf_file, sf_opt),
        'priority=420',
        'autostart=true',
        'killasgroup=true',
        'stdout_capture_maxbytes=1MB',
        'redirect_stderr=true',
        'stdout_logfile=/var/log/contrail/lbaas-haproxy-stdout.log',
        'stderr_logfile=/dev/null',
        'startsecs=5',
        'exitcodes=0'
    ]

    return itertools.chain(o for o in opts)
