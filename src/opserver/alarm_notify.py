#!/usr/bin/python

#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

#
# alarm_notify
#
# Send Email notification for alarms based on /analytics/alarm-stream REST API
#

from gevent import monkey
monkey.patch_all()
import time
import json
import argparse
import requests
import datetime
import smtplib
import getpass
from sseclient import SSEClient
from email.MIMEMultipart import MIMEMultipart
from email.MIMEText import MIMEText

from sandesh.viz.constants import UVE_MAP


class ContrailAlarmInfo(object):

    def __init__(self):
        self.type = None
        self.severity = None
        self.timestamp = None
        self.ack = None
        self.summary = None
        self.description = None
        self.details = None
    # end __init__


# end class ContrailAlarmInfo


class ContrailAlarm(object):

    def __init__(self):
        self.table = None
        self.key = None
        self.cleared = None
        self.alarms = None  # List of ContrailAlarmInfo objects
    # end __init__


# end class ContrailAlarm


class ContrailAlarmNotifier(object):

    def __init__(self):
        self._args = None
        self._email_server = None
        self._sender_email_pwd = None
        self._alarm_types = None
        self._analytics_api_name_to_table_name = UVE_MAP
        self._table_name_to_analytics_api_name = \
            {v: k for k, v in UVE_MAP.iteritems()}
    # end __init__

    def run(self):
        try:
            if self._parse_args() != 0:
                return
            self._alarm_types = self._get_alarm_types()
            if not self._alarm_types:
                return
            if not self._connect_to_smtp_server():
                return
            init_alarm_sub = 'Contrail Alarms Notification!'
            init_alarm_msg = 'Setting up Contrail Alarm Notification...' + \
                '\n\n' + 'Your email has been added to the Contrail Alarm ' + \
                'Notification List'
            # Can we send mail without authentication?
            if not self._send_email(init_alarm_sub, init_alarm_msg):
                # The SMTP server requires authentication before
                # sending mail
                self._sender_email_pwd = self._get_sender_password()
                if not self._login_to_smtp_server():
                    return
                if not self._send_email(init_alarm_sub, init_alarm_msg):
                    return
            self._listen_and_notify_alarms()
        except KeyboardInterrupt:
            return
    # end run

    def _parse_args(self):
        defaults = {
            'analytics_api_server': '127.0.0.1',
            'analytics_api_server_port': '8081',
            'smtp_server': None,
            'smtp_server_port': None,
            'sender_email': None,
            'receiver_email_list': None
        }

        parser = argparse.ArgumentParser(
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.set_defaults(**defaults)
        parser.add_argument('--analytics-api-server',
                            help='Name (or) IP Address of ' +
                                 'Analytics API Server')
        parser.add_argument('--analytics-api-server-port',
                            type=int,
                            help='Port of Analytics API Server')
        parser.add_argument('--smtp-server',
                            required=True,
                            help='Name (or) IP Address of SMTP Server')
        parser.add_argument('--smtp-server-port',
                            type=int,
                            required=True,
                            help='Port of SMTP Server')
        parser.add_argument('--sender-email',
                            required=True,
                            help='Email address of the Sender')
        parser.add_argument('--receiver-email-list',
                            required=True,
                            help='List of Email addresses to send ' +
                                 'notification',
                            nargs='+')
        self._args = parser.parse_args()
        return 0
    # end _parse_args

    def _get_sender_password(self):
        print 'Please enter password for {0}'.format(self._args.sender_email)
        return getpass.getpass()
    # end _get_sender_password

    def _connect_to_smtp_server(self):
        try:
            self._email_server = smtplib.SMTP(self._args.smtp_server,
                                              self._args.smtp_server_port,
                                              timeout=10)
            self._email_server.starttls()
        except Exception as e:
            print 'Failed to connect to SMTP Server: {0}'.format(e)
        else:
            if self._sender_email_pwd is not None:
                return self._login_to_smtp_server()
            else:
                return True
        return False
    # end _connect_to_smtp_server

    def _login_to_smtp_server():
        try:
            self._email_server.login(self._sender_email,
                                     self._sender_email_pwd)
        except smtplib.SMTPAuthenticationError:
            print 'Invalid email id and/or password'
        except Exception as e:
            print 'Could not login to the SMTP server: {0}'.format(e)
        else:
            return True
        return False
    # end _login_to_smtp_server

    def _get_alarm_types(self):
        alarm_types_url = 'http://{0}:{1}/analytics/alarm-types'.format(
            self._args.analytics_api_server,
            self._args.analytics_api_server_port)
        alarm_types = None
        try:
            resp = requests.get(alarm_types_url)
        except requests.ConnectionError:
            print 'Could not connect to analytics-api {0}:{1}'.format(
                self._args.analytics_api_server,
                self._args.analytics_api_server_port)
        else:
            if resp.status_code == 200:
                try:
                    alarm_types = json.loads(resp.text)
                except ValueError:
                    pass
        return alarm_types
    # end _get_alarm_types

    def _listen_and_notify_alarms(self):
        while True:
            try:
                alarm_stream_url = \
                    'http://{0}:{1}/analytics/alarm-stream'.format(
                        self._args.analytics_api_server,
                        self._args.analytics_api_server_port)
                alarm_stream = SSEClient(alarm_stream_url)
                for alarm in alarm_stream:
                    if alarm.event != 'update':
                        continue
                    alarm_data = self._parse_alarm(alarm)
                    if alarm_data:
                        if alarm_data.cleared:
                            subject = \
                                '[Contrail Alarm] {0}:{1} - Cleared'.format(
                                    alarm_data.table, alarm_data.key)
                            if not self._try_sending_email(subject, ''):
                                self._log_alarm({'subject': subject,
                                                 'msg': ''})
                        else:
                            for alarm_elt in alarm_data.alarms:
                                subject = \
                                    '[Contrail Alarm] {0} -- {1}:{2}'.format(
                                        alarm_elt.summary, alarm_data.table,
                                        alarm_data.key)
                                body = self._format_email_body(alarm_data,
                                                               alarm_elt)
                                if not self._try_sending_email(subject, body):
                                    self._log_alarm({'subject': subject,
                                                     'msg': body})
            except Exception as e:
                time.sleep(1)
    # end _listen_and_notify_alarms

    def _parse_alarm(self, alarm):
        try:
            data = json.loads(alarm.data)
            table, key = tuple(data['key'].split(':', 1))
            value = data['value']
            alarm_elts = None
            if value:
                alarm_elts = value['alarms']
        except KeyError:
            print 'Error parsing alarm object'
            self._log_alarm(alarm.data)
        else:
            alarm_data = ContrailAlarm()
            try:
                alarm_data.table = \
                    self._table_name_to_analytics_api_name[table]
            except KeyError:
                alarm_data.table = table
            alarm_data.key = key
            alarm_data.cleared = True if not value else False
            alarm_data.alarms = []
            if not alarm_data.cleared:
                for alarm_elt in alarm_elts:
                    alarm_info = ContrailAlarmInfo()
                    try:
                        alarm_info.type = alarm_elt['type']
                        alarm_info.severity = alarm_elt['severity']
                        alarm_info.timestamp = datetime.datetime.fromtimestamp(
                            alarm_elt['timestamp']/1000000.0).strftime(
                                '%Y-%m-%d %H:%M:%S')
                        alarm_info.ack = 'Acknowledged' if alarm_elt['ack'] \
                            else 'Unacknowledged'
                        alarm_info.details = json.dumps(alarm_elt['any_of'],
                                                        indent=4)
                    except KeyError:
                        print 'Error parsing alarm'
                        self._log_alarm(alarm_elt)
                    else:
                        try:
                            alarm_tbl_info = \
                                self._alarm_types[alarm_data.table]
                            alarm_info.summary, alarm_info.description = tuple(
                                alarm_tbl_info[alarm_info.type].split('.', 1))
                        except KeyError:
                            alarm_info.summary = alarm_type
                            alarm_info.description = ''
                        alarm_info.summary = alarm_info.summary.lstrip()
                        alarm_info.description = \
                            alarm_info.description.lstrip()
                        alarm_data.alarms.append(alarm_info)
            return alarm_data
        return None
    # end _parse_alarm

    def _format_email_body(self, alarm_data, alarm_elt):
        return '{0} : {1}:{2}'.format('Source', alarm_data.table,
                                      alarm_data.key) + \
               '\n{0} : {1}'.format('Type', alarm_elt.type) + \
               '\n{0} : {1}'.format('Severity', alarm_elt.severity) + \
               '\n{0} : {1}'.format('Timestamp', alarm_elt.timestamp) + \
               '\n{0} : {1}'.format('Status', alarm_elt.ack) + \
               '\n{0} : {1}'.format('Description', alarm_elt.description) + \
               '\n{0} : {1}'.format('Details', alarm_elt.details)
    # end _format_email_body

    def _try_sending_email(self, subject, body):
        if not self._send_email(subject, body):
            # SMTP session must have timeout due to long idle time.
            # Retry connecting to the SMTP Server.
            if not self._connect_to_smtp_server():
                return False
            else:
                return self._send_email(subject, body)
        return True
    # end _try_sending_email

    def _send_email(self, subject, body):
        msg = MIMEMultipart()
        msg['From'] = self._args.sender_email
        msg['To'] = ', '.join(self._args.receiver_email_list)
        msg['Subject'] = subject
        msg.attach(MIMEText(body, 'plain'))
        try:
            self._email_server.sendmail(self._args.sender_email,
                                        self._args.receiver_email_list,
                                        msg.as_string())
        except smtplib.SMTPServerDisconnected:
            return False
        return True
    # end _send_email

    def _log_alarm(self, alarm):
        print alarm
    # end _log_alarm


# end class ContrailAlarmNotifier


def main():
    alarm_notifier = ContrailAlarmNotifier()
    alarm_notifier.run()
# end main


if __name__ == '__main__':
    main()
