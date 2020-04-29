#!/bin/bash
FILE=$1
RELEASE=$2

export FILE
export RELEASE
input=$FILE
if [ -z "${RELEASE}"] 
then
    while IFS= read -r line
    do
        echo "http://svl-artifactory.juniper.net/artifactory/contrail-static-prod/"$line"/contrail-vrouter-agent.tgz"
        echo "http://svl-artifactory.juniper.net/artifactory/contrail-static-prod/"$line"/control-node.tgz"
        
    done < "$input"
else
    echo "http://svl-artifactory.juniper.net/artifactory/contrail-static-prod/"$RELEASE"/contrail-vrouter-agent.tgz"
    echo "http://svl-artifactory.juniper.net/artifactory/contrail-static-prod/"$RELEASE"/control-node.tgz"
fi