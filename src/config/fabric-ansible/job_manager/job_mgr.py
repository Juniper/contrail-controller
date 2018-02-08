from gevent import monkey; monkey.patch_socket()
from gevent.pool import Pool
from time import gmtime, strftime
from gevent.greenlet import Greenlet
import json
import argparse
import traceback

from vnc_api.vnc_api import *
from pysandesh.sandesh_base import *
from pysandesh.sandesh_logger import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

from job_handler import JobHandler
from job_exception import JobException
from job_result_handler import JobResultHandler
from job_utils import JobStatus, send_job_execution_uve, send_job_log, read_job_template
from logger import JobLogger

POOL_SIZE = 20

# sandesh instance is maintained as global variable for access from within ansible callbacks
sandesh = None

# TODO add api for use by the module callbacks


class JobManager(object):
    def __init__(self, logger, vnc_api, job_input):
        global sandesh
        sandesh = logger._sandesh
        self._logger = logger
        self._vnc_api = vnc_api
        self.job_template_id = None
        self.job_execution_id = None
        self.job_data = None
        self.job_params = dict()
        self.parse_job_input(job_input)
        logger.debug("Job manager initialized")

    def parse_job_input(self, job_input_json):

        # job input should have job_template_id and execution_id field
        if job_input_json['job_template_id']:
            self.job_template_id = job_input_json['job_template_id']

        self.job_execution_id = job_input_json['execution_id']

        try:
            if job_input_json['input']:
                self.job_data = job_input_json['input']
        except KeyError:
            self._logger.debug("Job input data is not provided.")
        try:
            if job_input_json['params']:
                self.job_params = job_input_json['params']
        except KeyError:
            self._logger.debug("Job extra params is not present.")

        return

    def start_job(self):
        try:
            # create job UVE and log
            msg = "Starting execution for job with template id %s and execution id %s"
            self._logger.debug(msg)
            timestamp = strftime("%Y-%m-%d %H:%M:%S", gmtime())
            send_job_execution_uve(self.job_template_id, self.job_execution_id, self._logger, timestamp, 0)
            send_job_log(self.job_template_id, self.job_execution_id, self._logger, msg, JobStatus.STARTING,
                         timestamp=timestamp)
            # read the job template object
            job_template = read_job_template(self._vnc_api, self.job_template_id, self._logger)

            # spawn job greenlets
            job_handler = JobHandler(self._logger, self._vnc_api, job_template, self.job_execution_id,
                                     self.job_data, self.job_params)
            result_handler = JobResultHandler(job_template, self.job_execution_id, self._logger)
            if job_template.get_job_template_multi_device_job():
                self.handle_multi_device_job(job_handler, result_handler)
            else:
                self.handle_single_job(job_handler, result_handler)

            # update job uve
            timestamp = strftime("%Y-%m-%d %H:%M:%S", gmtime())
            send_job_execution_uve(self.job_template_id, self.job_execution_id, self._logger, timestamp, 100)
            result_handler.create_job_summary_log(timestamp)
        except JobException as e:
            self.mark_job_failure(e.message)
        except Exception as e:
            self._logger.error("Error while executing job %s " % repr(e))
            self._logger.error(traceback.print_stack())
            self.mark_job_failure(e.message)

    def mark_job_failure(self, msg):
        try:
            timestamp = strftime("%Y-%m-%d %H:%M:%S", gmtime())
            send_job_execution_uve(self.job_template_id, self.job_execution_id, self._logger, timestamp, 100)
            send_job_log(self.job_template_id, self.job_execution_id, self._logger, msg, JobStatus.FAILURE,
                         timestamp=timestamp)
        except Exception as e:
            self._logger.error("Exception while marking the job as failed.")
            self._logger.error("%s" % traceback.print_stack())

    def handle_multi_device_job(self, job_handler, result_handler):
        job_worker_pool = Pool(POOL_SIZE)
        for device_id in self.job_params['device_list']:
            job_worker_pool.start(Greenlet(job_handler.handle_device_job, device_id, result_handler))
        job_worker_pool.join()
        pass

    def handle_single_job(self, job_handler, result_handler):
        g = Greenlet.spawn(job_handler.handle_job, result_handler)
        g.join()
        pass


