package db

import (
	"context"
	"fmt"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"

	"github.com/Juniper/asf/pkg/db/basedb"
)

func TestIntPool(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	err := db.DoInTransaction(
		ctx,
		func(ctx context.Context) error {
			err := clearIntOwner(ctx)
			assert.NoError(t, err)

			poolKey := "testPool"

			err = db.DeleteIntPool(ctx, poolKey)
			assert.NoError(t, err, "clear pool failed")

			err = db.CreateIntPool(ctx, poolKey, 0, 2)
			assert.NoError(t, err, "create pool failed")

			err = db.CreateIntPool(ctx, poolKey, 3, 5)
			assert.NoError(t, err, "create pool failed")

			err = db.CreateIntPool(ctx, poolKey, 3, 5)
			assert.Error(t, err, "it shouldn't be possible to create the same pool again")

			pools, err := db.GetIntPools(ctx, &IntPool{Key: poolKey})
			assert.NoError(t, err)
			assert.Equal(t, 2, len(pools), "get pool failed")

			size, err := db.SizeIntPool(ctx, poolKey)
			assert.NoError(t, err)
			assert.Equal(t, 4, size, "size pool failed")

			i, err := db.AllocateInt(ctx, poolKey, "firewall")
			assert.NoError(t, err, "allocate failed")
			assert.Equal(t, int64(0), i, "allocate failed")

			owner, err := db.GetIntOwner(ctx, poolKey, i)
			assert.NoError(t, err, "get int owner failed")
			assert.Equal(t, "firewall", owner, "get int owner failed")

			err = db.SetInt(ctx, poolKey, i, "firewall")
			assert.NoError(t, err, "setting the same id for the same owner failed")

			err = db.SetInt(ctx, poolKey, i, "another_firewall")
			assert.Error(t, err, "setting the same id for a different owner should fail")

			i, err = db.AllocateInt(ctx, poolKey, EmptyIntOwner)
			assert.NoError(t, err, "allocate failed")
			assert.Equal(t, int64(1), i, "allocate failed")

			i, err = db.AllocateInt(ctx, poolKey, EmptyIntOwner)
			assert.NoError(t, err, "allocate failed")
			assert.Equal(t, int64(3), i, "allocate failed")

			size, err = db.SizeIntPool(ctx, poolKey)
			assert.NoError(t, err)
			assert.Equal(t, 1, size, "size pool failed")

			pools, err = db.GetIntPools(ctx, &IntPool{Key: poolKey})
			assert.NoError(t, err)
			assert.Equal(t, 1, len(pools), "get pool failed")

			err = db.DeallocateInt(ctx, poolKey, 0)
			assert.NoError(t, err, "deallocate failed")

			owner, err = db.GetIntOwner(ctx, poolKey, 0)
			assert.Error(t, err, "get int owner should return ErrorNotFound")
			assert.Equal(t, "", owner, "int owner should be empty")

			err = db.DeallocateInt(ctx, poolKey, 3)
			assert.NoError(t, err, "deallocate failed")

			pools, err = db.GetIntPools(ctx, &IntPool{Key: poolKey})
			assert.NoError(t, err)
			assert.Equal(t, 2, len(pools), "get pool failed")

			size, err = db.SizeIntPool(ctx, poolKey)
			assert.NoError(t, err)
			assert.Equal(t, 3, size, "size pool failed")

			err = db.SetInt(ctx, poolKey, 4, "vlan")
			assert.NoError(t, err, "set failed")

			owner, err = db.GetIntOwner(ctx, poolKey, 4)
			assert.NoError(t, err, "get int owner failed")
			assert.Equal(t, "vlan", owner, "get int owner failed")

			err = db.SetInt(ctx, poolKey, 4, EmptyIntOwner)
			assert.Error(t, err, "setting the same ID should fail")

			pools, err = db.GetIntPools(ctx, &IntPool{Key: poolKey})
			assert.NoError(t, err)
			assert.Equal(t, 2, len(pools), "get pool failed")

			size, err = db.SizeIntPool(ctx, poolKey)
			assert.NoError(t, err)
			assert.Equal(t, 2, size, "size pool failed")

			err = db.DeleteIntPool(ctx, poolKey)
			assert.NoError(t, err, "delete pool failed")

			pools, err = db.GetIntPools(ctx, &IntPool{Key: poolKey})
			assert.NoError(t, err)
			assert.Equal(t, 0, len(pools), "get pool failed")
			return nil
		})
	assert.NoError(t, err)
}

func clearIntOwner(ctx context.Context) error {
	_, err := basedb.GetTransaction(ctx).ExecContext(ctx, "delete from int_owner")
	return err
}

func TestIntPoolSetIntWithSameOwner(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	err := db.DoInTransaction(ctx, func(ctx context.Context) error {
		poolKey := "test_pool"
		firstOwner := "first_firewall"
		id := int64(10)

		err := db.DeleteIntPool(ctx, poolKey)
		assert.NoError(t, err, "clear pool failed")

		err = db.CreateIntPool(ctx, poolKey, 0, 65535)
		assert.NoError(t, err, "create pool failed")

		err = db.SetInt(ctx, poolKey, id, firstOwner)
		assert.NoError(t, err, "allocate failed")

		owner, err := db.GetIntOwner(ctx, poolKey, id)
		assert.NoError(t, err, "get int owner failed")
		assert.Equal(t, firstOwner, owner)

		err = db.SetInt(ctx, poolKey, id, firstOwner)
		assert.NoError(t, err, "setting the same id for the same owner failed")

		err = db.DeleteIntPool(ctx, poolKey)
		assert.NoError(t, err, "delete pool failed")
		return nil
	})
	assert.NoError(t, err)
}

func TestAllocateOutsidePoolRange(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	err := db.DoInTransaction(ctx, func(ctx context.Context) error {
		poolKey := fmt.Sprintf("%v_test_pool", t.Name())
		firstInt := int64(5)

		err := db.DeleteIntPool(ctx, poolKey)
		assert.NoError(t, err, "clear pool failed")

		err = db.CreateIntPool(ctx, poolKey, firstInt, 65535)
		assert.NoError(t, err, "create pool failed")

		err = db.DeallocateInt(ctx, poolKey, 0)
		assert.NoError(t, err, "deallocating 0 failed")

		i, err := db.AllocateInt(ctx, poolKey, EmptyIntOwner)
		assert.NoError(t, err, "allocate failed")
		assert.Equal(t, int64(0), i, "allocate failed")

		err = db.DeleteIntPool(ctx, poolKey)
		assert.NoError(t, err, "delete pool failed")
		return nil
	})
	assert.NoError(t, err)
}
