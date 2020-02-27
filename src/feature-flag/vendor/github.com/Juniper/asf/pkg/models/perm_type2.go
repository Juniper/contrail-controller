package models

// EnableDomainSharing enables domain sharing for resource.
func (p *PermType2) EnableDomainSharing(domainUUID string, accessLevel int64) error {
	p.Share = append(p.Share, &ShareType{
		Tenant:       "domain:" + domainUUID,
		TenantAccess: accessLevel,
	})
	return nil
}
