package endpoint

import (
	"testing"

	"github.com/Juniper/asf/pkg/models"
	"github.com/stretchr/testify/assert"
)

// TODO(Daniel): improve test coverage

func TestTargetStoreWriteRead(t *testing.T) {
	const fooID = "foo-id"
	fooE := newEndpoint("foo")

	for _, tt := range []struct {
		name     string
		id       string
		expected *models.Endpoint
	}{
		{
			name: "invalid id",
			id:   "invalid",
		},
		{
			name:     "correct id",
			id:       fooID,
			expected: fooE,
		},
	} {
		t.Run(tt.name, func(t *testing.T) {
			ts := NewTargetStore()
			ts.Write(fooID, fooE)

			e := ts.Read(tt.id)

			assert.Equal(t, tt.expected, e)
		})
	}
}

func TestTargetStoreReadAll(t *testing.T) {
	fooE, barE, spockE := newEndpoint("foo"), newEndpoint("bar"), newEndpoint("spock")

	for _, tt := range []struct {
		name     string
		scope    string
		expected []*Endpoint
	}{
		{
			name:  "invalid URL scope",
			scope: "invalid",
		},
		{
			name:  "private URL scope",
			scope: PrivateURLScope,
			expected: []*Endpoint{
				{
					URL:      fooE.PrivateURL,
					Username: fooE.Username,
					Password: fooE.Password,
				},
				{
					URL:      barE.PrivateURL,
					Username: barE.Username,
					Password: barE.Password,
				},
				{
					URL:      spockE.PrivateURL,
					Username: spockE.Username,
					Password: spockE.Password,
				},
			},
		},
		{
			name:  "public URL scope",
			scope: PublicURLScope,
			expected: []*Endpoint{
				{
					URL:      fooE.PublicURL,
					Username: fooE.Username,
					Password: fooE.Password,
				},
				{
					URL:      barE.PublicURL,
					Username: barE.Username,
					Password: barE.Password,
				},
				{
					URL:      spockE.PublicURL,
					Username: spockE.Username,
					Password: spockE.Password,
				},
			},
		},
	} {
		t.Run(tt.name, func(t *testing.T) {
			ts := NewTargetStore()
			ts.Write("foo", fooE)
			ts.Write("bar", barE)
			ts.Write("spock", spockE)

			targets := ts.ReadAll(tt.scope)

			assert.Len(t, targets, len(tt.expected))
			for _, target := range targets {
				assert.Contains(t, tt.expected, target)
			}
		})
	}
}

func newEndpoint(name string) *models.Endpoint {
	return &models.Endpoint{
		UUID:       name + "-uuid",
		PrivateURL: name + "-private",
		PublicURL:  name + "-public",
		Username:   name + "-username",
		Password:   name + "-password",
	}
}
