package feature

import (
	"fmt"
)

//Manage feature flags
func Manage() {
	fmt.Println("pass")
	c, err := getAPIClient()
	if err != nil {
		fmt.Println(err)
	}
	fmt.Println(c.GetServer())
}
