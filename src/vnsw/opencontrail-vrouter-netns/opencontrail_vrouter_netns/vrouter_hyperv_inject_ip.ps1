param (
    [parameter(Mandatory = $true)][string]$Name,
    [parameter(Mandatory = $true)][string]$SwitchName,
    [parameter(Mandatory = $true)][string]$IPAddress,
    [parameter(Mandatory = $true)][string]$Subnet
)

$VmMgmtService = Get-WmiObject -Namespace 'root\virtualization\v2' `
    -Class Msvm_VirtualSystemManagementService

$vmNetworkAdapter = Get-VM -Name $Name | Get-VMNetworkAdapter | Where-Object SwitchName `
    -eq $SwitchName

$Vm = Get-WmiObject -Namespace 'root\virtualization\v2' -Class Msvm_ComputerSystem | `
    Where-Object { $_.ElementName -eq $Name }

$VmSettingData = ($Vm.GetRelated( `
    "Msvm_VirtualSystemSettingData", "Msvm_SettingsDefineState", $null, $null, `
    "SettingData", "ManagedElement", $false, $null) | % {$_})

$VmEthernetPortSettingData = $VmSettingData.GetRelated('Msvm_SyntheticEthernetPortSettingData') `
    | Where-Object { $_.ElementName -eq $vmNetworkAdapter.Name }
$GuestNetworkAdapterConfiguration = ($VmEthernetPortSettingData.GetRelated( `
    "Msvm_GuestNetworkAdapterConfiguration", "Msvm_SettingDataComponent", $null, $null, `
    "PartComponent", "GroupComponent", $false, $null) | % {$_})

$GuestNetworkAdapterConfiguration.IPAddresses = @($IPAddress)
$GuestNetworkAdapterConfiguration.DHCPEnabled = $false
$GuestNetworkAdapterConfiguration.Subnets = @($Subnet)

$SetIpJob = $VmMgmtService.SetGuestNetworkAdapterConfiguration($Vm.Path, `
        $GuestNetworkAdapterConfiguration.GetText(1))

if ($SetIpJob.ReturnValue -eq 4096) {
    $Job = $SetIpJob.job
    while ($Job.JobState -eq 3 -or $Job.JobState -eq 4) {
        Start-Sleep 1
        $Job = [WMI]$SetIpJob.job
    }

    if ($Job.JobState -eq 7) {
        Write-Host "Success"
        exit 0
    } else {
        Write-Host "Failure"
        exit 1
    }
}
