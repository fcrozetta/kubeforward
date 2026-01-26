# kubeforward

CLI to forward kubectl services to local.

## Quick start

```bash
go build ./cmd/kubeforward
```

```bash
./kubeforward api-service --namespace platform --local-port 8080 --remote-port 80
```

## CLI options

```bash
./kubeforward --help
```