def parse_args():
    parser = argparse.ArgumentParser(description='Job manager parameters')
    parser.add_argument('-i', '--job_input', nargs=1, help='Job manager input json')
    return parser.parse_args()


def parse_logger_args(args_str=None):
    parser = argparse.ArgumentParser(description='Job manager parameters')
    parser.add_argument('-i', '--job_input', nargs=1, help='Job manager input json')

    args_str = ''
    conf_parser = argparse.ArgumentParser(add_help=False)

    conf_parser.add_argument("-c", "--conf_file", action='append',
                             help="Specify config file", metavar="FILE")
    parser = argparse.ArgumentParser()
    args, remaining_argv = conf_parser.parse_known_args(args_str.split())

    defaults = {
        'rabbit_server': 'localhost',
        'rabbit_port': '5672',
        'rabbit_user': 'guest',
        'rabbit_password': 'guest',
        'rabbit_vhost': None,
        'rabbit_ha_mode': False,
        'cassandra_server_list': '127.0.0.1:9160',
        'api_server_ip': '127.0.0.1',
        'api_server_port': '8082',
        'api_server_use_ssl': False,
        'zk_server_ip': '127.0.0.1',
        'zk_server_port': '2181',
        'collectors': None,
        'http_server_port': '8111',
        'log_local': False,
        'log_level': SandeshLevel.SYS_DEBUG,
        'log_category': '',
        'log_file': Sandesh._DEFAULT_LOG_FILE,
        'use_syslog': False,
        'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
        'cluster_id': '',
        'logging_conf': '',
        'logger_class': None,
        'repush_interval': '15',
        'repush_max_interval': '600',
        'push_delay_per_kb': '0.01',
        'push_delay_max': '100',
        'push_delay_enable': True,
        'rabbit_use_ssl': False,
        'kombu_ssl_version': '',
        'kombu_ssl_keyfile': '',
        'kombu_ssl_certfile': '',
        'kombu_ssl_ca_certs': '',
    }
    defaults.update(SandeshConfig.get_default_options(['DEFAULTS']))
    secopts = {
        'use_certs': False,
        'keyfile': '',
        'certfile': '',
        'ca_certs': '',
    }
    ksopts = {
        'admin_user': 'user1',
        'admin_password': 'password1',
        'admin_tenant_name': 'default-domain',
    }
    cassandraopts = {
        'cassandra_user': None,
        'cassandra_password': None
    }
    sandeshopts = SandeshConfig.get_default_options()

    saved_conf_file = args.conf_file
    if args.conf_file:
        config = ConfigParser.SafeConfigParser()
        config.read(args.conf_file)
        defaults.update(dict(config.items("DEFAULTS")))
        if ('SECURITY' in config.sections() and
                'use_certs' in config.options('SECURITY')):
            if config.getboolean('SECURITY', 'use_certs'):
                secopts.update(dict(config.items("SECURITY")))
        if 'KEYSTONE' in config.sections():
            ksopts.update(dict(config.items("KEYSTONE")))
        if 'CASSANDRA' in config.sections():
            cassandraopts.update(dict(config.items('CASSANDRA')))
        SandeshConfig.update_options(sandeshopts, config)

    # Override with CLI options
    # Don't surpress add_help here so it will handle -h
    #parser = argparse.ArgumentParser(
        # Inherit options from config_parser
    #    parents=[conf_parser],
        # print script description with -h/--help
    #    description=__doc__,
        # Don't mess with format of description
    #    formatter_class=argparse.RawDescriptionHelpFormatter,
    #)
    defaults.update(secopts)
    defaults.update(ksopts)
    defaults.update(cassandraopts)
    defaults.update(sandeshopts)
    parser.set_defaults(**defaults)

    parser.add_argument(
        "--cassandra_server_list",
        help="List of cassandra servers in IP Address:Port format",
        nargs='+')
    parser.add_argument(
        "--reset_config", action="store_true",
        help="Warning! Destroy previous configuration and start clean")
    parser.add_argument("--api_server_ip",
                        help="IP address of API server")
    parser.add_argument("--api_server_port",
                        help="Port of API server")
    parser.add_argument("--api_server_use_ssl",
                        help="Use SSL to connect with API server")
    parser.add_argument("--zk_server_ip",
                        help="IP address:port of zookeeper server")
    parser.add_argument("--collectors",
                        help="List of VNC collectors in ip:port format",
                        nargs="+")
    parser.add_argument("--http_server_port",
                        help="Port of local HTTP server")
    parser.add_argument("--log_local", action="store_true",
                        help="Enable local logging of sandesh messages")
    parser.add_argument(
        "--log_level",
        help="Severity level for local logging of sandesh messages")
    parser.add_argument(
        "--log_category",
        help="Category filter for local logging of sandesh messages")
    parser.add_argument("--log_file",
                        help="Filename for the logs to be written to")
    parser.add_argument("--use_syslog", action="store_true",
                        help="Use syslog for logging")
    parser.add_argument("--syslog_facility",
                        help="Syslog facility to receive log lines")
    parser.add_argument("--admin_user",
                        help="Name of keystone admin user")
    parser.add_argument("--admin_password",
                        help="Password of keystone admin user")
    parser.add_argument("--admin_tenant_name",
                        help="Tenant name for keystone admin user")
    parser.add_argument("--cluster_id",
                        help="Used for database keyspace separation")
    parser.add_argument(
        "--logging_conf",
        help=("Optional logging configuration file, default: None"))
    parser.add_argument(
        "--logger_class",
        help=("Optional external logger class, default: None"))
    parser.add_argument("--repush_interval",
                        help="time interval for config re push")
    parser.add_argument("--repush_max_interval",
                         help="max time interval for config re push")
    parser.add_argument("--push_delay_per_kb",
                        help="time delay between two successful commits per kb config size")
    parser.add_argument("--push_delay_max",
                        help="max time delay between two successful commits")
    parser.add_argument("--push_delay_enable",
                        help="enable delay between two successful commits")
    parser.add_argument("--cassandra_user",
                        help="Cassandra user name")
    parser.add_argument("--cassandra_password",
                        help="Cassandra password")
    SandeshConfig.add_parser_arguments(parser)
    args = parser.parse_args(remaining_argv)
    if type(args.cassandra_server_list) is str:
        args.cassandra_server_list = args.cassandra_server_list.split()
    if type(args.collectors) is str:
        args.collectors = args.collectors.split()
    args.sandesh_config = SandeshConfig.from_parser_arguments(args)

    args.conf_file = saved_conf_file
    return args


