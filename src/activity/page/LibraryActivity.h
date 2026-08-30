#pragma once

/**
 * @file LibraryActivity.h
 * @brief Public interface and types for LibraryActivity.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../Activity.h"
#include "../Menu.h"
#include "state/BookTags.h"
#include "state/RecentBooks.h"
#include "system/UiTheme.h"

/**
 * @brief Forward declaration of temporary book entry structure
 */
struct TempBookEntry;

/**
 * @brief Activity for browsing and managing the book library
 *
 * Supports folder navigation and book list views with sorting options.
 * Provides pagination for efficient handling of large libraries.
 */
class LibraryActivity final : public Activity, public Menu {
 public:
  /**
   * @brief Represents an item in the library (folder or book)
   */
  struct LibraryItem {
    enum class Type { FOLDER, BOOK };

    Type type;                ///< Type of the item
    std::string name;         ///< Raw name of the item
    std::string displayName;  ///< Formatted display name
    std::string path;         ///< Full filesystem path
    std::string folderPath;   ///< Parent folder path (for books)
  };

  /**
   * @brief Display mode for the library
   */
  enum class ViewMode {
    FOLDER_VIEW,
    BOOK_LIST_VIEW,
    TAG_VIEW,
    SHELF_VIEW
  };

  /**
   * @brief Sorting modes for library items
   */
  enum class SortMode {
    TITLE_AZ,
    TITLE_ZA,
    GROUP_AZ,
    GROUP_ZA,
    READING_AZ,
    READING_ZA,
    TAG_AZ,
    TAG_ZA
  };

  static constexpr int LIST_ITEM_HEIGHT = UiTheme::DRAWER_LIST_ITEM_HEIGHT;
  static constexpr int FOLDER_ICON_WIDTH = 16;
  static constexpr int FOLDER_ICON_SPACING = 20;
  static constexpr int BOOK_ITEMS_PER_PAGE = 9;
  static constexpr int FOLDER_ITEMS_PER_PAGE = 9;
  static constexpr int GRID_ITEMS_PER_PAGE = 12;
  static constexpr int SHELF_ITEMS_PER_PAGE = 9;
  static constexpr int GRID_ICON_SIZE = 148;

