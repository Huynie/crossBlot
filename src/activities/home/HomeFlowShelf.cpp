// Crossblot: the Flow theme's collection shelf on Home, ported from CrumBLE
// v4.7.1 HomeActivity. Simplified from the original: the shelf repaints in full
// on every render (no partial-repaint snapshots) and series collapse is off.
//
// Focus model: CrossInk's selectorIndex still owns the carousel row (0..books-1)
// and the icon row (books..). The shelf adds two rows between them, tracked by
// shelfFocus, so the existing carousel/menu index math is untouched.
#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Xtc.h>

#include <algorithm>

#include "CollectionsStore.h"
#include "CoverThumbStatus.h"
#include "CrossPointSettings.h"
#include "HomeActivity.h"
#include "LibraryIndex.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/ActivityManager.h"
#include "activities/home/AddBooksToCollectionActivity.h"
#include "activities/home/BookActions.h"
#include "activities/home/BookshelfPickerActivity.h"
#include "activities/home/CollectionPickerActivity.h"
#include "activities/home/RearrangeCollectionsActivity.h"
#include "activities/home/SortPickerActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/OptionSelectionActivity.h"
#include "components/UITheme.h"
#include "components/themes/lyra/LyraFlowTheme.h"
#include "fontIds.h"

namespace {
// Synthetic cell shown in an empty user collection; Confirm opens "Add books".
constexpr const char* kEmptyCollectionCtaPath = "__EMPTY_CTA__";
constexpr unsigned long kShelfLongPressMs = 1000;
// Vertical centre of the shelf strip, measured up from the screen bottom
// (CrumBLE's tuning: the band between the carousel footer and the icon bar).
constexpr int kShelfMidFromBottom = 242;
constexpr int kShelfRowCount = 1;

std::string trimSpaces(const std::string& s) {
  const auto l = s.find_first_not_of(" \t");
  if (l == std::string::npos) return {};
  const auto r = s.find_last_not_of(" \t");
  return s.substr(l, r - l + 1);
}

std::string displayNameFromPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  std::string name = slash != std::string::npos ? path.substr(slash + 1) : path;
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0) name.resize(dot);
  return name;
}

std::string thumbTemplateFor(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) return Epub(path, "/.crosspoint").getThumbBmpPath();
  if (FsHelpers::hasXtcExtension(path)) return Xtc(path, "/.crosspoint").getThumbBmpPath();
  return {};
}

enum class CollectionOption : uint8_t {
  AddBooks,
  Rename,
  Delete,
  New,
  Sort,
  Rearrange,
  ToggleAllBooks,
  ToggleRecentlyAdded,
  ToggleUnopened,
  ToggleFinished,
  Rescan,
};

struct VirtualToggle {
  uint8_t* setting;
  const char* id;
  const char* name;
};

VirtualToggle virtualToggleFor(const CollectionOption option) {
  switch (option) {
    case CollectionOption::ToggleAllBooks:
      return {&SETTINGS.showAllBooksCollection, CollectionsStore::ALL_BOOKS_ID, CollectionsStore::ALL_BOOKS_NAME};
    case CollectionOption::ToggleRecentlyAdded:
      return {&SETTINGS.showRecentlyAddedCollection, CollectionsStore::RECENTLY_ADDED_ID,
              CollectionsStore::RECENTLY_ADDED_NAME};
    case CollectionOption::ToggleUnopened:
      return {&SETTINGS.showNewCollection, CollectionsStore::NEW_ID, CollectionsStore::NEW_NAME};
    case CollectionOption::ToggleFinished:
      return {&SETTINGS.showFinishedCollection, CollectionsStore::FINISHED_ID, CollectionsStore::FINISHED_NAME};
    default:
      return {nullptr, nullptr, nullptr};
  }
}
}  // namespace

bool HomeActivity::flowThemeActive() {
  return static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme) == CrossPointSettings::UI_THEME::LYRA_FLOW;
}

bool HomeActivity::flowShelfEnabled() const {
  return flowThemeActive() && !CollectionsStore::getInstance().getCollections().empty();
}

