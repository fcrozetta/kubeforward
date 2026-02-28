#!/usr/bin/env bash

set -euo pipefail

BINARY_PATH="${1:-}"
if [[ -z "${BINARY_PATH}" ]]; then
  echo "usage: $0 <kubeforward-binary>" >&2
  exit 2
fi

if [[ ! -x "${BINARY_PATH}" ]]; then
  echo "binary is not executable: ${BINARY_PATH}" >&2
  exit 2
fi

WORKDIR="${KUBEFORWARD_KIND_SMOKE_WORKDIR:-$(pwd)/out/kind-smoke}"
CLUSTER_NAME="${KUBEFORWARD_KIND_SMOKE_CLUSTER_NAME:-kubeforward-smoke}"
CONTEXT_NAME="kind-${CLUSTER_NAME}"
STATE_FILE="${WORKDIR}/state.yaml"
CONFIG_FILE="${WORKDIR}/kubeforward.yaml"
KIND_LOG_DIR="${WORKDIR}/kind-logs"
LOCAL_PORT="${KUBEFORWARD_KIND_SMOKE_LOCAL_PORT:-18080}"

mkdir -p "${WORKDIR}"

cleanup() {
  local exit_code=$?
  set +e

  if [[ -f "${CONFIG_FILE}" ]]; then
    KUBEFORWARD_STATE_FILE="${STATE_FILE}" "${BINARY_PATH}" down --file "${CONFIG_FILE}" --env dev --verbose || true
  fi

  if [[ ${exit_code} -ne 0 ]]; then
    kind export logs "${KIND_LOG_DIR}" --name "${CLUSTER_NAME}" >/dev/null 2>&1 || true
  fi

  kind delete cluster --name "${CLUSTER_NAME}" >/dev/null 2>&1 || true
  exit "${exit_code}"
}
trap cleanup EXIT

kind create cluster --name "${CLUSTER_NAME}" --wait 120s

kubectl --context "${CONTEXT_NAME}" create deployment api --image=nginx:1.27.5
kubectl --context "${CONTEXT_NAME}" rollout status deployment/api --timeout=180s

cat > "${CONFIG_FILE}" <<EOF
version: 1
metadata:
  project: kind-smoke
defaults:
  context: ${CONTEXT_NAME}
  namespace: default
  bindAddress: 127.0.0.1
environments:
  dev:
    forwards:
      - name: api
        resource:
          kind: deployment
          name: api
        annotations:
          detach: true
        ports:
          - local: ${LOCAL_PORT}
            remote: 80
EOF

export KUBEFORWARD_STATE_FILE="${STATE_FILE}"
"${BINARY_PATH}" up --file "${CONFIG_FILE}" --env dev --verbose

for attempt in $(seq 1 20); do
  if curl --fail --silent "http://127.0.0.1:${LOCAL_PORT}/" >/dev/null 2>&1; then
    break
  fi
  if [[ "${attempt}" == "20" ]]; then
    echo "smoke probe failed for forwarded port ${LOCAL_PORT}" >&2
    curl --fail --silent --show-error "http://127.0.0.1:${LOCAL_PORT}/" >/dev/null
    exit 1
  fi
  sleep 1
done

"${BINARY_PATH}" down --file "${CONFIG_FILE}" --env dev --verbose

if [[ -f "${STATE_FILE}" ]] && grep -q "id:" "${STATE_FILE}"; then
  echo "runtime state still contains sessions after down" >&2
  exit 1
fi
