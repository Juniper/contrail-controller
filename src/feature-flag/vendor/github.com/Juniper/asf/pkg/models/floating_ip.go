package models

// IsParentTypeInstanceIP checks if parent's type is instance ip.
func (fip *FloatingIP) IsParentTypeInstanceIP() bool {
	var m InstanceIP
	return fip.GetParentType() == m.Kind()
}