void HomeActivity::invalidateShelf() {
  shelfPathsCacheKey.clear();
  shelfCoversLoaded = false;
  focusedMetaPath.clear();
}

const std::vector<ShelfEntry>& HomeActivity::cachedShelfEntries() {
  auto& store = CollectionsStore::getInstance();
  const std::string& activeId = store.getActiveId();
  if (activeId.empty()) {
    shelfEntriesCache.clear();
    shelfPathsCacheKey.clear();
    return shelfEntriesCache;
  }
  if (activeId == shelfPathsCacheKey) return shelfEntriesCache;

  shelfEntriesCache = store.resolveShelfEntries(activeId);
  const bool heapPressure = store.lastResolveHitHeapPressure();
  if (shelfEntriesCache.empty() && !heapPressure) {
    const Collection* active = store.getActiveCollection();
    if (active != nullptr && !active->isVirtual) {
      ShelfEntry cta;
      cta.firstPath = kEmptyCollectionCtaPath;
      shelfEntriesCache.push_back(std::move(cta));
    }
  }
  // A heap-pressure empty result is not cached, so the next render retries.
  if (!heapPressure) shelfPathsCacheKey = activeId;
  return shelfEntriesCache;
}

void HomeActivity::loadShelfCovers(const int cellWidth, const int cellHeight, const int visibleCount) {
  if (shelfCoversLoaded) return;
  shelfCoversLoaded = true;

  const auto& entries = cachedShelfEntries();
  const int total = static_cast<int>(entries.size());
  const int start = std::clamp(shelfScrollOffset, 0, total);
  const int end = std::min(start + visibleCount, total);

  bool showingLoading = false;
  Rect popupRect;
  for (int i = start; i < end; ++i) {
    const std::string& bookPath = entries[i].firstPath;
    if (bookPath == kEmptyCollectionCtaPath || !Storage.exists(bookPath.c_str())) continue;
    if (std::find(failedShelfCovers.begin(), failedShelfCovers.end(), bookPath) != failedShelfCovers.end()) continue;
    if (CoverThumbStatus::isMarkedFailed(bookPath, cellWidth, cellHeight)) continue;

    const std::string templatePath = thumbTemplateFor(bookPath);
    if (templatePath.empty()) continue;
    const std::string resolved = UITheme::getCoverThumbPath(templatePath, cellWidth, cellHeight);
    if (resolved.empty() || Storage.exists(resolved.c_str())) continue;

    if (!showingLoading) {
      showingLoading = true;
      popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
    }
    GUI.fillPopupProgress(renderer, popupRect, 10 + ((i - start) * 90) / std::max(1, end - start));

    bool generated = false;
    if (FsHelpers::hasEpubExtension(bookPath)) {
      Epub epub(bookPath, "/.crosspoint");
      generated = epub.generateThumbBmpNoIndex(cellWidth, cellHeight);
    } else {
      Xtc xtc(bookPath, "/.crosspoint");
      generated = xtc.load() &&
                  xtc.generateThumbBmp(static_cast<uint16_t>(cellWidth), static_cast<uint16_t>(cellHeight));
    }
    if (!generated || !Storage.exists(resolved.c_str())) {
      failedShelfCovers.push_back(bookPath);
      // Only remember the failure across boots when heap was healthy; a
      // low-heap failure says nothing about the cover itself.
      if (ESP.getFreeHeap() >= 45u * 1024u) CoverThumbStatus::markFailed(bookPath, cellWidth, cellHeight);
      LOG_ERR("HOME", "shelf: thumb generation failed for %s", bookPath.c_str());
    }
  }
  if (showingLoading) requestUpdate();
}

void HomeActivity::updateFocusedShelfMeta(const std::string& path) {
  if (path == focusedMetaPath) return;
  focusedMetaPath = path;
  focusedMetaTitle.clear();
  focusedMetaAuthor.clear();
  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    if (epub.load(/*buildIfMissing=*/false, /*skipLoadingCss=*/true)) {
      focusedMetaTitle = epub.getTitle();
      focusedMetaAuthor = epub.getAuthor();
    } else if (epub.extractSeriesFromOpf()) {
      focusedMetaAuthor = epub.getLastAuthorPeek();
    }
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      focusedMetaTitle = xtc.getTitle();
      focusedMetaAuthor = xtc.getAuthor();
    }
  }
  if (focusedMetaTitle.empty()) focusedMetaTitle = displayNameFromPath(path);
}

