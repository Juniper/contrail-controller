package baseservices

import (
	"context"

	"github.com/Juniper/asf/pkg/models/basemodels"
)

//MetadataGetter provides getter for metadata.
type MetadataGetter interface {
	GetMetadata(ctx context.Context, requested basemodels.Metadata) (*basemodels.Metadata, error)
	ListMetadata(ctx context.Context, requested []*basemodels.Metadata) ([]*basemodels.Metadata, error)
}
