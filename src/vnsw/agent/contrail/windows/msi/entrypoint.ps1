#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

$ErrorActionPreference = "Stop"

$ConfPath = 'C:\ProgramData\Contrail\etc\contrail\contrail-vrouter-agent.conf'
$VhostName = 'vEthernet (HNSTransparent)'

$Conf = Get-Content $ConfPath
$VhostIfname = (Get-NetAdapter -Name $VhostName).ifName

for ($i = 0; $i -lt $Conf.Count; $i++) {
    if ($Conf[$i] -match '^name=') {
        break
    }
}

if ($i -eq $Conf.Count) {
    Write-Output 'Failed to update config'
    Exit 1
}

$Conf[$i] = "name=$VhostIfname"
Set-Content -Path $ConfPath -Value $Conf

.\contrail-vrouter-agent.exe
