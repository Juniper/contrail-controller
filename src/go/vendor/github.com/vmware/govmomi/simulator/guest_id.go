/*
Copyright (c) 2017 VMware, Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

package simulator

import "github.com/vmware/govmomi/vim25/types"

// GuestID is the list of valid types.VirtualMachineGuestOsIdentifier
var GuestID = []types.VirtualMachineGuestOsIdentifier{
	types.VirtualMachineGuestOsIdentifierDosGuest,
	types.VirtualMachineGuestOsIdentifierWin31Guest,
	types.VirtualMachineGuestOsIdentifierWin95Guest,
	types.VirtualMachineGuestOsIdentifierWin98Guest,
	types.VirtualMachineGuestOsIdentifierWinMeGuest,
	types.VirtualMachineGuestOsIdentifierWinNTGuest,
	types.VirtualMachineGuestOsIdentifierWin2000ProGuest,
	types.VirtualMachineGuestOsIdentifierWin2000ServGuest,
	types.VirtualMachineGuestOsIdentifierWin2000AdvServGuest,
	types.VirtualMachineGuestOsIdentifierWinXPHomeGuest,
	types.VirtualMachineGuestOsIdentifierWinXPProGuest,
	types.VirtualMachineGuestOsIdentifierWinXPPro64Guest,
	types.VirtualMachineGuestOsIdentifierWinNetWebGuest,
	types.VirtualMachineGuestOsIdentifierWinNetStandardGuest,
	types.VirtualMachineGuestOsIdentifierWinNetEnterpriseGuest,
	types.VirtualMachineGuestOsIdentifierWinNetDatacenterGuest,
	types.VirtualMachineGuestOsIdentifierWinNetBusinessGuest,
	types.VirtualMachineGuestOsIdentifierWinNetStandard64Guest,
	types.VirtualMachineGuestOsIdentifierWinNetEnterprise64Guest,
	types.VirtualMachineGuestOsIdentifierWinLonghornGuest,
	types.VirtualMachineGuestOsIdentifierWinLonghorn64Guest,
	types.VirtualMachineGuestOsIdentifierWinNetDatacenter64Guest,
	types.VirtualMachineGuestOsIdentifierWinVistaGuest,
	types.VirtualMachineGuestOsIdentifierWinVista64Guest,
	types.VirtualMachineGuestOsIdentifierWindows7Guest,
	types.VirtualMachineGuestOsIdentifierWindows7_64Guest,
	types.VirtualMachineGuestOsIdentifierWindows7Server64Guest,
	types.VirtualMachineGuestOsIdentifierWindows8Guest,
	types.VirtualMachineGuestOsIdentifierWindows8_64Guest,
	types.VirtualMachineGuestOsIdentifierWindows8Server64Guest,
	types.VirtualMachineGuestOsIdentifierWindows9Guest,
	types.VirtualMachineGuestOsIdentifierWindows9_64Guest,
	types.VirtualMachineGuestOsIdentifierWindows9Server64Guest,
	types.VirtualMachineGuestOsIdentifierWindowsHyperVGuest,
	types.VirtualMachineGuestOsIdentifierFreebsdGuest,
	types.VirtualMachineGuestOsIdentifierFreebsd64Guest,
	types.VirtualMachineGuestOsIdentifierRedhatGuest,
	types.VirtualMachineGuestOsIdentifierRhel2Guest,
	types.VirtualMachineGuestOsIdentifierRhel3Guest,
	types.VirtualMachineGuestOsIdentifierRhel3_64Guest,
	types.VirtualMachineGuestOsIdentifierRhel4Guest,
	types.VirtualMachineGuestOsIdentifierRhel4_64Guest,
	types.VirtualMachineGuestOsIdentifierRhel5Guest,
	types.VirtualMachineGuestOsIdentifierRhel5_64Guest,
	types.VirtualMachineGuestOsIdentifierRhel6Guest,
	types.VirtualMachineGuestOsIdentifierRhel6_64Guest,
	types.VirtualMachineGuestOsIdentifierRhel7Guest,
	types.VirtualMachineGuestOsIdentifierRhel7_64Guest,
	types.VirtualMachineGuestOsIdentifierCentosGuest,
	types.VirtualMachineGuestOsIdentifierCentos64Guest,
	types.VirtualMachineGuestOsIdentifierCentos6Guest,
	types.VirtualMachineGuestOsIdentifierCentos6_64Guest,
	types.VirtualMachineGuestOsIdentifierCentos7Guest,
	types.VirtualMachineGuestOsIdentifierCentos7_64Guest,
	types.VirtualMachineGuestOsIdentifierOracleLinuxGuest,
	types.VirtualMachineGuestOsIdentifierOracleLinux64Guest,
	types.VirtualMachineGuestOsIdentifierOracleLinux6Guest,
	types.VirtualMachineGuestOsIdentifierOracleLinux6_64Guest,
	types.VirtualMachineGuestOsIdentifierOracleLinux7Guest,
	types.VirtualMachineGuestOsIdentifierOracleLinux7_64Guest,
	types.VirtualMachineGuestOsIdentifierSuseGuest,
	types.VirtualMachineGuestOsIdentifierSuse64Guest,
	types.VirtualMachineGuestOsIdentifierSlesGuest,
	types.VirtualMachineGuestOsIdentifierSles64Guest,
	types.VirtualMachineGuestOsIdentifierSles10Guest,
	types.VirtualMachineGuestOsIdentifierSles10_64Guest,
	types.VirtualMachineGuestOsIdentifierSles11Guest,
	types.VirtualMachineGuestOsIdentifierSles11_64Guest,
	types.VirtualMachineGuestOsIdentifierSles12Guest,
	types.VirtualMachineGuestOsIdentifierSles12_64Guest,
	types.VirtualMachineGuestOsIdentifierNld9Guest,
	types.VirtualMachineGuestOsIdentifierOesGuest,
	types.VirtualMachineGuestOsIdentifierSjdsGuest,
	types.VirtualMachineGuestOsIdentifierMandrakeGuest,
	types.VirtualMachineGuestOsIdentifierMandrivaGuest,
	types.VirtualMachineGuestOsIdentifierMandriva64Guest,
	types.VirtualMachineGuestOsIdentifierTurboLinuxGuest,
	types.VirtualMachineGuestOsIdentifierTurboLinux64Guest,
	types.VirtualMachineGuestOsIdentifierUbuntuGuest,
	types.VirtualMachineGuestOsIdentifierUbuntu64Guest,
	types.VirtualMachineGuestOsIdentifierDebian4Guest,
	types.VirtualMachineGuestOsIdentifierDebian4_64Guest,
	types.VirtualMachineGuestOsIdentifierDebian5Guest,
	types.VirtualMachineGuestOsIdentifierDebian5_64Guest,
	types.VirtualMachineGuestOsIdentifierDebian6Guest,
	types.VirtualMachineGuestOsIdentifierDebian6_64Guest,
	types.VirtualMachineGuestOsIdentifierDebian7Guest,
	types.VirtualMachineGuestOsIdentifierDebian7_64Guest,
	types.VirtualMachineGuestOsIdentifierDebian8Guest,
	types.VirtualMachineGuestOsIdentifierDebian8_64Guest,
	types.VirtualMachineGuestOsIdentifierDebian9Guest,
	types.VirtualMachineGuestOsIdentifierDebian9_64Guest,
	types.VirtualMachineGuestOsIdentifierDebian10Guest,
	types.VirtualMachineGuestOsIdentifierDebian10_64Guest,
	types.VirtualMachineGuestOsIdentifierAsianux3Guest,
	types.VirtualMachineGuestOsIdentifierAsianux3_64Guest,
	types.VirtualMachineGuestOsIdentifierAsianux4Guest,
	types.VirtualMachineGuestOsIdentifierAsianux4_64Guest,
	types.VirtualMachineGuestOsIdentifierAsianux5_64Guest,
	types.VirtualMachineGuestOsIdentifierAsianux7_64Guest,
	types.VirtualMachineGuestOsIdentifierOpensuseGuest,
	types.VirtualMachineGuestOsIdentifierOpensuse64Guest,
	types.VirtualMachineGuestOsIdentifierFedoraGuest,
	types.VirtualMachineGuestOsIdentifierFedora64Guest,
	types.VirtualMachineGuestOsIdentifierCoreos64Guest,
	types.VirtualMachineGuestOsIdentifierVmwarePhoton64Guest,
	types.VirtualMachineGuestOsIdentifierOther24xLinuxGuest,
	types.VirtualMachineGuestOsIdentifierOther26xLinuxGuest,
	types.VirtualMachineGuestOsIdentifierOtherLinuxGuest,
	types.VirtualMachineGuestOsIdentifierOther3xLinuxGuest,
	types.VirtualMachineGuestOsIdentifierGenericLinuxGuest,
	types.VirtualMachineGuestOsIdentifierOther24xLinux64Guest,
	types.VirtualMachineGuestOsIdentifierOther26xLinux64Guest,
	types.VirtualMachineGuestOsIdentifierOther3xLinux64Guest,
	types.VirtualMachineGuestOsIdentifierOtherLinux64Guest,
	types.VirtualMachineGuestOsIdentifierSolaris6Guest,
	types.VirtualMachineGuestOsIdentifierSolaris7Guest,
	types.VirtualMachineGuestOsIdentifierSolaris8Guest,
	types.VirtualMachineGuestOsIdentifierSolaris9Guest,
	types.VirtualMachineGuestOsIdentifierSolaris10Guest,
	types.VirtualMachineGuestOsIdentifierSolaris10_64Guest,
	types.VirtualMachineGuestOsIdentifierSolaris11_64Guest,
	types.VirtualMachineGuestOsIdentifierOs2Guest,
	types.VirtualMachineGuestOsIdentifierEComStationGuest,
	types.VirtualMachineGuestOsIdentifierEComStation2Guest,
	types.VirtualMachineGuestOsIdentifierNetware4Guest,
	types.VirtualMachineGuestOsIdentifierNetware5Guest,
	types.VirtualMachineGuestOsIdentifierNetware6Guest,
	types.VirtualMachineGuestOsIdentifierOpenServer5Guest,
	types.VirtualMachineGuestOsIdentifierOpenServer6Guest,
	types.VirtualMachineGuestOsIdentifierUnixWare7Guest,
	types.VirtualMachineGuestOsIdentifierDarwinGuest,
	types.VirtualMachineGuestOsIdentifierDarwin64Guest,
	types.VirtualMachineGuestOsIdentifierDarwin10Guest,
	types.VirtualMachineGuestOsIdentifierDarwin10_64Guest,
	types.VirtualMachineGuestOsIdentifierDarwin11Guest,
	types.VirtualMachineGuestOsIdentifierDarwin11_64Guest,
	types.VirtualMachineGuestOsIdentifierDarwin12_64Guest,
	types.VirtualMachineGuestOsIdentifierDarwin13_64Guest,
	types.VirtualMachineGuestOsIdentifierDarwin14_64Guest,
	types.VirtualMachineGuestOsIdentifierDarwin15_64Guest,
	types.VirtualMachineGuestOsIdentifierDarwin16_64Guest,
	types.VirtualMachineGuestOsIdentifierVmkernelGuest,
	types.VirtualMachineGuestOsIdentifierVmkernel5Guest,
	types.VirtualMachineGuestOsIdentifierVmkernel6Guest,
	types.VirtualMachineGuestOsIdentifierVmkernel65Guest,
	types.VirtualMachineGuestOsIdentifierOtherGuest,
	types.VirtualMachineGuestOsIdentifierOtherGuest64,
}
