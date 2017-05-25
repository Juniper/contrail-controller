import string

template = string.Template("""
[Unit]
Description="Contrail Tor Agent service"
After=network.target

[Service]
Type=simple
ExecStart=/usr/bin/contrail-tor-agent --config_file /etc/contrail/$__contrail_tor_agent_conf_file__
PIDFile=/var/run/contrail/contrail-tor-agent.pid
TimeoutStopSec=0
Restart=always
ExecStop=/bin/kill -s KILL $MAINPID
PrivateTmp=yes
ProtectHome=yes
ReadOnlyDirectories=/
ReadWriteDirectories=-/var/crashes
ReadWriteDirectories=-/var/log/contrail
ReadWriteDirectories=-/var/lib/contrail
ReadWriteDirectories=-/dev

[Install]
WantedBy=multi-user.target
""")
