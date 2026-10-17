#!/usr/bin/env bash
# 重建 ARCHITECTURE.html（一键）
set -euo pipefail
cd "$(dirname "$0")"
python3 gen_arch.py
python3 assemble.py
echo "✅ 重建完成: $(pwd)/ARCHITECTURE.html"
