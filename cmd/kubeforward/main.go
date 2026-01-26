package main

import (
	"flag"
	"fmt"
	"os"
)

var version = "0.0.0"

type forwardRequest struct {
	configPath string
}

func parseArgs() forwardRequest {
	fs := flag.NewFlagSet(os.Args[0], flag.ExitOnError)
	configPath := fs.String("f", "kubeforward.yml", "Path to configuration file")
	showVersion := fs.Bool("version", false, "Print version and exit")

	fs.Usage = func() {
		fmt.Fprintf(fs.Output(), "kubeforward - forward Kubernetes resources based on a config file.\n\n")
		fmt.Fprintf(fs.Output(), "Usage:\n  kubeforward [flags]\n\n")
		fmt.Fprintf(fs.Output(), "Flags:\n")
		fs.PrintDefaults()
	}

	_ = fs.Parse(os.Args[1:])

	if *showVersion {
		fmt.Fprintln(fs.Output(), version)
		os.Exit(0)
	}

	if len(fs.Args()) > 0 {
		fmt.Fprintln(fs.Output(), "error: unexpected arguments")
		fs.Usage()
		os.Exit(2)
	}

	return forwardRequest{
		configPath: *configPath,
	}
}

func main() {
	request := parseArgs()
	configData, err := os.ReadFile(request.configPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: unable to read config %q: %v\n", request.configPath, err)
		os.Exit(1)
	}

	fmt.Printf("Loaded config from %s (%d bytes)\n", request.configPath, len(configData))
}
