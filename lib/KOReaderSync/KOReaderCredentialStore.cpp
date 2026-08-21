#include "KOReaderCredentialStore.h"

#include <Logging.h>
#include <MD5Builder.h>
#include <ObfuscationUtils.h>

#include <utility>

namespace {
// Default sync server URL. crosspoint-sync speaks the full KOSync protocol, so
// pointing at any other kosync server (e.g. https://sync.koreader.rocks:443)
// still works via the custom server URL setting.
constexpr char DEFAULT_SERVER_URL[] = "https://sync.crosspointreader.com";

// Default before config version 2. Configs saved without a version stamp and an
// empty serverUrl were implicitly syncing here — they get pinned on upgrade.
constexpr char LEGACY_DEFAULT_SERVER_URL[] = "https://sync.koreader.rocks:443";

// Bumped when a change to defaults would alter behavior for existing configs.
constexpr uint8_t CONFIG_VERSION = 2;
}  // namespace

void KOReaderCredentialStore::ensureLoaded() const {
  if (loadState.load(std::memory_order_acquire) == 2) return;
  const_cast<KOReaderCredentialStore*>(this)->loadFromFile();
}

bool KOReaderCredentialStore::loadFromFile() {
  uint8_t expected = 0;
  if (loadState.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
    loadSucceeded = PersistableStore<KOReaderCredentialStore>::loadFromFile();
    loadState.store(2, std::memory_order_release);
    return loadSucceeded;
  }
  while (loadState.load(std::memory_order_acquire) == 1) delay(1);
  return loadSucceeded;
}

bool KOReaderCredentialStore::saveToFile() const {
  ensureLoaded();
  return PersistableStore<KOReaderCredentialStore>::saveToFile();
}

void KOReaderCredentialStore::toJson(JsonDocument& doc) const {
  doc["cfgVersion"] = CONFIG_VERSION;
  doc["username"] = username;
  doc["password_obf"] = obfuscation::obfuscateToBase64(password);
  doc["serverUrl"] = serverUrl;
  doc["matchMethod"] = static_cast<uint8_t>(matchMethod);
  doc["sendMetadata"] = sendMetadata;
  doc["syncBehavior"] = static_cast<uint8_t>(syncBehavior);
}

bool KOReaderCredentialStore::fromJson(JsonVariantConst doc) {
  std::string user = doc["username"] | "";

  bool needsResave = false;
  obfuscation::DecodeStatus status = obfuscation::DecodeStatus::INVALID;
  std::string pass = obfuscation::deobfuscateFromBase64(doc["password_obf"] | "", &status);
  if (status == obfuscation::DecodeStatus::LEGACY && !pass.empty()) {
    needsResave = true;
  }
  if (status == obfuscation::DecodeStatus::INVALID || status == obfuscation::DecodeStatus::EMPTY || pass.empty()) {
    pass = doc["password"] | "";
    if (!pass.empty()) needsResave = true;
  }
  if (status == obfuscation::DecodeStatus::INVALID && pass.empty()) {
    LOG_ERR("KRS", "Ignoring unreadable KOReader password");
  }

  username = std::move(user);
  password = std::move(pass);
  serverUrl = doc["serverUrl"] | "";

  // The default server changed in config v2 (sync.koreader.rocks -> crosspoint-sync).
  // A pre-v2 config with credentials and no explicit URL was actively syncing
  // against the old default — pin that URL so the upgrade doesn't switch servers
  // out from under the user. Fresh setups get the new default.
  const uint8_t cfgVersion = doc["cfgVersion"] | (uint8_t)1;
  if (cfgVersion < CONFIG_VERSION) {
    if (serverUrl.empty() && !username.empty() && !password.empty()) {
      LOG_DBG("KRS", "Pre-v2 config used the old default server; pinning %s", LEGACY_DEFAULT_SERVER_URL);
      serverUrl = LEGACY_DEFAULT_SERVER_URL;
    }
    needsResave = true;  // stamp cfgVersion so this migration runs once
  }

  uint8_t method = doc["matchMethod"] | (uint8_t)0;
  if (method <= static_cast<uint8_t>(DocumentMatchMethod::BINARY)) {
    matchMethod = static_cast<DocumentMatchMethod>(method);
  } else {
    LOG_DBG("KRS", "Invalid matchMethod %u in JSON, resetting to FILENAME", method);
    matchMethod = DocumentMatchMethod::FILENAME;
  }
  sendMetadata = doc["sendMetadata"] | false;

  const JsonVariantConst behaviorValue = doc["syncBehavior"];
  const bool missingBehavior = behaviorValue.isNull();
  uint8_t behavior = behaviorValue | static_cast<uint8_t>(KOReaderSyncBehavior::ASK_EVERY_TIME);
  if (behavior <= static_cast<uint8_t>(KOReaderSyncBehavior::SMART)) {
    syncBehavior = static_cast<KOReaderSyncBehavior>(behavior);
    needsResave = needsResave || missingBehavior;
  } else {
    LOG_DBG("KRS", "Invalid syncBehavior %u in JSON, resetting to ASK_EVERY_TIME", behavior);
    syncBehavior = KOReaderSyncBehavior::ASK_EVERY_TIME;
    needsResave = true;
  }

  if (needsResave) {
    LOG_DBG("KRS", "Resaving KOReader credentials to update format");
    requestResave();
  }

  return true;
}

