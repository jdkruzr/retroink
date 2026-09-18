#include "ObsidianSyncClient.h"

#include <ArduinoJson.h>
#ifdef SIMULATOR
#include <ArduinoJsonStringCompat.h>
#endif
#include <Logging.h>
#ifdef SIMULATOR
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#else
#include <SecureHttpClient.h>
#endif

#include <cctype>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "ObsidianPendingQueue.h"
#include "ObsidianSyncStore.h"

ObsidianSyncClient::Error ObsidianSyncClient::_lastError = ObsidianSyncClient::OK;
int ObsidianSyncClient::_lastHttpCode = 0;

namespace {

// Same floors as KOReaderSyncClient: a TLS handshake needs working heap, and
// failing fast with a clear error beats an obscure crash or hang.
constexpr uint32_t MIN_FREE_HEAP_FOR_TLS = 35000;
constexpr uint32_t MIN_MAX_ALLOC_HEAP_FOR_TLS = 20000;

bool insufficientHeap() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_TLS || maxAllocHeap < MIN_MAX_ALLOC_HEAP_FOR_TLS) {
    LOG_ERR("OBS", "Insufficient heap for TLS handshake: %u bytes free (need %u), %u max alloc (need %u)", freeHeap,
            MIN_FREE_HEAP_FOR_TLS, maxAllocHeap, MIN_MAX_ALLOC_HEAP_FOR_TLS);
    return true;
  }
  return false;
}

using HeaderList = std::vector<std::pair<std::string, std::string>>;

// One request/response transaction. Returns the HTTP status code, or a
// negative value on transport failure (mirrors SecureHttpClient/HTTPClient).
// Only POST and PUT are needed here (the two methods ObsidianSyncClient
// issues); the simulator's HTTPClient stub has no generic sendRequest().
#ifdef SIMULATOR
int sendHttp(const std::string& url, const char* method, const std::string& body, const HeaderList& headers,
            bool insecureTls) {
  HTTPClient http;
  std::unique_ptr<WiFiClientSecure> secureClient;
  WiFiClient plainClient;

  if (url.rfind("https://", 0) == 0) {
    secureClient.reset(new WiFiClientSecure);
    if (insecureTls) secureClient->setInsecure();
    http.begin(*secureClient, url.c_str());
  } else {
    http.begin(plainClient, url.c_str());
  }
  for (const auto& h : headers) http.addHeader(h.first.c_str(), h.second.c_str());

  const bool isPut = strcmp(method, "PUT") == 0;
  const int code = isPut ? http.PUT(body.c_str()) : http.POST(body.c_str());
  http.end();
  return code;
}
#else
int sendHttp(const std::string& url, const char* method, const std::string& body, const HeaderList& headers,
            bool insecureTls) {
  freeink::SecureHttpClient http;
  if (insecureTls) http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("OBS", "Bad URL: %s", url.c_str());
    return -1;
  }
  for (const auto& h : headers) http.addHeader(h.first, h.second);
  const int code = http.sendRequest(method, body);
  http.end();
  return code;
}
#endif

ObsidianSyncClient::Error classifyHttpCode(int code, bool notFoundIsError = true) {
  if (code <= 0) return ObsidianSyncClient::NETWORK_ERROR;
  if (code == 200 || code == 201 || code == 204) return ObsidianSyncClient::OK;
  if (code == 401 || code == 403) return ObsidianSyncClient::AUTH_FAILED;
  if (code == 404 && !notFoundIsError) return ObsidianSyncClient::OK;
  return ObsidianSyncClient::SERVER_ERROR;
}

