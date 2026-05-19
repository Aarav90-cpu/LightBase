#!/usr/bin/env bash
# ==============================================================================
# 🏎️ LIGHTBASE CLI BATCH AUTOMATION EXECUTOR (lb-cli)
# ==============================================================================

TARGET_HOST="${1:-httpbin.org}"
TARGET_PATH="${2:-/get}"
REQUEST_METHOD="${3:-GET}"

echo "======================================================================"
echo "🏎️  BOOTING LIGHTBASE HEADLESS LINUX AUTOMATION RUNNER (lb-cli)"
echo "======================================================================"
echo "[lb-cli] Directing target: ${REQUEST_METHOD} https://${TARGET_HOST}${TARGET_PATH}"

# Construct a pure, lightweight JSON transaction packet string natively via Bash strings
JSON_PAYLOAD=$(cat <<JSON
{
    "method": "${REQUEST_METHOD}",
    "hostname": "${TARGET_HOST}",
    "path": "${TARGET_PATH}",
    "headers": [{"key": "User-Agent", "value": "LightBaseCLI/1.0"}]
}
JSON
)

# Fire the transaction directly across our zero-dependency local bridge endpoint server using curl!
RESPONSE=$(curl -s -X POST -H "Content-Type: application/json" \
    -d "$JSON_PAYLOAD" \
    http://localhost:8000/request)

echo -e "\n[lb-cli] Network Transaction Complete! Extraction metrics logged:"
echo "----------------------------------------------------------------------"
echo "$RESPONSE" | jq . 2>/dev/null || echo "$RESPONSE"
echo "----------------------------------------------------------------------"
echo "🏁 [lb-cli] Execution pipeline closed successfully on bare metal!"
