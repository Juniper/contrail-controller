#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

$ErrorActionPreference = "Stop"

$ConfPath = 'C:\ProgramData\Contrail\etc\contrail\contrail-vrouter-agent.conf'
# This file assumes that there is only one virtual switch created
# and that physical adapter's name to which it is connected starts
# with 'Ethernet'.
$VhostName = 'vEthernet \((HNSTransparent|Ethernet.*?)\)'

$Conf = Get-Content $ConfPath
$VhostIfname = (Get-NetAdapter | Where-Object 'Name' -match $VhostName).ifName

for ($i = 0; $i -lt $Conf.Count; $i++) {
    if ($Conf[$i] -match '^name=') {
        break
    }
}

if ($i -eq $Conf.Count) {
    Throw 'Failed to update config'
}

$Conf[$i] = "name=$VhostIfname"
Set-Content -Path $ConfPath -Value $Conf

.\contrail-vrouter-agent.exe
