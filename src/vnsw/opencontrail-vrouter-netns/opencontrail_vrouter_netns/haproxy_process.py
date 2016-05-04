import itertools
import os
import shlex
import subprocess
import logging
import cert_mgr.barbican_cert_manager as barbican_cert_mgr

HAPROXY_DIR = "/var/lib/contrail/loadbalancer/haproxy"
HAPROXY_PROCESS = 'haproxy'
HAPROXY_PROCESS_CONF = HAPROXY_PROCESS + ".conf"
SUPERVISOR_BASE_DIR = '/etc/contrail/supervisord_vrouter_files/lbaas-haproxy-'
LOG_FILE= '/var/log/contrail/contrail-lb-haproxy-stdout.log'

logging.basicConfig(level=logging.WARNING,
                    format='%(asctime)s %(levelname)-8s %(message)s',
                    datefmt='%m/%d/%Y %H:%M:%S',
                    filename=LOG_FILE)

def delete_haproxy_dir(base_dir, loadbalancer_id):
    dir_name = base_dir + "/" +  loadbalancer_id
    cmd = "rm -rf " + dir_name
    cmd_list = shlex.split(cmd)
    p = subprocess.Popen(cmd_list)
    p.communicate()

def create_haproxy_dir(base_dir, loadbalancer_id):
    dir_name = base_dir + "/" + loadbalancer_id
    cmd = "mkdir -p " + dir_name
    cmd_list = shlex.split(cmd)
    p = subprocess.Popen(cmd_list)
    p.communicate()
    return dir_name

def get_haproxy_config_file(cfg_file, dir_name):
    f = open(cfg_file)
    content = f.read()
    f.close()
    kvps = content.split(':::::')
    for kvp in kvps or []:
        KeyValue = kvp.split('::::')
        if (KeyValue[0] == 'haproxy_config'):
            break;
    haproxy_cfg_file = dir_name + "/" + HAPROXY_PROCESS_CONF
    f = open(haproxy_cfg_file, 'w+')
    f.write(KeyValue[1])
    f.close()
    updated_conf = barbican_cert_mgr.update_ssl_conf(haproxy_cfg_file)
    if updated_conf is None:
        return None

    return haproxy_cfg_file

def get_pid_file_from_conf_file(conf_file):
    dir_name = os.path.dirname(conf_file)
    pid_file = dir_name + "/" + "haproxy.pid"
    return pid_file

def stop_haproxy(loadbalancer_id, daemon_mode=False):
    conf_file = HAPROXY_DIR + "/" +  loadbalancer_id + "/" + HAPROXY_PROCESS_CONF
    try:
        if daemon_mode:
            _stop_haproxy_daemon(loadbalancer_id, conf_file)
        else:
            pool_id = os.path.split(os.path.dirname(conf_file))[1]
            _stop_supervisor_haproxy(pool_id)
    except Exception as e:
        msg = "Exception in Stopping haproxy for Loadbalancer-ID %s" %loadbalancer_id
        logging.exception(msg)
        logging.error(e.__class__)
        logging.error(e.__doc__)
        logging.error(e.message)

    delete_haproxy_dir(HAPROXY_DIR, loadbalancer_id)

def start_update_haproxy(loadbalancer_id, cfg_file,
        netns, daemon_mode=False, keystone_auth_conf_file=None):
    try:
        pool_id = loadbalancer_id
        dir_name = create_haproxy_dir(HAPROXY_DIR, loadbalancer_id)
        haproxy_cfg_file = get_haproxy_config_file(cfg_file, dir_name)
        if haproxy_cfg_file is None:
            msg = "Failed to Create haproxy config for Loadbalancer-ID %s" %loadbalancer_id
            logging.error(msg)
            stop_haproxy(loadbalancer_id, daemon_mode)
            return False
    except Exception as e:
        msg = "Exception in Createing haproxy config for Loadbalancer-ID %s" %loadbalancer_id
        logging.exception(msg)
        logging.error(e.__class__)
        logging.error(e.__doc__)
        logging.error(e.message)
        stop_haproxy(loadbalancer_id, daemon_mode)
        return False
    try:
        if daemon_mode:
            _start_haproxy_daemon(pool_id, netns, haproxy_cfg_file)
        else:
            _start_supervisor_haproxy(pool_id, netns, haproxy_cfg_file)
    except Exception as e:
        msg = "Exception in Starting/Updating haproxy for Loadbalancer-ID %s" %loadbalancer_id
        logging.exception(msg)
        logging.error(e.__class__)
        logging.error(e.__doc__)
        logging.error(e.message)
        return False
    return True

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

def _stop_haproxy_daemon(loadbalancer_id, conf_file):
    last_pid = _get_lbaas_pid(conf_file)
    if last_pid:
        cmd_list = shlex.split('kill -9 ' + last_pid)
        subprocess.Popen(cmd_list)
        msg = "Stopping haproxy for Loadbalancer-ID %s" %loadbalancer_id
        logging.info(msg)

def _start_haproxy_daemon(pool_id, netns, conf_file):
    loadbalancer_id = pool_id
    last_pid = _get_lbaas_pid(conf_file)
    if last_pid:
        msg = "Updating haproxy for Loadbalancer-ID %s" %loadbalancer_id
        sf_opt = '-sf ' + last_pid
    else:
        msg = "Starting haproxy for Loadbalancer-ID %s" %loadbalancer_id
        sf_opt = ''

    pid_file = get_pid_file_from_conf_file(conf_file)
    logging.info(msg)

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