// Escapes characters that are unsafe inside an HTTP path segment. Keeps '/'
// unescaped so the caller can build a multi-segment vault path in one pass.
std::string urlEncodePath(const std::string& path) {
  // Named hexDigits, not HEX: Arduino's Print.h #defines HEX to 16.
  static constexpr char hexDigits[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(path.size() * 3);
  for (const unsigned char ch : path) {
    const bool safe =
        std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == '/';
    if (safe) {
      out += static_cast<char>(ch);
    } else {
      out += '%';
      out += hexDigits[(ch >> 4) & 0xF];
      out += hexDigits[ch & 0xF];
    }
  }
  return out;
}

// Vault note paths can't contain these characters on any common filesystem.
std::string sanitizeForPath(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (const char ch : raw) {
    out += (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' ||
            ch == '|')
               ? '-'
               : ch;
  }
  return out.empty() ? "Untitled" : out;
}

std::string resolveNotePath(const std::string& tmpl, const ObsidianPendingClipping& clipping) {
  std::string path = tmpl.empty() ? "Clippings/{book}.md" : tmpl;
  const std::string book = sanitizeForPath(clipping.book.empty() ? "Untitled" : clipping.book);
  size_t pos;
  while ((pos = path.find("{book}")) != std::string::npos) {
    path.replace(pos, 6, book);
  }
  return path;
}

std::string buildMarkdownBlock(const ObsidianPendingClipping& c) {
  std::string block = "\n> [!quote] ";
  if (!c.chapter.empty()) {
    block += c.chapter;
    block += " · ";  // middle dot
  }
  block += "p. " + std::to_string(c.page);
  block += "\n> " + c.text + "\n\n";
  return block;
}

// Tries `attempt` against cfg.baseUrl, then, only on a transport-level
// failure (code <= 0; DNS/TCP/TLS never even reached a server, so a
// different address might succeed where an auth/server error would not),
// against cfg.fallbackBaseUrl when one is configured. Returns whichever
// attempt's code should be reported.
template <typename Attempt>
int withFallback(const ObsidianSyncConfig& cfg, Attempt attempt) {
  const int code = attempt(cfg.baseUrl);
  if (code > 0 || cfg.fallbackBaseUrl.empty()) return code;
  LOG_DBG("OBS", "Primary Obsidian target unreachable, trying fallback address");
  return attempt(cfg.fallbackBaseUrl);
}

// Returns the raw HTTP status code (or a negative transport-failure code);
// the caller (a member function) owns writing it to _lastHttpCode/_lastError.
int pushLocalRestApi(const ObsidianSyncConfig& cfg, const ObsidianPendingClipping& clipping) {
  const std::string notePath = urlEncodePath(resolveNotePath(cfg.notePathTemplate, clipping));
  const std::string body = buildMarkdownBlock(clipping);
  HeaderList headers = {{"Content-Type", "text/markdown; charset=utf-8"}};
  if (!cfg.apiKey.empty()) headers.push_back({"Authorization", "Bearer " + cfg.apiKey});

  return withFallback(cfg, [&](const std::string& base) {
    const std::string url = base + "/vault/" + notePath;
    // POST appends to an existing note (per the Local REST API plugin's
    // documented behavior at the time this was written; verify against your
    // installed plugin version). A 404 means the note doesn't exist yet:
    // create it once with PUT, then this and future clippings append normally.
    int code = sendHttp(url, "POST", body, headers, cfg.insecureTls);
    if (code == 404) {
      code = sendHttp(url, "PUT", body, headers, cfg.insecureTls);
    }
    return code;
  });
}

int pushWebhook(const ObsidianSyncConfig& cfg, const ObsidianPendingClipping& clipping) {
  JsonDocument doc;
  doc["source"] = "RetroInk";
  doc["book"] = clipping.book;
  doc["author"] = clipping.author;
  doc["chapter"] = clipping.chapter;
  doc["page"] = clipping.page;
  doc["text"] = clipping.text;
  doc["timestamp"] = clipping.timestamp;
  std::string body;
  serializeJson(doc, body);

  HeaderList headers = {{"Content-Type", "application/json"}};
  if (!cfg.apiKey.empty()) headers.push_back({"Authorization", "Bearer " + cfg.apiKey});

  return withFallback(
      cfg, [&](const std::string& base) { return sendHttp(base, "POST", body, headers, cfg.insecureTls); });
}

// True if `url` is plain http:// while an API key is configured. Sending
// would put the key (and the clipping text) on the wire in cleartext for
// anyone else on the network to read.
bool leaksKeyInCleartext(const std::string& url, const std::string& apiKey) {
  return !apiKey.empty() && url.rfind("http://", 0) == 0;
}

}  // namespace

size_t ObsidianSyncClient::syncPending() {
  _lastError = OK;
  _lastHttpCode = 0;

  const ObsidianSyncConfig& cfg = OBSIDIAN_STORE.getConfig();
  if (!cfg.enabled || cfg.baseUrl.empty()) {
    _lastError = NOT_CONFIGURED;
    return 0;
  }

  if (leaksKeyInCleartext(cfg.baseUrl, cfg.apiKey) || leaksKeyInCleartext(cfg.fallbackBaseUrl, cfg.apiKey)) {
    LOG_ERR("OBS", "Refusing to sync: an API key is set but a target URL is http:// (not https://)");
    _lastError = INSECURE_URL_REFUSED;
    return 0;
  }

  const auto pending = ObsidianPendingQueue::readAll();
  if (pending.empty()) {
    _lastError = NOTHING_PENDING;
    return 0;
  }

  if (insufficientHeap()) {
    _lastError = LOW_MEMORY;
    return 0;
  }

  size_t sent = 0;
  for (const auto& clipping : pending) {
    const int code =
        cfg.mode == ObsidianTargetMode::WEBHOOK ? pushWebhook(cfg, clipping) : pushLocalRestApi(cfg, clipping);
    _lastHttpCode = code;
    const Error result = classifyHttpCode(code);
    if (result != OK) {
      _lastError = result;
      break;
    }
    sent++;
  }

  if (sent > 0 && !ObsidianPendingQueue::removeFirst(sent)) {
    LOG_ERR("OBS", "Delivered %u clippings but failed to drain the queue; they will resend next sync",
            static_cast<unsigned>(sent));
  }

  LOG_DBG("OBS", "Sync delivered %u/%u pending clippings (lastError=%d, lastHttpCode=%d)",
          static_cast<unsigned>(sent), static_cast<unsigned>(pending.size()), static_cast<int>(_lastError),
          _lastHttpCode);

  return sent;
}

std::string ObsidianSyncClient::errorString(const Error error) {
  switch (error) {
    case OK:
      return "Success";
    case NOT_CONFIGURED:
      return "Obsidian sync is not configured";
    case NOTHING_PENDING:
      return "No clippings waiting to sync";
    case NETWORK_ERROR:
      return "Could not reach the sync target";
    case AUTH_FAILED:
      return "Sync target rejected the API key";
    case SERVER_ERROR:
      return _lastHttpCode > 0 ? ("Sync target returned HTTP " + std::to_string(_lastHttpCode))
                                : std::string("Sync target error");
    case LOW_MEMORY:
      return "Not enough free memory for a secure connection right now";
    case INSECURE_URL_REFUSED:
      return "Refused to send: target is http:// with an API key set. Use https:// or remove the key";
    default:
      return "Unknown error";
  }
}
