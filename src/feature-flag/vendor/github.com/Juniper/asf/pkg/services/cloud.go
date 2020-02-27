package services

import (
	"encoding/base64"
	"fmt"
	"io/ioutil"
	"net/http"
	"os"
	"path"
	"strings"

	"github.com/labstack/echo"
)

//API Path definitions.
const (
	UploadCloudKeysPath    = "upload-cloud-keys"
	keyHomeDir             = "/var/tmp/contrail"
	secretKeyFileName      = "aws_secret.key"
	accessKeyFileName      = "aws_access.key"
	accountFileName        = "google-account.json"
	subscriptionIDFileName = "subscription_id"
	clientIDFileName       = "client_id"
	clientSecretFileName   = "client_secret"
	tenantIDFileName       = "tenant_id"
)

// UploadCloudKeysBody defines data format for /upload-cloud-keys endpoint.
type UploadCloudKeysBody struct {
	// CloudProviderUUID is the UUID of the cloud provider who provided keys
	CloudProviderUUID string `json:"cloud_provider_uuid"`
	// AWSSecretKey is the secret key to created on API Server host.
	AWSSecretKey string `json:"aws_secret_key"`
	// AWSAccessKey is the access key to created on API Server host.
	AWSAccessKey string `json:"aws_access_key"`
	// AzureSubscriptionID is the subscription id to created on API Server host.
	AzureSubscriptionID string `json:"azure_subscription_id"`
	// AzureClientID is the client id to created on API Server host.
	AzureClientID string `json:"azure_client_id"`
	// AzureClientSecret is the client secret to created on API Server host.
	AzureClientSecret string `json:"azure_client_secret"`
	// AzureTenantID is the tenant id to created on API Server host.
	AzureTenantID string `json:"azure_tenant_id"`
	// GoogleAccountJson is the account file to created on API Server host.
	GoogleAccount string `json:"google_account_json"`
}

// KeyFileDefaults defines data format for various cloud secret file
type KeyFileDefaults struct {
	KeyHomeDir             string
	SecretKeyFileName      string
	AccessKeyFileName      string
	SubscriptionIDFileName string
	ClientIDFileName       string
	ClientSecretFileName   string
	TenantIDFileName       string
	AccountFileName        string
}

// NewKeyFileDefaults returns defaults for various cloud secret files.
func NewKeyFileDefaults() (defaults *KeyFileDefaults) {
	return &KeyFileDefaults{
		keyHomeDir,
		secretKeyFileName,
		accessKeyFileName,
		subscriptionIDFileName,
		clientIDFileName,
		clientSecretFileName,
		tenantIDFileName,
		accountFileName,
	}
}

// GetAWSSecretPath determines the aws secret key path
func (defaults *KeyFileDefaults) GetAWSSecretPath() string {
	return path.Join(defaults.KeyHomeDir, defaults.SecretKeyFileName)
}

// GetAWSAccessPath determines the aws access key path
func (defaults *KeyFileDefaults) GetAWSAccessPath() string {
	return path.Join(defaults.KeyHomeDir, defaults.AccessKeyFileName)
}

// GetAzureSubscriptionIDPath determines the azure subscription id path
func (defaults *KeyFileDefaults) GetAzureSubscriptionIDPath() string {
	return path.Join(defaults.KeyHomeDir, defaults.SubscriptionIDFileName)
}

// GetAzureClientIDPath determines the azure client id path
func (defaults *KeyFileDefaults) GetAzureClientIDPath() string {
	return path.Join(defaults.KeyHomeDir, defaults.ClientIDFileName)
}

// GetAzureClientSecretPath determines the azure client secret path
func (defaults *KeyFileDefaults) GetAzureClientSecretPath() string {
	return path.Join(defaults.KeyHomeDir, defaults.ClientSecretFileName)
}

// GetAzureTenantIDPath determines the azure tenant id path
func (defaults *KeyFileDefaults) GetAzureTenantIDPath() string {
	return path.Join(defaults.KeyHomeDir, defaults.TenantIDFileName)
}

