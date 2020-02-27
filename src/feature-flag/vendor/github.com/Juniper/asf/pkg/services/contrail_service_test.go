package services

import (
	"context"
	"testing"

	"github.com/stretchr/testify/assert"

	"github.com/Juniper/asf/pkg/models"
	"github.com/Juniper/asf/pkg/models/basemodels"
	// TODO(dfurman): Decouple from below packages
	//"github.com/Juniper/asf/pkg/auth"
)

func TestBasePropertiesGetDefaultValuesOnCreate(t *testing.T) {
	tests := []struct {
		name     string
		model    models.AccessControlList
		metadata basemodels.Metadata
		want     models.AccessControlList
		fails    bool
	}{
		{name: "empty", fails: true},
		{
			name:  "missing parent type - ambiguous",
			model: models.AccessControlList{ParentUUID: "parent-uuid"},
			fails: true,
		},
		{
			name: "parent uuid and type provided",
			model: models.AccessControlList{
				UUID:       "4789f49b-a6df-4744-1ecf-60b0958e45e6",
				ParentUUID: "parent-uuid",
				ParentType: "virtual-network",
			},
			metadata: basemodels.Metadata{FQName: []string{"default-domain", "default-project", "default-virtual-network"}},
			want: models.AccessControlList{
				UUID:       "4789f49b-a6df-4744-1ecf-60b0958e45e6",
				ParentUUID: "parent-uuid",
				ParentType: "virtual-network",
				// Default filled fields below
				Name: "default-access-control-list",
				FQName: []string{
					"default-domain", "default-project", "default-virtual-network", "default-access-control-list"},
				Perms2: &models.PermType2{
					Owner:       "default-project",
					OwnerAccess: basemodels.PermsRWX,
					Share:       []*models.ShareType{},
				},
				IDPerms: &models.IdPermsType{
					Enable:      true,
					UserVisible: true,
					UUID: &models.UuidType{
						UUIDMslong: 5154920197859002180,
						UUIDLslong: 2220099452856583654,
					},
					Permissions: &models.PermType{
						Owner:       "cloud-admin",
						OwnerAccess: 7,
						OtherAccess: 7,
						Group:       "cloud-admin-group",
						GroupAccess: 7,
					},
				},
			},
		},
		{
			name: "fill default display name",
			model: models.AccessControlList{
				UUID:       "4789f49b-a6df-4744-1ecf-60b0958e45e6",
				ParentUUID: "parent-uuid",
				ParentType: "virtual-network",
				Name:       "some-name",
				FQName: []string{
					"default-domain", "default-project", "default-virtual-network", "default-access-control-list"},
				Perms2: &models.PermType2{Owner: "default-project"},
			},
			want: models.AccessControlList{
				UUID:       "4789f49b-a6df-4744-1ecf-60b0958e45e6",
				ParentUUID: "parent-uuid",
				ParentType: "virtual-network",
				Name:       "some-name",
				FQName: []string{
					"default-domain", "default-project", "default-virtual-network", "default-access-control-list"},
				Perms2: &models.PermType2{
					Owner:       "default-project",
					OwnerAccess: basemodels.PermsRWX,
					Share:       []*models.ShareType{},
				},
				// Default filled fields below
				IDPerms: &models.IdPermsType{
					Enable:      true,
					UserVisible: true,
					UUID: &models.UuidType{
						UUIDMslong: 5154920197859002180,
						UUIDLslong: 2220099452856583654,
					},
					Permissions: &models.PermType{
						Owner:       "cloud-admin",
						OwnerAccess: 7,
						OtherAccess: 7,
						Group:       "cloud-admin-group",
						GroupAccess: 7,
					},
				},
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			spy := &serviceSpy{}
			tv, err := models.NewTypeValidatorWithFormat()
			assert.NoError(t, err)

			service := &ContrailService{
				BaseService:    BaseService{next: spy},
				MetadataGetter: (*mockMetadataGetter)(&tt.metadata),
				TypeValidator:  tv,
			}
			_, err = service.CreateAccessControlList(
				auth.NoAuth(context.Background()),
				&CreateAccessControlListRequest{AccessControlList: &tt.model},
			)
			if tt.fails {
				assert.Error(t, err)
			} else {
				assert.NoError(t, err)
				assert.Equal(t, tt.want, *spy.acl)
			}
		})
	}
}

type mockMetadataGetter basemodels.Metadata

func (m *mockMetadataGetter) GetMetadata(
	_ context.Context,
	_ basemodels.Metadata,
) (*basemodels.Metadata, error) {
	return (*basemodels.Metadata)(m), nil
}

func (m *mockMetadataGetter) ListMetadata(
	ctx context.Context,
	metadataSlice []*basemodels.Metadata,
) ([]*basemodels.Metadata, error) {
	return []*basemodels.Metadata{(*basemodels.Metadata)(m)}, nil
}

type serviceSpy struct {
	BaseService
	acl *models.AccessControlList
}

func (s *serviceSpy) CreateAccessControlList(
	ctx context.Context,
	request *CreateAccessControlListRequest,
) (*CreateAccessControlListResponse, error) {
	s.acl = request.AccessControlList
	return nil, nil
}
