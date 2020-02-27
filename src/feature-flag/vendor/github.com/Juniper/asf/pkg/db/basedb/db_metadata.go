package basedb

import (
	"bytes"
	"context"
	"fmt"
	"strings"

	"github.com/pkg/errors"

	"github.com/Juniper/asf/pkg/errutil"
	"github.com/Juniper/asf/pkg/models/basemodels"
)

// CreateMetadata creates fqname, uuid pair with type.
func (db *BaseDB) CreateMetadata(ctx context.Context, metaData *basemodels.Metadata) error {
	return db.DoInTransaction(ctx, func(ctx context.Context) error {
		tx := GetTransaction(ctx)
		fqNameStr, err := fqNameToString(metaData.FQName)
		if err != nil {
			return errors.Wrap(err, "failed to stringify fq_name")
		}
		_, err = tx.Exec(
			"insert into metadata (uuid,type,fq_name) values ("+
				db.Dialect.Values("uuid", "type", "fq_name")+");",
			metaData.UUID, metaData.Type, fqNameStr)
		err = FormatDBError(err)
		return errors.Wrap(err, "failed to create metadata")
	})
}

// GetMetadata gets metadata from database. It requires type and fq_name or uuid.
func (db *BaseDB) GetMetadata(
	ctx context.Context,
	requested basemodels.Metadata,
) (*basemodels.Metadata, error) {
	metadatas, err := db.ListMetadata(ctx, []*basemodels.Metadata{&requested})

	if err != nil {
		return nil, errors.Wrapf(FormatDBError(err), "failed to get metadata")
	}

	if len(metadatas) == 1 {
		return metadatas[0], nil
	}

	return nil, errutil.ErrorNotFound
}

func buildMetadataFilter(
	dialect Dialect,
	metaDatas []*basemodels.Metadata,
) (where string, values []interface{}, err error) {
	index := 0
	var filters []string
	var t, uuid, fqName string
	for _, m := range metaDatas {
		if (m.Type == "" || len(m.FQName) == 0) && m.UUID == "" {
			return "", nil, fmt.Errorf("uuid or pair of fq_name and type is required")
		}

		if m.UUID != "" {
			uuid, index = dialect.ValuesWithIndex(index, m.UUID)
			values = append(values, m.UUID)
			filters = append(filters, " ( uuid = "+uuid+" ) ")
		} else {
			t, index = dialect.ValuesWithIndex(index, m.Type)
			values = append(values, m.Type)
			fqNameStr, err := fqNameToString(m.FQName)
			if err != nil {
				return "", nil, errors.Wrapf(err, "failed to stringify fq_name")
			}
			fqName, index = dialect.ValuesWithIndex(index, fqNameStr)
			values = append(values, fqNameStr)
			filters = append(filters, " ( type = "+t+" and fq_name = "+fqName+" ) ")
		}
	}
	where = strings.Join(filters, " or ")
	return where, values, nil
}

// ListMetadata gets metadata from database.
func (db *BaseDB) ListMetadata(
	ctx context.Context,
	requested []*basemodels.Metadata,
) ([]*basemodels.Metadata, error) {
	var metadatas []*basemodels.Metadata

	var query bytes.Buffer
	query.WriteString("select uuid,type, fq_name from metadata where ")

	where, values, err := buildMetadataFilter(db.Dialect, requested)
	if err != nil {
		return nil, err
	}
	query.WriteString(where)

	if err := db.DoInTransaction(ctx, func(ctx context.Context) error {
		tx := GetTransaction(ctx)

		rows, err := tx.QueryContext(ctx, query.String(), values...)
		if err != nil {
			return err
		}

		for rows.Next() {
			metadata := &basemodels.Metadata{}
			fqNameString := ""
			err := rows.Scan(&metadata.UUID, &metadata.Type, &fqNameString)
			err = FormatDBError(err)
			if err != nil {
				return errors.Wrap(err, "couldn't get metadata list")
			}
			fqName, err := ParseFQName(fqNameString)
			if err != nil {
				return err
			}
			metadata.FQName = fqName
			metadatas = append(metadatas, metadata)
		}

		return nil
	}); err != nil {
		return nil, err
	}

	return metadatas, nil
}

// DeleteMetadata deletes metadata by uuid.
func (db *BaseDB) DeleteMetadata(ctx context.Context, uuid string) error {
	return db.DoInTransaction(ctx, func(ctx context.Context) error {
		tx := GetTransaction(ctx)
		_, err := tx.Exec("delete from metadata where uuid = "+db.Dialect.Placeholder(1), uuid)
		err = FormatDBError(err)
		return errors.Wrap(err, "failed to delete metadata")
	})
}
