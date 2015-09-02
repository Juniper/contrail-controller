opencontrail-netns
==================

OpenContrail Linux network namespace provisioning

This package contains two scripts (netns-daemon-start, netns-daemon-stop) which are intended to be used as pre-start and post-stop scripts for applications started out of init.d.

The application itself should then be executed as:
```
ip netns exec ns-<daemon> /usr/bin/<daemon>
```

Example:
```
# daemon upstart configuration file

env PIDFILE=/var/run/nents-<daemon_name>.pid

pre-start script
  start-stop-daemon --start --background --pidfile $PIDFILE --make-pidfile \
      --exec /usr/local/bin/netns-daemon-start -- -U <username> -P <password> \
                 -s <api_server_ip> --network <network_name> <daemon_name> --monitor
  # Wait for the netns to get created.
  sleep 5
end script

post-stop script
  start-stop-daemon --stop --pidfile $PIDFILE
  netns-daemon-stop -U <username> -P <password> -s <api_server_ip> --project <project_name> <daemon_name>
end script

script
  ip netns exec ns-daemon /usr/bin/<daemon_name>
end script
```
