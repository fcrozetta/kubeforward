package main

import (
	"flag"
	"fmt"
	"os"
)

const version = "0.1.0"

type forwardRequest struct {
	service    string
	namespace  string
	localPort  int
	remotePort int
	context    string
}

func parseArgs() forwardRequest {
	fs := flag.NewFlagSet(os.Args[0], flag.ExitOnError)
	namespace := fs.String("namespace", "default", "Namespace containing the service")
	localPort := fs.Int("local-port", 8080, "Local port to bind")
	remotePort := fs.Int("remote-port", 80, "Target service port")
	context := fs.String("context", "", "Optional kubeconfig context to use")
	showVersion := fs.Bool("version", false, "Print version and exit")

	fs.Usage = func() {
		fmt.Fprintf(fs.Output(), "kubeforward - forward a Kubernetes service to a local port.\n\n")
		fmt.Fprintf(fs.Output(), "Usage:\n  kubeforward [flags] <service>\n\n")
		fmt.Fprintf(fs.Output(), "Flags:\n")
		fs.PrintDefaults()
	}

	_ = fs.Parse(os.Args[1:])

	if *showVersion {
		fmt.Fprintln(fs.Output(), version)
		os.Exit(0)
	}

	args := fs.Args()
	if len(args) == 0 {
		fmt.Fprintln(fs.Output(), "error: service name is required")
		fs.Usage()
		os.Exit(2)
	}

	return forwardRequest{
		service:    args[0],
		namespace:  *namespace,
		localPort:  *localPort,
		remotePort: *remotePort,
		context:    *context,
	}
}

func main() {
	request := parseArgs()
	contextFragment := ""
	if request.context != "" {
		contextFragment = fmt.Sprintf(" (context: %s)", request.context)
	}

	fmt.Printf(
		"Starting forward: %s in %s %d:%d%s\n",
		request.service,
		request.namespace,
		request.localPort,
		request.remotePort,
		contextFragment,
	)
}
