#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>

enum class ObsidianTargetMode : uint8_t {
  LOCAL_REST_API = 0,  // Obsidian "Local REST API" community plugin (writes directly into the vault)
  WEBHOOK = 1,         // Generic JSON webhook (n8n, Make, Home Assistant, a custom relay, etc.)
};

const char* obsidianTargetModeToJson(ObsidianTargetMode mode);
ObsidianTargetMode obsidianTargetModeFromJson(const char* value);

struct ObsidianSyncConfig {
  bool enabled = false;
  ObsidianTargetMode mode = ObsidianTargetMode::LOCAL_REST_API;
  // Local REST API: e.g. https://192.168.1.50:27124. Webhook: the full endpoint URL.
  // Prefer a mDNS hostname here (e.g. https://your-mac.local:27124) over a raw
  // IP: it survives the host's DHCP lease changing, which a bare IP does not.
  std::string baseUrl;
  // Tried only if baseUrl fails to connect at all (DNS/TCP failure, not an
  // auth or server error; retrying elsewhere wouldn't fix those). Useful as
  // a raw-IP backup for when the primary is a hostname that occasionally
  // fails to resolve, or vice versa. Optional; empty disables the fallback.
  std::string fallbackBaseUrl;
  // Local REST API: the plugin's bearer token. Webhook: an optional bearer token (may be empty).
  std::string apiKey;
  // Local REST API only. "{book}" is replaced with a sanitized book title.
  std::string notePathTemplate = "Clippings/{book}.md";
  // Skip TLS certificate verification. The Local REST API plugin serves a
  // self-signed cert by default, so this defaults on for LAN use.
  bool insecureTls = true;
  // Push the pending queue automatically whenever File Transfer / Calibre
  // Wireless mode starts (Wi-Fi is already up then, so this costs no extra
  // battery). When off, syncing is manual only (Settings > Obsidian Sync,
  // or the web UI's "Sync Now" button).
  bool autoSyncOnFileTransfer = false;
};

/**
 * Singleton store for the Obsidian clipping-sync destination. Mirrors
 * OpdsServerStore/KOReaderCredentialStore: JSON on the SD card, API key
 * obfuscated with the device's hardware MAC key, lazy-loaded on first access.
 */
class ObsidianSyncStore : public PersistableStore<ObsidianSyncStore> {
 private:
  ObsidianSyncConfig config;
  bool loaded_ = false;

  ObsidianSyncStore() = default;
  friend class PersistableStore<ObsidianSyncStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/obsidian.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  void ensureLoaded() const;

  const ObsidianSyncConfig& getConfig() const {
    ensureLoaded();
    return config;
  }
  // Replaces the config wholesale and persists it.
  bool setConfig(const ObsidianSyncConfig& newConfig);
};

#define OBSIDIAN_STORE ObsidianSyncStore::getInstance()
