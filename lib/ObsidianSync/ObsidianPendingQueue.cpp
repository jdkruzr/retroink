#include "ObsidianPendingQueue.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

namespace ObsidianPendingQueue {

namespace {

void serializeLine(const ObsidianPendingClipping& c, std::string& outLine) {
  JsonDocument doc;
  doc["book"] = c.book;
  doc["author"] = c.author;
  doc["chapter"] = c.chapter;
  doc["page"] = c.page;
  doc["text"] = c.text;
  doc["ts"] = c.timestamp;
  outLine.clear();
  serializeJson(doc, outLine);
}

bool parseLine(const std::string& line, ObsidianPendingClipping& out) {
  if (line.empty()) return false;
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, line);
  if (err) {
    LOG_ERR("OBS", "Skipping unparsable pending clipping line: %s", err.c_str());
    return false;
  }
  out.book = doc["book"] | "";
  out.author = doc["author"] | "";
  out.chapter = doc["chapter"] | "";
  out.page = doc["page"] | 0;
  out.text = doc["text"] | "";
  out.timestamp = doc["ts"] | 0u;
  return true;
}

constexpr const char* TMP_PATH = "/.crosspoint/obsidian-pending.tmp";

}  // namespace

bool append(const ObsidianPendingClipping& clipping) {
  std::string line;
  serializeLine(clipping, line);
  line += '\n';

  FsFile file = Storage.open(PENDING_PATH, O_RDWR | O_CREAT | O_AT_END);
  if (!file) {
    LOG_ERR("OBS", "Failed to open %s for append", PENDING_PATH);
    return false;
  }

  if (file.size() >= MAX_PENDING_BYTES) {
    file.close();
    LOG_DBG("OBS", "Pending Obsidian queue at cap (%u bytes); dropping until next sync",
            static_cast<unsigned>(MAX_PENDING_BYTES));
    return false;
  }

  const bool ok = file.write(line.data(), line.size()) == line.size();
  file.flush();
  file.close();
  if (!ok) {
    LOG_ERR("OBS", "Failed to write pending clipping to %s", PENDING_PATH);
  }
  return ok;
}

std::vector<ObsidianPendingClipping> readAll() {
  std::vector<ObsidianPendingClipping> out;
  if (!Storage.exists(PENDING_PATH)) return out;

  const String content = Storage.readFile(PENDING_PATH);
  int start = 0;
  const int len = content.length();
  while (start < len) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = len;
    if (nl > start) {
      ObsidianPendingClipping clipping;
      if (parseLine(std::string(content.c_str() + start, static_cast<size_t>(nl - start)), clipping)) {
        out.push_back(std::move(clipping));
      }
    }
    start = nl + 1;
  }
  return out;
}

bool hasPending() { return count() > 0; }

size_t count() { return readAll().size(); }

bool removeFirst(size_t sentCount) {
  if (sentCount == 0) return true;

  auto pending = readAll();
  if (sentCount >= pending.size()) {
    Storage.remove(PENDING_PATH);
    return !Storage.exists(PENDING_PATH);
  }

  std::string rebuilt;
  for (size_t i = sentCount; i < pending.size(); i++) {
    std::string line;
    serializeLine(pending[i], line);
    rebuilt += line;
    rebuilt += '\n';
  }

  if (!Storage.writeFile(TMP_PATH, rebuilt.c_str())) {
    LOG_ERR("OBS", "Failed to write %s while draining the pending queue", TMP_PATH);
    return false;
  }
  Storage.remove(PENDING_PATH);
  if (!Storage.rename(TMP_PATH, PENDING_PATH)) {
    LOG_ERR("OBS", "Failed to rename %s -> %s", TMP_PATH, PENDING_PATH);
    return false;
  }
  return true;
}

}  // namespace ObsidianPendingQueue
