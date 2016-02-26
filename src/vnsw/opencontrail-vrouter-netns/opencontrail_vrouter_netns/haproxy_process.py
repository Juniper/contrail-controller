import itertools
import os
import shlex
import subprocess
import haproxy_config

SUPERVISOR_BASE_DIR = '/etc/contrail/supervisord_vrouter_files/lbaas-haproxy-'

def get_pid_file_from_conf_file(conf_file):
    dir_name = os.path.dirname(conf_file)
    sout = os.path.split(dir_name)
    pid_file = sout[0] + "/" + sout[1] + ".haproxy.pid"
    return pid_file

def delete_haproxy_pid_file(conf_file):
    pid_file = get_pid_file_from_conf_file(conf_file)
    if os.path.isfile(pid_file):
        cmd = "rm " + pid_file
        cmd_list = shlex.split(cmd)
        subprocess.Popen(cmd_list)

def stop_haproxy(conf_file, daemon_mode=False):
    try:
        if daemon_mode:
            _stop_haproxy_daemon(conf_file)
        else:
            pool_id = os.path.split(os.path.dirname(conf_file))[1]
            _stop_supervisor_haproxy(pool_id)
    except Exception as e:
        pass

    delete_haproxy_pid_file(conf_file)


def start_update_haproxy(conf_file, netns, daemon_mode=False,
                         keystone_auth_conf_file=None):
    pool_id = os.path.split(os.path.dirname(conf_file))[1]
    haproxy_cfg_file = haproxy_config.build_config(conf_file, \
                                      keystone_auth_conf_file)
    try:
        if daemon_mode:
            _start_haproxy_daemon(pool_id, netns, haproxy_cfg_file)
        else:
            _start_supervisor_haproxy(pool_id, netns, haproxy_cfg_file)
    except Exception as e:
        pass

def _get_lbaas_pid(conf_file):
    pid_file = get_pid_file_from_conf_file(conf_file)
    if not os.path.isfile(pid_file):
        return None
    cmd = 'cat %s' % pid_file
    cmd_list = shlex.split(cmd)
    p = subprocess.Popen(cmd_list, stdout=subprocess.PIPE)
    pid, err = p.communicate()
    if err:
        return None
    return pid

def _stop_haproxy_daemon(conf_file):
    last_pid = _get_lbaas_pid(conf_file)
    if last_pid:
        cmd_list = shlex.split('kill -9 ' + last_pid)
        subprocess.Popen(cmd_list)

def _start_haproxy_daemon(pool_id, netns, conf_file):
    last_pid = _get_lbaas_pid(conf_file)
    if last_pid:
        sf_opt = '-sf ' + last_pid
    else:
        sf_opt = ''

    pid_file = get_pid_file_from_conf_file(conf_file)

    cmd = 'ip netns exec %s haproxy -f %s -p %s %s' % \
        (netns, conf_file, pid_file, sf_opt)
    cmd_list = shlex.split(cmd)
    subprocess.Popen(cmd_list)

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