def initialize_sandesh_logger():
    # parse the logger args
    args = None
    try:
        args = parse_logger_args()
        args.random_collectors = args.collectors
        if args.collectors:
                args.random_collectors = random.sample(args.collectors, len(args.collectors))
    except Exception as e:
        print "Error in parsing the logger args %s" % repr(e)
        print "%s" % traceback.print_stack()

    # initialize logger
    logger = None
    try:
        if args is not None:
            logger = JobLogger(args)
            logger.info("Job Manager process is starting. Sandesh is initialized.")
    except Exception as e:
        print "Error while initializing the SANDESH instance %s" % repr(e)
        print "%s" % traceback.print_stack()

    return logger

if __name__ == "__main__":

    # TODO the prints should go to some log file
    # parse the params passed to the job manager process
    job_input_json = None
    try:
        job_params = parse_args()
        job_input_json = json.loads(job_params.job_input[0])
        if job_input_json is None:
            print "Job input data is not passed to job manager. Aborting job."
    except Exception as e:
        print "Job input data is not passed to job manager. Aborting job. %s" % repr(e)
        print "%s" % traceback.print_stack()

    # TODO need to get the config from api-server
    logger = initialize_sandesh_logger()

    # initialize _vnc_api instance
    vnc_api = None
    try:
        auth_token = job_input_json['params']['auth_token']
        vnc_api = VncApi(auth_token=auth_token)
        logger.info("VNC api is initialized using the auth token passed.")
    except Exception as e:
        logger.error("Error in vnc api initialization. %s " % repr(e))
        logger.error("%s" % traceback.print_stack())

    # invoke job manager
    try:
        if logger is not None and vnc_api is not None:
            job_manager = JobManager(logger, vnc_api, job_input_json)
            job_manager.start_job()
        else:
            logger.error("Failed to start job due to issue in logger/api intialization")
    except Exception as e:
        logger.error("Error starting job from job manager %s " % repr(e))
        logger.error("%s" % traceback.print_stack())

    # TODO disconnect from sandesh

