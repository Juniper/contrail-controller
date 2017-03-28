import string

template = string.Template("""
[program:$__contrail_tor_agent__]
command=/usr/bin/contrail-tor-agent --config_file /etc/contrail/$__contrail_tor_agent_conf_file__
priority=420
autostart=true
killasgroup=true
stopsignal=KILL
stdout_capture_maxbytes=1MB
redirect_stderr=true
stdout_logfile=/var/log/contrail/$__contrail_tor_agent_log_file__
stderr_logfile=/dev/null
startsecs=5
exitcodes=0                   ; 'expected' exit codes for process (default 0,2)
""")
