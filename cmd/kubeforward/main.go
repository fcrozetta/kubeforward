package main

import (
	"flag"
	"fmt"
	"os"
	"runtime/debug"

	"gopkg.in/yaml.v3"
)

const defaultVersion = "0.0.0"

var version = defaultVersion

type forwardRequest struct {
	configPath  string
	environment string
}

func newFlagSet() (*flag.FlagSet, *string, *bool) {
	fs := flag.NewFlagSet(os.Args[0], flag.ExitOnError)
	configPath := fs.String("f", "kubeforward.yml", "Path to configuration file")
	showVersion := fs.Bool("version", false, "Print version and exit")

	fs.Usage = func() {
		fmt.Fprintf(fs.Output(), "kubeforward - forward Kubernetes resources based on a config file.\n\n")
		fmt.Fprintf(fs.Output(), "Usage:\n  kubeforward [flags] <environment>\n\n")
		fmt.Fprintf(fs.Output(), "Flags:\n")
		fs.PrintDefaults()
	}

	return fs, configPath, showVersion
}

func parseArgs() forwardRequest {
	fs, configPath, showVersion := newFlagSet()
	_ = fs.Parse(os.Args[1:])

	if *showVersion {
		fmt.Fprintln(fs.Output(), resolvedVersion())
		os.Exit(0)
	}

	if len(fs.Args()) > 0 {
		if len(fs.Args()) > 1 {
			fmt.Fprintln(fs.Output(), "error: too many arguments")
			fs.Usage()
			os.Exit(2)
		}
	}

	return forwardRequest{
		configPath:  *configPath,
		environment: fs.Arg(0),
	}
}

func printUsage() {
	fs, _, _ := newFlagSet()
	fs.SetOutput(os.Stderr)
	fs.Usage()
}

func resolvedVersion() string {
	if version != defaultVersion {
		return version
	}

	info, ok := debug.ReadBuildInfo()
	if !ok || info.Main.Version == "" || info.Main.Version == "(devel)" {
		return defaultVersion
	}

	return info.Main.Version
}

type config struct {
	Environments map[string]environment `yaml:"environments"`
}

type environment struct {
	Context    string               `yaml:"context"`
	Namespaces map[string]namespace `yaml:"namespaces"`
}

type namespace struct {
	Forwards []forward `yaml:"forwards"`
}

type forward struct {
	Kind  string `yaml:"kind"`
	Name  string `yaml:"name"`
	Ports []port `yaml:"ports"`
}

type port struct {
	LocalPort  int `yaml:"localPort"`
	RemotePort int `yaml:"remotePort"`
}

func main() {
	request := parseArgs()
	configData, err := os.ReadFile(request.configPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: unable to read config %q: %v\n", request.configPath, err)
		os.Exit(1)
	}

	var cfg config
	if err := yaml.Unmarshal(configData, &cfg); err != nil {
		fmt.Fprintf(os.Stderr, "error: unable to parse config %q: %v\n", request.configPath, err)
		os.Exit(1)
	}

	if len(cfg.Environments) == 0 {
		fmt.Fprintf(os.Stderr, "error: config %q has no environments defined\n", request.configPath)
		os.Exit(1)
	}

	if request.environment == "" {
		fmt.Fprintln(os.Stderr, "error: missing environment argument")
		fmt.Fprintln(os.Stderr)
		printUsage()
		os.Exit(2)
	}

	selected, ok := cfg.Environments[request.environment]
	if !ok {
		fmt.Fprintf(os.Stderr, "error: environment %q not found in config\n", request.environment)
		os.Exit(1)
	}

	fmt.Printf("Loaded config from %s (%d bytes)\n", request.configPath, len(configData))
	fmt.Printf("Selected environment %s (context %s)\n", request.environment, selected.Context)
}