void HomeActivity::renderFlowShelf(const int pageWidth, const int pageHeight) {
  auto& flowTheme = const_cast<LyraFlowTheme&>(static_cast<const LyraFlowTheme&>(GUI));
  flowTheme.focusedBookAuthorForLabel.clear();

  auto& store = CollectionsStore::getInstance();
  const Collection* active = store.getActiveCollection();
  if (active == nullptr) return;

  const auto layout = LyraFlowTheme::shelfLayoutFor(kShelfRowCount);
  const int visibleCells = layout.cellsPerRow * layout.rowCount;
  const auto& entries = cachedShelfEntries();
  const int total = static_cast<int>(entries.size());
  shelfBookIndex = std::clamp(shelfBookIndex, 0, std::max(0, total - 1));
  shelfScrollOffset = std::clamp(shelfScrollOffset, 0, std::max(0, total - visibleCells));
  if (shelfFocus == ShelfFocus::Books && total == 0) shelfFocus = ShelfFocus::Header;

  loadShelfCovers(layout.cellWidth, layout.cellHeight, visibleCells);

  // Cover paths and placeholder titles for the visible window only; cells
  // outside it are never drawn, so their slots stay empty.
  std::vector<std::string> coverPaths(total);
  std::vector<std::string> cellTitles(total);
  const int winEnd = std::min(shelfScrollOffset + visibleCells, total);
  for (int i = shelfScrollOffset; i < winEnd; ++i) {
    const std::string& path = entries[i].firstPath;
    if (path == kEmptyCollectionCtaPath) {
      cellTitles[i] = tr(STR_EMPTY_COLLECTION_ADD);
      continue;
    }
    const std::string templatePath = thumbTemplateFor(path);
    if (!templatePath.empty()) {
      std::string resolved = UITheme::getCoverThumbPath(templatePath, layout.cellWidth, layout.cellHeight);
      if (!resolved.empty() && Storage.exists(resolved.c_str())) coverPaths[i] = std::move(resolved);
    }
    cellTitles[i] = displayNameFromPath(path);
  }

  const int focusedCell = shelfFocus == ShelfFocus::Books ? shelfBookIndex : -1;
  const char* focusedTitle = nullptr;
  const char* focusedAuthor = nullptr;
  if (focusedCell >= 0 && focusedCell < total && entries[focusedCell].firstPath != kEmptyCollectionCtaPath) {
    updateFocusedShelfMeta(entries[focusedCell].firstPath);
    focusedTitle = focusedMetaTitle.c_str();
    if (!focusedMetaAuthor.empty()) focusedAuthor = focusedMetaAuthor.c_str();
  }

  const int stripY = pageHeight - kShelfMidFromBottom - layout.stripHeight / 2;
  const Rect shelfRect{0, stripY, pageWidth, layout.stripHeight};
  flowTheme.drawBookshelfStrip(renderer, shelfRect, active->name.c_str(), coverPaths, focusedCell, shelfScrollOffset,
                               shelfFocus == ShelfFocus::Header, store.getCollections().size() > 1, focusedTitle,
                               nullptr, focusedAuthor, kShelfRowCount, &cellTitles);

  if (total == 0) {
    // Empty virtual collection: a static note (there is nothing to add to).
    const char* msg = tr(STR_EMPTY_COLLECTION_VIRTUAL);
    const int textW = renderer.getTextWidth(UI_10_FONT_ID, msg, EpdFontFamily::REGULAR);
    const int textY = shelfRect.y + (shelfRect.height * 3) / 5 - renderer.getLineHeight(UI_10_FONT_ID) / 2;
    renderer.drawText(UI_10_FONT_ID, (pageWidth - textW) / 2, textY, msg, true, EpdFontFamily::REGULAR);
  }

  // The focused book's author goes in the icon bar's label slot.
  if (focusedAuthor != nullptr) flowTheme.focusedBookAuthorForLabel = focusedAuthor;
}