  explicit LibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                           const std::function<void()>& onGoToRecent,
                           const std::function<void(const std::string& path)>& onSelectBook,
                           const std::function<void()>& onRecentOpen, const std::function<void()>& onSettingsOpen,
                           const std::string& initialPath = "/");
  ~LibraryActivity() override;

  static void getShelfCoverSize(GfxRenderer& renderer, int& outCoverW, int& outCoverH);

  void onEnter() override;
  void onExit() override;
  void loop() override;

  /**
   * Hit-tests exactly the controls rendered by LibraryActivity. A completed tap
   * selects and activates the tapped semantic control directly; no touch point
   * is translated into a synthetic directional button.
   */
  bool handleTouchTap(const int x, const int y) override {
    if (isInitialLoading_ || isIndexing_ || letterPickerVisible_) {
      return false;
    }

    const int screenW = renderer.getScreenWidth();
    const int screenH = renderer.getScreenHeight();
    if (x < 0 || x >= screenW || y < 0 || y >= screenH) {
      return false;
    }

    const int itemCount = static_cast<int>(currentPageItems.size());

    // Classic button hints are real rounded rectangles at these exact positions
    // (UiRender::buttonHints). Make the visible labels tappable too.
    if (!INX_THEME.mainTabsAtBottom() && y >= screenH - 40) {
      std::string back;
      if (currentViewMode == ViewMode::TAG_VIEW) {
        back = selectedTagKey_.empty() ? (SETTINGS.libraryShelfEnabled ? "Shelf »" : "Groups »") : "« Tags";
      } else if (currentViewMode == ViewMode::BOOK_LIST_VIEW) {
        back = SETTINGS.useLibraryIndex ? "Tags »" : (SETTINGS.libraryShelfEnabled ? "Shelf »" : "Groups »");
      } else if (currentViewMode == ViewMode::SHELF_VIEW) {
        back = "« Groups";
      } else {
        back = basepath != "/" ? "« Back" : "Books »";
      }
      const std::string select = "Select";
      const auto labels = mappedInput.mapLabels(back.c_str(), select.c_str(), "", "");
      const char* slots[] = {labels.btn1, labels.btn2, labels.btn3, labels.btn4};
      constexpr int positions[] = {25, 130, 245, 350};
      constexpr int buttonWidth = 106;
      for (int slot = 0; slot < 4; ++slot) {
        if (x < positions[slot] || x >= positions[slot] + buttonWidth || !slots[slot] || slots[slot][0] == '\0') {
          continue;
        }
        const std::string label = slots[slot];
        if (label == back) {
          handleBackNavigation();
          return true;
        }
        if (label == select) {
          handleConfirmAction(itemCount);
          return true;
        }
        return false;
      }
    }

    const int headerY = mainContentTop();
    const int headerH = mainHeaderHeight();
    if (y >= headerY && y < headerY + headerH) {
      constexpr int sortWidth = 110;
      constexpr int indexWidth = 64;
      const int rightX = screenW - 1;
      const int sortX = rightX - sortWidth;
      if (x >= sortX) {
        cycleSortMode();
        return true;
      }
      if (shouldShowIndexButton() && x >= sortX - indexWidth) {
        startLibraryIndexing();
        return true;
      }
      toggleViewMode();
      return true;
    }

    if (itemCount <= 0) {
      return false;
    }

    const int dividerY = headerY + headerH;
    const int startY = dividerY + librarySubheadingHeight() - 3;

    const auto activateIndex = [&](const int index) {
      if (index < 0 || index >= itemCount) {
        return false;
      }
      selectorIndex = index;
      isHeaderButtonSelected = false;
      isIndexButtonSelected = false;
      isSortButtonSelected = false;
      updateRequired = true;
      handleConfirmAction(itemCount);
      return true;
    };

    if (isLibraryGridMode()) {
      constexpr int cols = 3;
      constexpr int rows = 4;
      constexpr int gapMinX = 8;
      constexpr int gapMinY = 6;
      constexpr int outerPad = 8;
      const int contentBottom = INX_THEME.mainTabsAtBottom() ? mainContentBottom(renderer) + 8 : screenH - 30;
      const int availW = std::max(1, screenW - outerPad * 2);
      const int availH = std::max(1, contentBottom - startY - outerPad * 2);
      const int frameW = std::min(GRID_ICON_SIZE, (availW - (cols - 1) * gapMinX) / cols);
      const int maxFrameH = (availH - (rows - 1) * gapMinY) / rows;
      const int frameH = std::max(96, std::min(GRID_ICON_SIZE, maxFrameH));
      const int remainingH = availH - rows * frameH;
      const int gapY = rows > 1 ? std::max(gapMinY, remainingH / (rows - 1)) : 0;
      const int blockH = rows * frameH + (rows - 1) * gapY;
      const int blockTop = startY + outerPad + std::max(0, (availH - blockH) / 2);
      const int remainingW = availW - cols * frameW;
      const int gapX = cols > 1 ? std::max(gapMinX, remainingW / (cols - 1)) : 0;
      const int blockW = cols * frameW + (cols - 1) * gapX;
      const int row0X = outerPad + std::max(0, (availW - blockW) / 2);

      for (int i = 0; i < std::min(itemCount, GRID_ITEMS_PER_PAGE); ++i) {
        const int row = i / cols;
        const int col = i % cols;
        const int boxX = row0X + col * (frameW + gapX);
        const int boxY = blockTop + row * (frameH + gapY);
        if (x >= boxX && x < boxX + frameW && y >= boxY && y < boxY + frameH) {
          return activateIndex(i);
        }
      }
      return false;
    }

    if (currentViewMode == ViewMode::SHELF_VIEW) {
      constexpr int cols = 3;
      constexpr int rows = 3;
      constexpr int gapX = 15;
      constexpr int gapY = 12;
      constexpr int outerPadX = 14;
      constexpr int outerPadY = 10;
      const int contentBottom = mainContentBottom(renderer) - 30;
      const int availW = screenW - outerPadX * 2;
      const int availH = contentBottom - startY - outerPadY * 2;
      int cardW = 0;
      int cardH = 0;
      getShelfCoverSize(renderer, cardW, cardH);
      const int totalW = cardW * cols + gapX * (cols - 1);
      const int totalH = cardH * rows + gapY * (rows - 1);
      const int originX = outerPadX + std::max(0, (availW - totalW) / 2);
      const int originY = startY + outerPadY + std::max(0, (availH - totalH) / 2);
      for (int i = 0; i < std::min(itemCount, SHELF_ITEMS_PER_PAGE); ++i) {
        const int row = i / cols;
        const int col = i % cols;
        const int cardX = originX + col * (cardW + gapX);
        const int cardY = originY + row * (cardH + gapY);
        if (x >= cardX && x < cardX + cardW && y >= cardY && y < cardY + cardH) {
          return activateIndex(i);
        }
      }
      return false;
    }

    int drawY = startY + 2;
    for (int i = listScrollOffset; i < itemCount; ++i) {
      const int itemH = getItemHeight(currentPageItems[static_cast<size_t>(i)]);
      if (drawY + itemH > screenH) {
        break;
      }
      if (y >= drawY && y < drawY + itemH) {
        return activateIndex(i);
      }
      drawY += itemH;
    }
    return false;
  }

  void loadLibraryFromIndex();
  void ensureTagEntriesLoaded();
  std::string findCachedTag(const std::string& path) const;
  std::function<bool(const TempBookEntry&, const TempBookEntry&)> getReadingStatusComparator(bool ascending) const;

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  TaskHandle_t initialLoadTaskHandle_ = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool halfRefreshOnLoadApplied_ = false;
  volatile bool displayTaskStopRequested_ = false;
  volatile bool isIndexing_ = false;
  volatile bool libraryIndexReloadRequested_ = false;
  volatile int indexingProgress_ = 0;
  volatile int indexingTotal_ = 0;
  volatile bool isInitialLoading_ = false;

  std::string savedFolderPath;
  std::string basepath;
  std::string selectedTagKey_;
  int selectorIndex = 0;
  int listScrollOffset = 0;
  bool updateRequired = false;
  bool favoritesPromoted = true;

  bool isHeaderButtonSelected = false;
  bool isIndexButtonSelected = false;
  bool isSortButtonSelected = false;
  bool favoriteLongPressProcessed = false;
  bool backLongPressProcessed_ = false;
  bool letterPickerVisible_ = false;
  bool letterPickerIgnoreBackRelease_ = false;
  int letterPickerPage_ = 0;
  int letterPickerIndex_ = 0;
  char libraryLetterFilter_ = 0;

  unsigned long libraryListDownNextMs = 0;
  unsigned long libraryListUpNextMs = 0;

  int itemsPerPage;
  int currentPage;
  int totalPages;
  std::vector<LibraryItem> currentPageItems;
  std::vector<LibraryItem> cachedLibraryItems_;
  bool cachedLibraryItemsValid_ = false;
  mutable std::unordered_map<std::string, uint8_t> bookStateCache_;
  std::unordered_map<std::string, bool> directoryHasBooksCache_;
  mutable std::unordered_map<std::string, std::string> shelfImagePathCache_;

  mutable uint8_t* libraryShelfPageBuffer_ = nullptr;
  mutable bool libraryShelfPageBufferStored_ = false;
  mutable int libraryShelfPageBufferPage_ = -1;
  mutable int libraryShelfPageBufferItemCount_ = -1;
  mutable std::string libraryShelfPageBufferKey_;
  mutable bool suppressShelfSelectionHighlight_ = false;
  mutable bool pendingShelfExitHalfRefresh_ = false;

  std::vector<BookTags::Entry> cachedTagEntries_;
  bool cachedTagEntriesLoaded_ = false;

  const std::function<void()> onGoToRecent;
  const std::function<void(const std::string& path)> onSelectBook;
  const std::function<void()> onRecentOpen;
  const std::function<void()> onSettingsOpen;

  ViewMode currentViewMode;
  SortMode currentSortMode;

  void navigateToSelectedMenu() override {
    if (tabSelectorIndex == 0) onRecentOpen();
    if (tabSelectorIndex == 2) onSettingsOpen();
  }

  void toggleViewMode();
  void switchToBookListView();
  void switchToFolderView();
  void switchToTagView();
  void switchToShelfView();
  void leaveShelfViewIfNeeded();
  void startLibraryIndexing();
  bool shouldShowIndexButton() const;
  bool restoreSelectionToPath(const std::string& path);
  bool restoreSelectionToTag(const std::string& tagKey);
  void resetNavigation();
  void goToNextPage();
  void goToPreviousPage();
  bool handlePageNavigation(bool wantUpStep, bool wantDownStep, int itemCount);
  void handleFavoriteLongPress(int itemCount);
  void handleSelectionNavigation(bool wantUpStep, bool wantDownStep, int itemCount);
  void handleButtonSelectionNavigation(bool leftPressed, bool rightPressed);
  void handleConfirmAction(int itemCount);
  void handleBackNavigation();
  void openLetterFilterPicker();
  void closeLetterFilterPicker();
  void handleLetterFilterPickerInput();
  void applyLetterFilterSelection();
  char letterForPickerCell(int page, int index) const;
  char leadingLibraryLetter(const std::string& value) const;
  bool itemMatchesLetterFilter(const LibraryItem& item) const;
  bool tempBookMatchesLetterFilter(const TempBookEntry& entry) const;
  bool isValidBookFile(const std::string& filename) const;
  bool shouldSkipFile(const char* name) const;
  bool directoryHasBooks(const std::string& path);
  int countTotalBooks(const std::string& path);
  void findBooksPaginated(const std::string& path, std::vector<LibraryItem>& books, int startIndex, int count,
                          int& foundCount, bool& stop);
  bool isBookMarked(const std::string& path) const;
  bool isBookOpened(const std::string& path) const;
  bool isBookFinished(const std::string& path) const;
  LibraryItem createBookItem(const std::string& fullPath, const std::string& filename,
                             const std::string& parentPath) const;
  LibraryItem createFolderItem(const std::string& name, const std::string& fullPath) const;
  TempBookEntry createTempBookEntry(const std::string& fullPath, const std::string& filename,
                                    const std::string& parentPath) const;
  void loadAllBooksRecursive();
  void loadAllBooksRecursiveLocked();
  void beginLibraryLoadWithLoadingScreen();
  void applyPaginationToCachedItems();
  void invalidateLibraryCache();
  uint8_t getBookStateFlags(const std::string& path) const;
  std::string getShelfImagePath(const std::string& bookPath) const;
  void loadBooksRecursiveScan();
  void loadFoldersAndBooksCurrentDirectory();
  void loadBooksFromIndex(FsFile& idxFile, const std::string& cleanBase);
  void loadFoldersFromIndex(FsFile& idxFile, const std::string& cleanBase);
  TempBookEntry readBookEntryFromIndex(FsFile& idxFile);
  LibraryItem readDirectoryEntryFromIndex(FsFile& idxFile);
  void skipDirectoryMarker(FsFile& idxFile);
  bool shouldIncludeFolder(const std::string& folderPath, const std::string& cleanBase) const;
  void sortTempBooks(std::vector<TempBookEntry>& tempBooks);
  std::function<bool(const TempBookEntry&, const TempBookEntry&)> getBookComparator() const;
  std::function<bool(const LibraryItem&, const LibraryItem&)> getFolderComparator() const;
  void sortFoldersAndBooks(std::vector<LibraryItem>& tempFolders, std::vector<TempBookEntry>& tempBooks);
  void combineAndPaginateItems(const std::vector<LibraryItem>& tempFolders,
                               const std::vector<TempBookEntry>& tempBooks);
  void applyPaginationToBooks(const std::vector<TempBookEntry>& tempBooks);
  void cycleSortMode();
  void resetLibraryView();
  std::string formatFolderName(const std::string& name) const;
  std::string getBaseFilename(const std::string& filename) const;
  std::string extractFolderName(const std::string& path) const;
  std::string truncateTextIfNeeded(const std::string& text, size_t maxLength) const;
  std::string getHeaderText() const;
  std::string getSortButtonText() const;
  static void taskTrampoline(void* param);
  void displayTaskLoop();
  void render() const;
  void renderLetterFilterPicker() const;
  void renderLibraryList(int startY) const;
  int librarySubheadingHeight() const;
  int renderLibrarySubheading(int startY) const;
  void renderLibraryShelf(int startY) const;
  void renderShelfCard(int index, int startY, bool selected) const;
  bool canUseLibraryShelfBuffer() const;
  bool storeLibraryShelfBuffer() const;
  bool restoreLibraryShelfBuffer() const;
  void freeLibraryShelfBuffer() const;
  void drawShelfSelectionOverlay(int startY) const;
  void renderLibraryGrid(int startY) const;
  bool isLibraryGridMode() const;
  bool isTagViewMode() const;
  void renderGridItemIcon(const LibraryItem& item, int x, int y, int w, int h, bool isSelected, bool isLarge) const;
  void renderItemIcon(const LibraryItem& item, int drawY, int itemHeight, bool isSelected) const;
  void renderBookListBadges(const LibraryItem& item, int drawY, int itemHeight, bool isSelected, int screenWidth) const;
  void renderItemText(const LibraryItem& item, int drawY, int itemHeight, bool isSelected, int screenWidth) const;
  int drawHeaderButton(const std::string& text, int headerY, int headerHeight, int rightX, bool isSelected) const;
  int drawIndexButton(int headerY, int headerHeight, int x, bool isSelected) const;
  int drawSortButton(int headerY, int headerHeight, int rightX) const;
  void drawButtonHints() const;
  int getItemHeight(const LibraryItem& item) const;
};
