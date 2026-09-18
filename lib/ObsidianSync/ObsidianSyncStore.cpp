#include "ObsidianSyncStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cstring>

namespace {
constexpr char MODE_LOCAL_REST_API[] = "local_rest_api";
constexpr char MODE_WEBHOOK[] = "webhook";
}  // namespace

const char* obsidianTargetModeToJson(const ObsidianTargetMode mode) {
  switch (mode) {
    case ObsidianTargetMode::WEBHOOK:
      return MODE_WEBHOOK;
    case ObsidianTargetMode::LOCAL_REST_API:
    default:
      return MODE_LOCAL_REST_API;
  }
}

ObsidianTargetMode obsidianTargetModeFromJson(const char* value) {
  if (value && strcmp(value, MODE_WEBHOOK) == 0) {
    return ObsidianTargetMode::WEBHOOK;
  }
  return ObsidianTargetMode::LOCAL_REST_API;
}

void ObsidianSyncStore::toJson(JsonDocument& doc) const {
  doc["enabled"] = config.enabled;
  doc["mode"] = obsidianTargetModeToJson(config.mode);
  doc["baseUrl"] = config.baseUrl;
  doc["fallbackBaseUrl"] = config.fallbackBaseUrl;
  doc["apiKey_obf"] = obfuscation::obfuscateToBase64(config.apiKey);
  doc["notePathTemplate"] = config.notePathTemplate;
  doc["insecureTls"] = config.insecureTls;
  doc["autoSyncOnFileTransfer"] = config.autoSyncOnFileTransfer;
}

bool ObsidianSyncStore::fromJson(JsonVariantConst doc) {
  config.enabled = doc["enabled"] | false;
  config.mode = obsidianTargetModeFromJson(doc["mode"] | "");
  config.baseUrl = doc["baseUrl"] | "";
  config.fallbackBaseUrl = doc["fallbackBaseUrl"] | "";
  config.notePathTemplate = doc["notePathTemplate"] | "";
  if (config.notePathTemplate.empty()) {
    config.notePathTemplate = "Clippings/{book}.md";
  }
  config.insecureTls = doc["insecureTls"] | true;
  config.autoSyncOnFileTransfer = doc["autoSyncOnFileTransfer"] | false;

  obfuscation::DecodeStatus status = obfuscation::DecodeStatus::INVALID;
  config.apiKey = obfuscation::deobfuscateFromBase64(doc["apiKey_obf"] | "", &status);
  if (status == obfuscation::DecodeStatus::LEGACY && !config.apiKey.empty()) {
    requestResave();
  }
  if ((status == obfuscation::DecodeStatus::INVALID || status == obfuscation::DecodeStatus::EMPTY) &&
      config.apiKey.empty()) {
    // Tolerate a hand-edited plaintext key in obsidian.json; resave obfuscates it.
    config.apiKey = doc["apiKey"] | "";
    if (!config.apiKey.empty()) requestResave();
  }

  return true;
}

void ObsidianSyncStore::ensureLoaded() const {
  if (loaded_) return;
  auto* self = const_cast<ObsidianSyncStore*>(this);
  self->loaded_ = true;
  // Silently leaves `config` default-constructed (disabled) when the file
  // does not exist yet, matching every other store's first-boot behavior.
  self->PersistableStore<ObsidianSyncStore>::loadFromFile();
}

bool ObsidianSyncStore::setConfig(const ObsidianSyncConfig& newConfig) {
  config = newConfig;
  loaded_ = true;
  return saveToFile();
}