void HomeActivity::ensureShelfFocusVisible() {
  const auto layout = LyraFlowTheme::shelfLayoutFor(kShelfRowCount);
  const int visibleCells = layout.cellsPerRow * layout.rowCount;
  const int before = shelfScrollOffset;
  if (shelfBookIndex < shelfScrollOffset) shelfScrollOffset = shelfBookIndex;
  if (shelfBookIndex >= shelfScrollOffset + visibleCells) shelfScrollOffset = shelfBookIndex - visibleCells + 1;
  if (shelfScrollOffset != before) shelfCoversLoaded = false;
}

void HomeActivity::cycleActiveCollection(const int delta) {
  auto& store = CollectionsStore::getInstance();
  const auto& collections = store.getCollections();
  const int count = static_cast<int>(collections.size());
  if (count <= 1) return;
  int current = 0;
  for (int i = 0; i < count; ++i) {
    if (collections[i].id == store.getActiveId()) current = i;
  }
  store.setActiveId(collections[(current + delta + count) % count].id);
  shelfBookIndex = 0;
  shelfScrollOffset = 0;
  invalidateShelf();
  requestUpdate();
}

void HomeActivity::leaveShelfToCarousel(const int bookCount) {
  shelfFocus = ShelfFocus::None;
  selectorIndex = bookCount > 0 ? std::clamp(lastCarouselBookIndex, 0, bookCount - 1) : 0;
  invalidateCoverCache();
  requestUpdate();
}

void HomeActivity::leaveShelfToMenu(const int bookCount, const int menuItemCount) {
  shelfFocus = ShelfFocus::None;
  selectorIndex = bookCount + std::clamp(lastFlowMenuIndex, 0, std::max(0, menuItemCount - 1));
  requestUpdate();
}

