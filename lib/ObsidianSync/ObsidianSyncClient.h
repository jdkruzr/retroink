#pragma once

#include <cstddef>
#include <string>

/**
 * Pushes queued clippings (see ObsidianPendingQueue) to the destination
 * configured in ObsidianSyncStore. Two destination modes are supported:
 *
 *  - LOCAL_REST_API: talks directly to the Obsidian "Local REST API"
 *    community plugin running on the same network, writing straight into the
 *    vault. This is the closest thing to "sync" available: there is no
 *    public Obsidian Sync protocol to integrate with.
 *  - WEBHOOK: POSTs a JSON payload to any URL, for routing through n8n,
 *    Make, Home Assistant, or a small self-hosted relay.
 *
 * Modeled on lib/KOReaderSync/KOReaderSyncClient: same SecureHttpClient
 * transport, same heap-gated TLS handshake, same call-and-check-lastError
 * shape. See docs/obsidian-sync.md.
 */
class ObsidianSyncClient {
 public:
  enum Error {
    OK = 0,
    NOT_CONFIGURED,
    NOTHING_PENDING,
    NETWORK_ERROR,
    AUTH_FAILED,
    SERVER_ERROR,
    LOW_MEMORY,
    // Refused to send: the target is http:// (not https://) and an API key
    // is configured, which would put it on the wire in cleartext.
    INSECURE_URL_REFUSED,
  };

  // Sends every pending clipping to the configured destination, in order,
  // stopping at the first failure so nothing already-delivered is lost and
  // nothing unsent is skipped over. Successfully delivered clippings are
  // removed from the queue; the rest remain for the next attempt. Returns
  // the number of clippings delivered.
  static size_t syncPending();

  static Error lastError() { return _lastError; }
  static int lastHttpCode() { return _lastHttpCode; }
  static std::string errorString(Error error);

 private:
  static Error _lastError;
  static int _lastHttpCode;
};
