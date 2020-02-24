package feature

import (
	"fmt"
	"github.com/spf13/viper"

	contrail "github.com/Juniper/contrail-go-api"
)

func getAPIClient() (*contrail.Client, error) {
	hosts := viper.GetStringSlice("apiserver.hosts")
	port := viper.GetInt("apiserver.port")

	contrailClient := contrail.NewClientPool(hosts, port)

	if len(viper.GetString("keystone.authurl")) > 0 {
		if err := setupAuthKeystone(contrailClient); err != nil {
			return nil, err
		}
	}

	_, err := contrailClient.List("global-system-config")
	if err == nil {
		return contrailClient, nil
	}
	return contrailClient, fmt.Errorf("%s", "cannot get api server client")

}

func setupAuthKeystone(client *contrail.Client) error {
	keystone := contrail.NewKeystoneClient(
		viper.GetString("keystone.authurl"),
		viper.GetString("keystone.admin_tenant_name"),
		viper.GetString("keystone.admin_user"),
		viper.GetString("keystone.admin_password"),
		"",
	)
	if err := keystone.Authenticate(); err != nil {
		return err
	}
	client.SetAuthenticator(keystone)

	return nil
}
