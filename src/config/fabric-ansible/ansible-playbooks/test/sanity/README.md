## Procedure to run the container cleanup sanity test.
1. SSH into the contrail-controller node.
2. Get inside the config_devicemgr_1 docker by executing the command below.
    # docker exec -it config_devicemgr_1 bash
3. Navigate to the sanity test folder
    # cd /opt/contrail/fabric_ansible_playbooks/test/sanity
4. From here, get inside the config directory and enter the appropriate host IP for API server and analytics in the test_config.yml file.

   Here is a sample file: opt/contrail/fabric_ansible_playbooks/test/sanity/config/test_config.yml
                    log:
                    level: DEBUG
                    file:
                        level: DEBUG
                        dir: /var/log
                    console: INFO
                    wait_for_job:
                    timeout: 3
                    max_retries: 75
                    api_server:
                    host: {{api_server_hostname}}
                    port: 8082
                    username: admin
                    password: sample123
                    tenant: admin
                    analytics:
                    host: {{analytics_server_hostname}}
                    port: 8081
                    prouter:
                    passwords:
                        - sample123
                    ips:
                        - 10.155.67.5
                    namespaces:
                    asn: 64512
                    management:
                        name: vmx
                        cidrs:
                        - 10.155.67.5/32
                    rma:
                    fabric: fab01
                    device_list:
                        - name: Leaf1
                        serial_number: WS3718270049

5. Go back to the sanity directory and execute the sanity test file -/opt/contrail/fabric_ansible_playbooks/test/sanity/sanity_test_container_cleanup.py
    # python sanity_test_container_cleanup.py

This file will clean up all the unwanted chunks in the container and free up space for fresh upload.
