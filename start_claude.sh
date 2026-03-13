#!/usr/bin/env bash

# Z.AI API Key hier eintragen oder vorher als ENV setzen
export ANTHROPIC_AUTH_TOKEN="c9ad1c04238f4c3dbfbd567675a135b5.qx6LJsU6Qqs2ksbC"

# Z.AI Anthropic-kompatibler Endpoint
export ANTHROPIC_BASE_URL="https://api.z.ai/api/anthropic"

# Optional: Timeout großzügig setzen
export API_TIMEOUT_MS="3000000"

# Standard-Modelle für die „Familienslots“ von Claude Code auf GLM-4.7 mappen
# (Haiku/Sonnet/Opus werden intern auf glm-4.7 gelegt)
export ANTHROPIC_DEFAULT_HAIKU_MODEL="glm-4.7"
export ANTHROPIC_DEFAULT_SONNET_MODEL="glm-4.7"
export ANTHROPIC_DEFAULT_OPUS_MODEL="glm-4.7"

# Claude Code im aktuellen Projektordner starten
claude "$@"