bool HomeActivity::handleFlowInput(const int bookCount, const int menuItemCount) {
  // A recognised long-press runs once Confirm is released.
  if (pendingFlowLongPress != FlowLongPress::None) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      const FlowLongPress action = pendingFlowLongPress;
      pendingFlowLongPress = FlowLongPress::None;
      runFlowLongPress(action);
    }
    return true;
  }
  const bool confirmLongPress = mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
                                mappedInput.getHeldTime() >= kShelfLongPressMs;

  if (shelfFocus == ShelfFocus::None) {
    const bool inCarouselRow = selectorIndex < bookCount;
    if (confirmLongPress) {
      if (inCarouselRow && selectorIndex < static_cast<int>(recentBooks.size())) {
        pendingFlowLongPress = FlowLongPress::CarouselBook;
        return true;
      }
      if (!inCarouselRow && selectorIndex - bookCount == kFlowBookshelfMenuIndex) {
        pendingFlowLongPress = FlowLongPress::BookshelfIcon;
        return true;
      }
      return false;
    }
    if (!flowShelfEnabled()) return false;
    if (inCarouselRow && mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      lastCarouselBookIndex = selectorIndex;
      shelfFocus = ShelfFocus::Header;
      invalidateCoverCache();
      requestUpdate();
      return true;
    }
    if (!inCarouselRow && mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      lastFlowMenuIndex = selectorIndex - bookCount;
      const auto& entries = cachedShelfEntries();
      shelfFocus = entries.empty() ? ShelfFocus::Header : ShelfFocus::Books;
      invalidateCoverCache();
      requestUpdate();
      return true;
    }
    return false;
  }

  const auto& entries = cachedShelfEntries();
  const int total = static_cast<int>(entries.size());

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    leaveShelfToCarousel(bookCount);
    return true;
  }

  if (shelfFocus == ShelfFocus::Header) {
    if (confirmLongPress) {
      pendingFlowLongPress = FlowLongPress::Header;
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      cycleActiveCollection(-1);
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      cycleActiveCollection(+1);
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      leaveShelfToCarousel(bookCount);
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (total > 0) {
        shelfFocus = ShelfFocus::Books;
        requestUpdate();
      } else {
        leaveShelfToMenu(bookCount, menuItemCount);
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      onBookshelfOpen();
    }
    return true;
  }

  // ShelfFocus::Books
  if (confirmLongPress) {
    pendingFlowLongPress = FlowLongPress::ShelfBook;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (shelfBookIndex > 0) {
      --shelfBookIndex;
      ensureShelfFocusVisible();
      requestUpdate();
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (shelfBookIndex < total - 1) {
      ++shelfBookIndex;
      ensureShelfFocusVisible();
      requestUpdate();
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    shelfFocus = ShelfFocus::Header;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    leaveShelfToMenu(bookCount, menuItemCount);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openShelfEntry();
  }
  return true;
}

void HomeActivity::runFlowLongPress(const FlowLongPress action) {
  switch (action) {
    case FlowLongPress::CarouselBook:
      if (selectorIndex >= 0 && selectorIndex < static_cast<int>(recentBooks.size())) {
        showFlowBookActions(recentBooks[selectorIndex].path, recentBooks[selectorIndex].title);
      }
      return;
    case FlowLongPress::BookshelfIcon:
      showBookshelfCollectionPicker();
      return;
    case FlowLongPress::Header:
      showCollectionOptions(0);
      return;
    case FlowLongPress::ShelfBook: {
      const auto& entries = cachedShelfEntries();
      if (shelfBookIndex < 0 || shelfBookIndex >= static_cast<int>(entries.size())) return;
      const std::string path = entries[shelfBookIndex].firstPath;
      if (path == kEmptyCollectionCtaPath) {
        launchAddBooksToActiveCollection();
      } else {
        updateFocusedShelfMeta(path);
        showFlowBookActions(path, focusedMetaTitle);
      }
      return;
    }
    case FlowLongPress::None:
      return;
  }
}

void HomeActivity::openShelfEntry() {
  const auto& entries = cachedShelfEntries();
  if (shelfBookIndex < 0 || shelfBookIndex >= static_cast<int>(entries.size())) return;
  const std::string path = entries[shelfBookIndex].firstPath;
  if (path == kEmptyCollectionCtaPath) {
    launchAddBooksToActiveCollection();
    return;
  }
  if (!path.empty()) onSelectBook(path);
}

void HomeActivity::onBookshelfOpen() { activityManager.goToBookshelf(CollectionsStore::getInstance().getActiveId()); }

void HomeActivity::showBookshelfCollectionPicker() {
  auto& store = CollectionsStore::getInstance();
  const auto& collections = store.getCollections();
  if (collections.size() <= 1) {
    onBookshelfOpen();
    return;
  }
  std::vector<std::string> labels;
  std::vector<std::string> ids;
  labels.reserve(collections.size());
  ids.reserve(collections.size());
  int currentIndex = -1;
  for (size_t i = 0; i < collections.size(); ++i) {
    labels.push_back(collections[i].name);
    ids.push_back(collections[i].id);
    if (collections[i].id == store.getActiveId()) currentIndex = static_cast<int>(i);
  }
  startActivityForResult(
      std::make_unique<BookshelfPickerActivity>(renderer, mappedInput, std::move(labels), currentIndex),
      [this, ids = std::move(ids)](const ActivityResult& res) {
        const auto* picked = res.isCancelled ? nullptr : std::get_if<ChoicePromptResult>(&res.data);
        if (picked == nullptr || picked->choice < 0 || picked->choice >= static_cast<int>(ids.size())) {
          requestUpdate();
          return;
        }
        CollectionsStore::getInstance().setActiveId(ids[picked->choice]);
        activityManager.goToBookshelf(ids[picked->choice]);
      });
}

void HomeActivity::launchAddBooksToActiveCollection() {
  const Collection* active = CollectionsStore::getInstance().getActiveCollection();
  if (active == nullptr || active->isVirtual) return;
  startActivityForResult(std::make_unique<AddBooksToCollectionActivity>(renderer, mappedInput, active->id, active->name),
                         [this](const ActivityResult&) {
                           invalidateShelf();
                           requestUpdate();
                         });
}

void HomeActivity::showFlowBookActions(const std::string& path, const std::string& title) {
  // Collections first, then the generic book actions Home can carry out.
  std::vector<FileBrowserActionActivity::MenuItem> items{{FileBrowserAction::AddToCollection,
                                                           StrId::STR_ADD_TO_COLLECTION}};
  for (const auto& item : BookActions::buildBookActionItems(path, /*includeRemoveFromRecents=*/false)) {
    if (item.action == FileBrowserAction::ToggleCompleted || item.action == FileBrowserAction::DeleteCache) {
      items.push_back(item);
    }
  }
  startActivityForResult(
      std::make_unique<FileBrowserActionActivity>(renderer, mappedInput, title, std::move(items)),
      [this, path, title](const ActivityResult& result) {
        const auto* action = result.isCancelled ? nullptr : std::get_if<FileBrowserActionResult>(&result.data);
        if (action == nullptr) {
          requestUpdate();
          return;
        }
        switch (static_cast<FileBrowserAction>(action->action)) {
          case FileBrowserAction::AddToCollection:
            startActivityForResult(std::make_unique<CollectionPickerActivity>(renderer, mappedInput, path, title),
                                   [this](const ActivityResult&) {
                                     invalidateShelf();
                                     requestUpdate();
                                   });
            return;
          case FileBrowserAction::ToggleCompleted: {
            bool completed = false;
            if (BookActions::toggleBookCompleted(path, title, completed)) {
              BookActions::drawToast(renderer, completed ? tr(STR_MARKED_FINISHED) : tr(STR_MARKED_UNFINISHED));
              delay(1000);
            }
            CollectionsStore::getInstance().invalidateScannedVirtuals();
            invalidateShelf();
            break;
          }
          case FileBrowserAction::DeleteCache:
            BookActions::drawToast(renderer, BookActions::clearBookCache(path) ? tr(STR_BOOK_CACHE_DELETED)
                                                                              : tr(STR_CACHE_DELETE_FAILED));
            delay(1000);
            invalidateShelf();
            break;
          default:
            break;
        }
        requestUpdate();
      });
}

void HomeActivity::showCollectionOptions(const int initialIndex) {
  auto& store = CollectionsStore::getInstance();
  const Collection* active = store.getActiveCollection();
  const bool isUserCollection = active != nullptr && !active->isVirtual;
  const bool isFavorites = active != nullptr && active->id == CollectionsStore::FAVORITES_ID;
  const bool isRecentlyAdded = active != nullptr && active->id == CollectionsStore::RECENTLY_ADDED_ID;

  std::vector<CollectionOption> actions;
  std::vector<std::string> labels;
  auto add = [&](const CollectionOption action, std::string label) {
    actions.push_back(action);
    labels.push_back(std::move(label));
  };
  auto shownLabel = [](const StrId name, const uint8_t on) {
    return std::string(I18N.get(name)) + ": " + I18N.get(on ? StrId::STR_COL_SHOWN : StrId::STR_COL_HIDDEN);
  };

  if (isUserCollection) {
    add(CollectionOption::AddBooks, tr(STR_ADD_BOOKS_TO_COLLECTION));
    add(CollectionOption::Rename, tr(STR_RENAME_COLLECTION));
    if (!isFavorites) add(CollectionOption::Delete, tr(STR_DELETE_COLLECTION));
  }
  add(CollectionOption::New, tr(STR_HEADER_NEW_COLLECTION));
  if (active != nullptr && !isRecentlyAdded) {
    std::string label = tr(STR_SORT_BY);
    if (active->sortMode != CollectionSort::Manual) {
      label += " (";
      label += SortPickerActivity::labelFor(active->sortMode);
      label += ")";
    }
    add(CollectionOption::Sort, std::move(label));
  }
  if (store.getCollections().size() > 1) add(CollectionOption::Rearrange, tr(STR_REARRANGE));
  add(CollectionOption::ToggleAllBooks, shownLabel(StrId::STR_COL_ALL_BOOKS, SETTINGS.showAllBooksCollection));
  add(CollectionOption::ToggleRecentlyAdded,
      shownLabel(StrId::STR_COL_RECENTLY_ADDED, SETTINGS.showRecentlyAddedCollection));
  add(CollectionOption::ToggleUnopened, shownLabel(StrId::STR_COL_UNOPENED, SETTINGS.showNewCollection));
  add(CollectionOption::ToggleFinished, shownLabel(StrId::STR_COL_FINISHED, SETTINGS.showFinishedCollection));
  add(CollectionOption::Rescan, tr(STR_RESCAN_LIBRARY));

  const auto startIndex = static_cast<uint8_t>(std::clamp(initialIndex, 0, static_cast<int>(labels.size()) - 1));
  startActivityForResult(
      std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "CollectionOptions",
                                                StrId::STR_COLLECTION_OPTIONS, std::move(labels), startIndex,
                                                /*readerMode=*/false, /*showTouchHeaderBackButton=*/false,
                                                /*markCurrentOption=*/false),
      [this, actions = std::move(actions)](const ActivityResult& result) {
        const auto* picked = result.isCancelled ? nullptr : std::get_if<OptionSelectionResult>(&result.data);
        if (picked == nullptr || picked->index >= actions.size()) {
          requestUpdate();
          return;
        }
        applyCollectionOption(static_cast<uint8_t>(actions[picked->index]), picked->index);
      });
}

void HomeActivity::applyCollectionOption(const uint8_t optionValue, const int menuIndex) {
  const auto option = static_cast<CollectionOption>(optionValue);
  auto& store = CollectionsStore::getInstance();
  const Collection* active = store.getActiveCollection();
  const std::string activeId = active != nullptr ? active->id : std::string();
  const std::string activeName = active != nullptr ? active->name : std::string();

  auto refreshShelf = [this](const bool resetPosition) {
    if (resetPosition) {
      shelfBookIndex = 0;
      shelfScrollOffset = 0;
    }
    invalidateShelf();
    requestUpdate();
  };

  switch (option) {
    case CollectionOption::AddBooks:
      launchAddBooksToActiveCollection();
      return;

    case CollectionOption::Rename:
      startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput,
                                                                     tr(STR_RENAME_COLLECTION_PROMPT), activeName, 40),
                             [this, activeId, refreshShelf](const ActivityResult& res) {
                               const auto* kr = res.isCancelled ? nullptr : std::get_if<KeyboardResult>(&res.data);
                               const std::string name = kr != nullptr ? trimSpaces(kr->text) : std::string();
                               if (!name.empty() && CollectionsStore::getInstance().renameCollection(activeId, name)) {
                                 BookActions::drawToast(renderer, tr(STR_COLLECTION_RENAMED));
                                 delay(800);
                               }
                               refreshShelf(false);
                             });
      return;

    case CollectionOption::Delete:
      startActivityForResult(
          std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_COLLECTION_PROMPT), activeName),
          [this, activeId, refreshShelf](const ActivityResult& res) {
            if (!res.isCancelled && CollectionsStore::getInstance().deleteCollection(activeId)) {
              BookActions::drawToast(renderer, tr(STR_COLLECTION_DELETED));
              delay(800);
            }
            refreshShelf(true);
          });
      return;

    case CollectionOption::New:
      startActivityForResult(
          std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_NEW_COLLECTION_PROMPT), "", 40),
          [this, refreshShelf](const ActivityResult& res) {
            const auto* kr = res.isCancelled ? nullptr : std::get_if<KeyboardResult>(&res.data);
            const std::string name = kr != nullptr ? trimSpaces(kr->text) : std::string();
            if (!name.empty()) {
              const std::string newId = CollectionsStore::getInstance().createCollection(name);
              if (!newId.empty()) {
                CollectionsStore::getInstance().setActiveId(newId);
                BookActions::drawToast(renderer, tr(STR_COLLECTION_CREATED));
                delay(800);
              }
            }
            refreshShelf(true);
          });
      return;

    case CollectionOption::Sort:
      if (active == nullptr) return;
      startActivityForResult(
          std::make_unique<SortPickerActivity>(renderer, mappedInput, activeName, active->sortMode, !active->isVirtual),
          [this, activeId, refreshShelf](const ActivityResult& res) {
            const auto* sr = res.isCancelled ? nullptr : std::get_if<SortPickerResult>(&res.data);
            if (sr != nullptr) {
              const auto mode = static_cast<CollectionSort>(sr->sortMode);
              auto& library = LibraryIndex::getInstance();
              if ((mode == CollectionSort::AuthorAlpha || mode == CollectionSort::AuthorAlphaDesc) &&
                  library.pendingAuthorKeyCount() > 0) {
                const Rect popupRect = GUI.drawPopup(renderer, tr(STR_READING_METADATA));
                library.populateAuthorKeysWithProgress([&](int pct) { GUI.fillPopupProgress(renderer, popupRect, pct); });
              }
              CollectionsStore::getInstance().setSortMode(activeId, mode);
            }
            refreshShelf(true);
          });
      return;

    case CollectionOption::Rearrange: {
      std::vector<RearrangeCollectionsActivity::Item> snapshot;
      for (const auto& c : store.getCollections()) snapshot.push_back({c.id, c.name});
      startActivityForResult(std::make_unique<RearrangeCollectionsActivity>(renderer, mappedInput, std::move(snapshot)),
                             [this, refreshShelf](const ActivityResult& res) {
                               const auto* rr =
                                   res.isCancelled ? nullptr : std::get_if<RearrangeCollectionsResult>(&res.data);
                               if (rr != nullptr && !rr->orderedIds.empty()) {
                                 CollectionsStore::getInstance().setDisplayOrder(rr->orderedIds);
                                 CollectionsStore::getInstance().setActiveId(rr->orderedIds.front());
                               }
                               refreshShelf(true);
                             });
      return;
    }

    case CollectionOption::ToggleAllBooks:
    case CollectionOption::ToggleRecentlyAdded:
    case CollectionOption::ToggleUnopened:
    case CollectionOption::ToggleFinished: {
      const VirtualToggle toggle = virtualToggleFor(option);
      if (*toggle.setting != 0) {
        *toggle.setting = 0;
        SETTINGS.saveToFile();
        store.setVirtualCollectionVisible(toggle.id, toggle.name, false);
        invalidateShelf();
        showCollectionOptions(menuIndex);
        return;
      }
      auto turnOn = [this, toggle, menuIndex]() {
        *toggle.setting = 1;
        SETTINGS.saveToFile();
        CollectionsStore::getInstance().setVirtualCollectionVisible(toggle.id, toggle.name, true);
        auto& library = LibraryIndex::getInstance();
        if (!library.hasWalked()) {
          const Rect popupRect = GUI.drawPopup(renderer, tr(STR_RESCAN_LIBRARY));
          library.ensureWalked([&](int pct) { GUI.fillPopupProgress(renderer, popupRect, pct); });
        }
        CollectionsStore::getInstance().invalidateScannedVirtuals();
        invalidateShelf();
        showCollectionOptions(menuIndex);
      };
      if (LibraryIndex::getInstance().hasWalked()) {
        turnOn();
      } else {
        // The first virtual collection needs a whole-SD scan; ask first.
        startActivityForResult(
            std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_SCAN_LIBRARY_PROMPT), toggle.name),
            [this, turnOn](const ActivityResult& confirm) {
              if (confirm.isCancelled) {
                requestUpdate();
                return;
              }
              turnOn();
            });
      }
      return;
    }

    case CollectionOption::Rescan: {
      const Rect popupRect = GUI.drawPopup(renderer, tr(STR_RESCAN_LIBRARY));
      LibraryIndex::getInstance().rescan([&](int pct) { GUI.fillPopupProgress(renderer, popupRect, pct); });
      store.invalidateScannedVirtuals();
      BookActions::drawToast(renderer, tr(STR_LIBRARY_RESCANNED));
      delay(800);
      refreshShelf(false);
      return;
    }
  }
}
