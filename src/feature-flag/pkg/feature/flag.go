package feature

import (
	"fmt"

	"feature-flag/pkg/vncapi"
)

//Manage feature flags
func Manage() {
	fmt.Println("pass")
	c, err := vncapi.GetAPIClient()
	if err != nil {
		fmt.Println(err)
	}
	fmt.Println(c.GetServer())
}
