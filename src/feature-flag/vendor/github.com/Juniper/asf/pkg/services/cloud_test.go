package services_test

import (
	"encoding/base64"
	"fmt"
	"io/ioutil"
	"os"
	"testing"

	"github.com/Juniper/asf/pkg/services"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	uuid "github.com/satori/go.uuid"
)

func TestUploadCloudKeys(t *testing.T) {
	outputDirectory := "/tmp/test_upload_cloud_keys"

	for _, tt := range []struct {
		name                string
		cloudProviderUUID   string
		awsAccessKey        string
		awsSecretKey        string
		azureSubscriptionID string
		azureClientID       string
		azureClientSecret   string
		azureTenantID       string
		googleAccount       string
		statusCode          int
	}{
		{
			name:                "upload aws/azure/gcp keys",
			cloudProviderUUID:   uuid.NewV4().String(),
			awsAccessKey:        "test access key",
			awsSecretKey:        "test secret key",
			azureSubscriptionID: "test subscription id",
			azureClientID:       "test client id",
			azureClientSecret:   "test client secret",
			azureTenantID:       "test tenant id",
			googleAccount:       "{\"test\": \"account\"}",
		},
		{
			name:              "upload aws keys",
			cloudProviderUUID: uuid.NewV4().String(),
			awsAccessKey:      "test access key",
			awsSecretKey:      "test secret key",
		},
		{
			name:              "upload goolgle keys",
			cloudProviderUUID: uuid.NewV4().String(),
			googleAccount:     "{\"test\": \"account\"}",
		},
		{
			name:                "upload azure keys",
			cloudProviderUUID:   uuid.NewV4().String(),
			azureSubscriptionID: "test subscription id",
			azureClientID:       "test client id",
			azureClientSecret:   "test client secret",
			azureTenantID:       "test tenant id",
		},
	} {
		t.Run(tt.name, func(t *testing.T) {
			require.NoError(t, os.RemoveAll(outputDirectory))

			require.NoError(t, os.MkdirAll(outputDirectory, 0755))

			defer func() {
				if err := os.RemoveAll(outputDirectory); err != nil {
					fmt.Println("Failed to clean up", outputDirectory, ":", err)
				}
			}()

			defaults := services.NewKeyFileDefaults()
			defaults.KeyHomeDir = outputDirectory

			request := &services.UploadCloudKeysBody{
				CloudProviderUUID:   tt.cloudProviderUUID,
				AWSAccessKey:        base64.StdEncoding.EncodeToString([]byte(tt.awsAccessKey)),
				AWSSecretKey:        base64.StdEncoding.EncodeToString([]byte(tt.awsSecretKey)),
				AzureSubscriptionID: base64.StdEncoding.EncodeToString([]byte(tt.azureSubscriptionID)),
				AzureClientID:       base64.StdEncoding.EncodeToString([]byte(tt.azureClientID)),
				AzureClientSecret:   base64.StdEncoding.EncodeToString([]byte(tt.azureClientSecret)),
				AzureTenantID:       base64.StdEncoding.EncodeToString([]byte(tt.azureTenantID)),
				GoogleAccount:       base64.StdEncoding.EncodeToString([]byte(tt.googleAccount)),
			}

			cs := services.ContrailService{}
			require.NoError(t, cs.UploadCloudKeys(request, defaults))

			for keyPath, content := range map[string]string{
				defaults.GetAWSSecretPath():           tt.awsSecretKey,
				defaults.GetAWSAccessPath():           tt.awsAccessKey,
				defaults.GetAzureSubscriptionIDPath(): tt.azureSubscriptionID,
				defaults.GetAzureClientIDPath():       tt.azureClientID,
				defaults.GetAzureClientSecretPath():   tt.azureClientSecret,
				defaults.GetAzureTenantIDPath():       tt.azureTenantID,
				defaults.GetGoogleAccountPath():       tt.googleAccount,
			} {
				if content == "" {
					if _, err := os.Stat(keyPath); err != nil {
						assert.Truef(t, os.IsNotExist(err), "could not open %s, err: %v", keyPath, err)
					} else {
						assert.Failf(t, "File should not be created but it is.", "File: %s", keyPath)
					}
				} else {
					b, err := ioutil.ReadFile(keyPath)
					assert.NoError(t, err)
					assert.Equal(t, content, string(b))
				}
			}
		})
	}
}
