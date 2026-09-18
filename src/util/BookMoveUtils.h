#pragma once

#include <string>

namespace BookMoveUtils {

std::string buildReadFolderDestination(const std::string& srcPath);
// Moves a supported book (or a folder containing books) and its path-keyed
// reading state. A failed migration leaves a journal for retry on boot.
bool moveWithState(const std::string& oldPath, const std::string& newPath, bool keepInRecents = true);
bool recoverPendingMove();
bool migrateExistingPath(const std::string& oldPath, const std::string& newPath, bool keepInRecents = true);
// Restores an externally moved book only after the user has reviewed the old
// and current records. Existing destination cache/bookmarks/clippings get backups.
bool restorePriorState(const std::string& oldPath, const std::string& currentPath);
bool migrateMovedEpubState(const std::string& oldPath, const std::string& newPath, const std::string& oldCachePath,
                           const std::string& title, const std::string& author, bool keepInRecents);

}  // namespace BookMoveUtils
