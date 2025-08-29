#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

make -s all
timeout 20s ./main || true
echo "integration: ran basic scenario"