void KOReaderCredentialStore::setCredentials(const std::string& user, const std::string& pass) {
  ensureLoaded();
  username = user;
  password = pass;
}

const std::string& KOReaderCredentialStore::getUsername() const {
  ensureLoaded();
  return username;
}

const std::string& KOReaderCredentialStore::getPassword() const {
  ensureLoaded();
  return password;
}

std::string KOReaderCredentialStore::getMd5Password() const {
  ensureLoaded();
  if (password.empty()) {
    return "";
  }

  // Calculate MD5 hash of password using ESP32's MD5Builder
  MD5Builder md5;
  md5.begin();
  md5.add(password.c_str());
  md5.calculate();

  return md5.toString().c_str();
}

bool KOReaderCredentialStore::hasCredentials() const {
  ensureLoaded();
  return !username.empty() && !password.empty();
}

void KOReaderCredentialStore::clearCredentials() {
  ensureLoaded();
  username.clear();
  password.clear();
  saveToFile();
}

void KOReaderCredentialStore::setServerUrl(const std::string& url) {
  ensureLoaded();
  serverUrl = url;
}

const std::string& KOReaderCredentialStore::getServerUrl() const {
  ensureLoaded();
  return serverUrl;
}

std::string KOReaderCredentialStore::getBaseUrl() const {
  ensureLoaded();
  std::string url;
  if (serverUrl.empty()) {
    url = DEFAULT_SERVER_URL;
  } else if (serverUrl.find("://") == std::string::npos) {
    // Normalize URL: add http:// if no protocol specified (local servers typically don't have SSL)
    url = "http://" + serverUrl;
  } else {
    url = serverUrl;
  }

  // Strip trailing slashes to avoid double-slash in API paths
  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }

  return url;
}

bool KOReaderCredentialStore::usesCrossPointSyncServer() const { return getBaseUrl() == DEFAULT_SERVER_URL; }

void KOReaderCredentialStore::setMatchMethod(DocumentMatchMethod method) {
  ensureLoaded();
  matchMethod = method;
}

DocumentMatchMethod KOReaderCredentialStore::getMatchMethod() const {
  ensureLoaded();
  return matchMethod;
}

void KOReaderCredentialStore::setSendMetadata(bool enabled) {
  ensureLoaded();
  sendMetadata = enabled;
}

bool KOReaderCredentialStore::getSendMetadata() const {
  ensureLoaded();
  return sendMetadata;
}

void KOReaderCredentialStore::setSyncBehavior(KOReaderSyncBehavior behavior) {
  ensureLoaded();
  if (static_cast<uint8_t>(behavior) > static_cast<uint8_t>(KOReaderSyncBehavior::SMART)) {
    behavior = KOReaderSyncBehavior::ASK_EVERY_TIME;
  }
  syncBehavior = behavior;
}

KOReaderSyncBehavior KOReaderCredentialStore::getSyncBehavior() const {
  ensureLoaded();
  return syncBehavior;
}
