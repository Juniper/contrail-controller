package vcenter
import (
	"context"
	"net/url"
	"runtime"
	"testing"
	"github.com/vmware/govmomi/simulator"
)
func TestNew(t *testing.T) {
	u := &url.URL{Host: "http://user:pass@127.0.0.1:0"}
	tag := " (govmomi simulator)"
	model := simulator.VPX()
	model.ServiceContent.About.Name += tag
	model.ServiceContent.About.OsType = runtime.GOOS + "-" + runtime.GOARCH
	if err := model.Create(); err != nil {
		t.Fatal(err)
	}
	defer model.Remove()
	s := model.Service.NewServer()
	s.URL = u
	addr := s.Listener.Addr()
	defer s.Close()
	c, err := New(context.Background(), "http://user:pass@"+addr.String(), true)
	if err != nil {
		t.Fatalf("failed to create client: %v", err)
	}
	defer c.Close()
	got, err := c.DataCenterHostSystems(context.Background(), "DC0")
	if err != nil {
		t.Fatal("Test failed, expected nil err")
	}
	if len(got) != 4 {
		t.Fatalf("Test failed, expected 4 host %d", len(got))
	}
}
