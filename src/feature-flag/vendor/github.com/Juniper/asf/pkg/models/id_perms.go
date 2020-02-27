package models

var defaultIDPerms = &IdPermsType{
	Enable:      true,
	Permissions: defaultPermissions,
}

// NewIDPerms creates new UUIdType instance
func NewIDPerms(uuid string) *IdPermsType {
	return &IdPermsType{
		UUID:   NewUUIDType(uuid),
		Enable: true,
	}
}

// EnsureDefault sets default permissions for a new object
func (m *IdPermsType) EnsureDefault(uuid string) {
	if m == nil {
		return
	}
	if m.UUID == nil {
		m.UUID = NewUUIDType(uuid)
	}

	m.Merge(defaultIDPerms)
}

// Merge sets undefined fields from source values
func (m *IdPermsType) Merge(source *IdPermsType) {
	if !m.Enable {
		m.Enable = source.Enable
	}
	if m.Description == "" {
		m.Description = source.Description
	}
	if m.Created == "" {
		m.Created = source.Created
	}
	if m.Creator == "" {
		m.Creator = source.Creator
	}
	if m.LastModified == "" {
		m.LastModified = source.LastModified
	}
	if source.Permissions == nil {
		return
	}
	if m.Permissions == nil {
		m.Permissions = new(PermType)
	}
	m.Permissions.Merge(source.Permissions)
}

// IsUUIDMatch verify that IdPermsType.UUID match uuid (after transforming to longs)
func (m *IdPermsType) IsUUIDMatch(uuid string) bool {
	if m == nil || m.UUID == nil {
		return true
	}
	uuidType := NewUUIDType(uuid)
	return uuidType.UUIDLslong == m.UUID.UUIDLslong && uuidType.UUIDMslong == m.UUID.UUIDMslong
}