// GetGoogleAccountPath determines the google account path
func (defaults *KeyFileDefaults) GetGoogleAccountPath() string {
	return path.Join(defaults.KeyHomeDir, defaults.AccountFileName)
}

// RESTUploadCloudKeys handles an /upload-cloud-keys REST request.
func (service *ContrailService) RESTUploadCloudKeys(c echo.Context) error {
	var request *UploadCloudKeysBody
	if err := c.Bind(&request); err != nil {
		return echo.NewHTTPError(http.StatusBadRequest,
			fmt.Sprintf("invalid JSON format: %v", err))
	}
	return service.UploadCloudKeys(request, NewKeyFileDefaults())
}

// UploadCloudKeys stores specified cloud secrets
func (service *ContrailService) UploadCloudKeys(request *UploadCloudKeysBody, keyDefaults *KeyFileDefaults) error {
	keyPaths := []string{}
	for _, secret := range []struct {
		keyType string
		encoded string
		path    string
	}{{
		keyType: "aws-secret-key",
		encoded: request.AWSSecretKey,
		path:    keyDefaults.GetAWSSecretPath(),
	}, {
		keyType: "aws-access-key",
		encoded: request.AWSAccessKey,
		path:    keyDefaults.GetAWSAccessPath(),
	}, {
		keyType: "azure-subscription-id",
		encoded: request.AzureSubscriptionID,
		path:    keyDefaults.GetAzureSubscriptionIDPath(),
	}, {
		keyType: "azure-client-id",
		encoded: request.AzureClientID,
		path:    keyDefaults.GetAzureClientIDPath(),
	}, {
		keyType: "azure-client-secret",
		encoded: request.AzureClientSecret,
		path:    keyDefaults.GetAzureClientSecretPath(),
	}, {
		keyType: "azure-tenant-id",
		encoded: request.AzureTenantID,
		path:    keyDefaults.GetAzureTenantIDPath(),
	}, {
		keyType: "google-account",
		encoded: request.GoogleAccount,
		path:    keyDefaults.GetGoogleAccountPath(),
	}} {
		if err := decodeAndStoreCloudKey(secret.keyType, secret.path, secret.encoded, keyPaths); err != nil {
			return echo.NewHTTPError(http.StatusInternalServerError,
				fmt.Sprintf("failed to store secret key: %v", err))
		}
		keyPaths = append(keyPaths, secret.path)
	}
	return nil
}

func decodeAndStoreCloudKey(keyType, keyPath, encodedSecret string, existingKeyPaths []string) error {
	decodedSecret, err := base64.StdEncoding.DecodeString(encodedSecret)
	if err != nil {
		errstrings := []string{fmt.Sprintf("failed to base64-decode %s: %v", keyType, err)}
		errstrings = append(errstrings, cleanupCloudKeys(existingKeyPaths)...)
		if len(errstrings) != 0 {
			return fmt.Errorf(strings.Join(errstrings, "\n"))
		}
	}

	if len(decodedSecret) == 0 {
		return nil
	}

	err = os.MkdirAll(path.Dir(keyPath), 0755)
	if err != nil {
		errstrings := []string{fmt.Sprintf("failed to make dir for %s: %v", keyType, err)}
		errstrings = append(errstrings, cleanupCloudKeys(existingKeyPaths)...)
		if len(errstrings) != 0 {
			return fmt.Errorf(strings.Join(errstrings, "\n"))
		}
	}

	if err = ioutil.WriteFile(keyPath, decodedSecret, 0644); err != nil {
		errstrings := []string{fmt.Sprintf("failed to store %s: %v", keyType, err)}
		errstrings = append(errstrings, cleanupCloudKeys(existingKeyPaths)...)
		if len(errstrings) != 0 {
			return fmt.Errorf(strings.Join(errstrings, "\n"))
		}
	}

	return nil
}

func cleanupCloudKeys(keyPaths []string) (errstrings []string) {
	for _, keyPath := range keyPaths {
		err := os.Remove(keyPath)
		if err != nil {
			errstrings = append(errstrings, fmt.Sprintf("Unable to delete %s: %v", keyPath, err))
		}
	}
	return errstrings
}
