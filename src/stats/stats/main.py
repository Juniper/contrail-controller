"""The main module for statistics sending package."""
import logging
from argparse import ArgumentParser
from datetime import datetime, timedelta
from json import dump, dumps, load
from os import getenv
try:
    from urllib.error import HTTPError, URLError
    from urllib.request import Request, urlopen
except ImportError:
    from urllib2 import HTTPError, URLError, Request, urlopen
from time import sleep
from traceback import format_exc

from six.moves.configparser import ConfigParser

from vnc_api.vnc_api import VncApi


def parse_args():
    """Parse command-line arguments to start stats service."""
    parser = ArgumentParser()
    parser.add_argument("--config-file", required=True)
    args = parser.parse_args()
    return args


def parse_config(args):
    """Parse configuration file for stats service."""
    config = ConfigParser()
    config.read(args.config_file)
    log_file = config.get("DEFAULT", "log_file")
    log_lev_map = {"SYS_EMERG": logging.CRITICAL,
                   "SYS_ALERT": logging.CRITICAL,
                   "SYS_CRIT": logging.CRITICAL,
                   "SYS_ERR": logging.ERROR,
                   "SYS_WARN": logging.WARNING,
                   "SYS_NOTICE": logging.INFO,
                   "SYS_INFO": logging.INFO,
                   "SYS_DEBUG": logging.DEBUG
                   }
    log_level = log_lev_map[config.get("DEFAULT", "log_level")]
    stats_server = config.get("DEFAULT", "stats_server")
    state = config.get("DEFAULT", "state")
    return {"log_file": log_file,
            "log_level": log_level,
            "stats_server": stats_server,
            "state": state
            }


def init_logger(log_level, log_file):
    """Initialise logger for stats service."""
    logger = logging.getLogger(name="stats_client")
    logger.setLevel(level=log_level)
    handler = logging.FileHandler(filename=log_file)
    handler.setLevel(level=log_level)
    formatter = logging.Formatter(
        '%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    handler.setFormatter(formatter)
    logger.addHandler(handler)
    return logger


class Stats(object):
    """Contrail Statistics class."""

    def __init__(self, client):
        """Initialise Statistics objest."""
        self.tf_id = client.get_default_project_id()
        self.vmachines = len(client.virtual_machines_list().get(
            'virtual-machines'))
        self.vnetworks = len(client.virtual_networks_list().get(
            'virtual-networks'))
        self.vrouters = len(client.virtual_routers_list().get(
            'virtual-routers'))
        self.vm_interfaces = len(client.virtual_machine_interfaces_list().get(
            'virtual-machine-interfaces'))

    def __str__(self):
        """Represent statistics object as string for logging."""
        return str({"tf_id": self.tf_id,
                    "vr": self.vrouters,
                    "vm": self.vmachines,
                    "vn": self.vnetworks,
                    "vi": self.vm_interfaces})


class Scheduler(object):
    """Schedule job for statistics sending."""

    DEF_SEND_FREQ = None

    def __init__(self, vnc_client, state):
        """Initialise Scheduler instance."""
        self.state = state
        self._vnc_client = vnc_client
        self._state_data = self._get_state_data()
        self._send_freq = self._state_data.get(
            "send_freq",
            self._get_updated_send_freq())
        self._sched_job_ts = self._state_data.get(
            "sched_job_ts",
            self._init_first_job())
        self._save_state_data()

    @property
    def sched_job_ts(self):
        """Get scheduled job timestamp for statistics sending."""
        return self._sched_job_ts

    @sched_job_ts.setter
    def sched_job_ts(self, send_freq):
        """Schedule new job for statistics sending."""
        if (send_freq is not None):
            self._sched_job_ts = datetime.now() + send_freq
        else:
            self._sched_job_ts = None
        self._save_state_data()

    @property
    def send_freq(self):
        """Get sending frequency."""
        return self._send_freq

    @send_freq.setter
    def send_freq(self, updated_send_freq):
        """Set sending frequency."""
        self._send_freq = updated_send_freq
        self._save_state_data()

    def _get_state_data(self):
        try:
            with open(self.state) as json_file:
                state_data = load(json_file)
        except (ValueError, IOError):
            state_data = dict()
        return state_data

    def _init_first_job(self):
        if (self.send_freq is not None):
            sched_job_ts = datetime.now() + self.send_freq
        else:
            sched_job_ts = None
        return sched_job_ts

    def _save_state_data(self):
        state_data = dict()
        state_data["sched_job_ts"] = self.sched_job_ts
        state_data["send_freq"] = self.send_freq
        with open(self.state, 'w') as state_file:
            dump(state_data, state_file)

    def _get_updated_send_freq(self):
        send_freq = Scheduler.DEF_SEND_FREQ
        freq_list = [{"label=stats_monthly": timedelta(days=30)},
                     {"label=stats_weekly": timedelta(days=7)},
                     {"label=stats_daily": timedelta(days=1)},
                     {"label=stats_every_minute": timedelta(minutes=1)}]
        for tag in self._vnc_client.tags_list()["tags"]:
            tag = tag["fq_name"][0]
            for index, freq_item in enumerate(freq_list):
                if tag in freq_item:
                    send_freq = freq_item[tag]
                    del freq_list[index:]
                    if not freq_list:
                        return send_freq
        return send_freq

    def is_job(self):
        """Check if there is scheduled job which must be executed now."""
        # statistics will be sent if:
        # 1. frequency of sending was changed
        # and sending is not switched off.
        # 2. statistics sending was not scheduled yet
        # 3. scheduled stats sending timestamp was already passed

        newest_send_freq = self._get_updated_send_freq()

        if newest_send_freq is None:
            self.sched_job_ts = None
            self.send_freq = None
            return False
        elif self.sched_job_ts is None:
            self.send_freq = newest_send_freq
            self.sched_job_ts = self.send_freq
            return True
        elif self.send_freq != newest_send_freq:
            self.send_freq = newest_send_freq
            self.sched_job_ts = self.send_freq
            return True
        elif datetime.now() > self.sched_job_ts:
            self.sched_job_ts = self.send_freq
            return True
        return False


class Postman(object):
    """Send statistics ang response from statistics server."""

    SLEEP_TIME = 3600

    def __init__(self, stats_server, vnc_client, logger):
        """Initialise Postman instance for statistics sending job."""
        self._vnc_client = vnc_client
        self._stats_server = stats_server
        self.logger = logger

    def send_stats(self):
        """Send statistics to server."""
        self.logger.info("Statistics sending started..")
        RESP = {
            201: {"success": True,
                  "message": ""},
            200: {"success": False,
                  "message": "The server response code is 200. \
                  Successfull stats server response code is 201."},
            404: {"success": False,
                  "message": "The server URI was not found."},
            400: {"success": False,
                  "message": "Malformed or resubmitted data."}
            }

        stats = Stats(client=self._vnc_client)
        try:
            resp_code = urlopen(
                url=Request(
                    url=self._stats_server,
                    data=dumps(stats.__dict__).encode('utf-8'),
                    headers={'Content-Type': 'application/json'})).code
            def_err = {"success": False,
                       "message": "Uknown error. HTTP code: %s." % resp_code}
        except HTTPError as e:
            resp_code = e.code
            def_err = {
                "success": False,
                "message": "Uknown error. HTTP error code: %s." % resp_code}
        except URLError as e:
            resp_code = e.reason[1]
            def_err = {"success": False,
                       "message": "Uknown error. URLError: %s." % resp_code}
        except Exception:
            resp_code = "unknown"
            def_err = {
                "success": False,
                "message": "Unknown error. Traceback: %s" % str(format_exc())}
        finally:
            self.logger.info(str(RESP.get(resp_code, def_err)))
            self.logger.debug("stats: %s" % (str(stats)))


def main():
    """Do the main logic of statistics service."""
    config = parse_config(args=parse_args())
    logger = init_logger(log_level=config["log_level"],
                         log_file=config["log_file"])
    vnc_client = VncApi(username=getenv("KEYSTONE_AUTH_ADMIN_USER"),
                        password=getenv("KEYSTONE_AUTH_ADMIN_PASSWORD"),
                        tenant_name=getenv("KEYSTONE_AUTH_ADMIN_TENANT"))
    scheduler = Scheduler(vnc_client=vnc_client, state=config["state"])
    postman = Postman(stats_server=config["stats_server"],
                      vnc_client=vnc_client,
                      logger=logger)
    while True:
        logger.info("TF usage report client started.")
        if scheduler.is_job():
            postman.send_stats()
        logger.info(
            "Frequency of statistics sending is %s" % str(scheduler.send_freq))
        logger.info(
            "Statistics sending is scheduled at %s" % str(
                scheduler.sched_job_ts))
        sleep(Postman.SLEEP_TIME)
