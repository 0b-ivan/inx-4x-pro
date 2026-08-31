#pragma once

/**
 * @file EpubActivity.h
 * @brief Public interface and types for EpubActivity.
 */

#include <Epub.h>
#include <Epub/Section.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "EpubAnnotationUi.h"
#include "EpubDictionaryUi.h"
#include "EpubReadingStats.h"
#include "MenuDrawer.h"
#include "OrientationPickerUi.h"
#include "PresetPickerUi.h"
#include "QuickActionsMenuUi.h"
#include "ReaderButtonBindings.h"
#include "SettingsDrawer.h"
#include "StatusBar.h"
#include "activity/ActivityWithSubactivity.h"
#include "state/BookProgress.h"
#include "state/BookSetting.h"
#include "system/ScreenComponents.h"

struct ViewportInfo {
  int totalMarginTop;
  int totalMarginBottom;
  int totalMarginLeft;
  int totalMarginRight;
  uint16_t width;
  uint16_t height;
  int fontId;
  float lineCompression;
  float wordSpacing;
};

class EpubActivity final : public ActivityWithSubactivity {
  friend class EpubAnnotationUi;
  friend class EpubDictionaryUi;
  friend class OrientationPickerUi;
  friend class PresetPickerUi;
  friend class QuickActionsMenuUi;
  friend class ReaderButtonBindings;

 public:
  struct Bookmark {
    uint16_t spineIndex;
    uint16_t pageNumber;
    uint16_t pageCount;
    char chapterTitle[64];
    uint32_t timestamp;

    bool isValid() const { return spineIndex != 0xFFFF && pageNumber != 0xFFFF; }
  };

  explicit EpubActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub,
                        const std::function<void()>& onGoBack, const std::function<void()>& onGoToRecent);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool handleTouchTap(int x, int y) override;
  bool prioritizesScreenTouch() const override { return annUi_.touchSelecting(); }
  bool skipLoopDelay() override { return true; }
  bool preventAutoSleep() override;

 private:
  int currentFontId;
  int nextFontId;
  bool isToggleClosed = false;
  bool settingsChanged = false;
  bool isBookmarking = false;
  bool isDoingSomethingHeavy = false;
  std::unique_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  std::unique_ptr<BookProgress> bookProgress = nullptr;
  std::unique_ptr<StatusBar> statusBar = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  int pagesUntilFullRefresh = 0;
  bool pendingPercentJump = false;
  float pendingSpineProgress = 0.0f;
  bool suppressNextSectionLoadProgress_ = false;
  bool suppressBackUntilReleased_ = false;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  bool updateRequired = false;
  bool bookmarkLongPressProcessed = false;
  bool leftLongPressProcessed = false;
  int loadingProgress = 0;
  unsigned long lastAutoPageTurnTime = 0;

  const std::function<void()> onGoBack;
  const std::function<void()> onGoToRecent;

  static constexpr int MAX_BOOKMARKS = 200;
  static constexpr const char* BOOKMARKS_FILENAME = "bookmarks.bin";

  std::vector<Bookmark> bookmarks;
  bool showBookmarkIndicator = false;
  int lastPreloadedSpineIndex = -1;
  bool lastPageHadImages = false;
  bool lastPageHadLargeImage = false;

  int lastGoodSpineIndex_ = 0;
  int lastGoodPageNumber_ = 0;
  bool chapterRecoveryAttempted_ = false;

  SettingsDrawer* settingsDrawer = nullptr;
  bool settingsDrawerVisible = false;
  MenuDrawer* menuDrawer = nullptr;
  bool menuDrawerVisible = false;
  BookSettings bookSettings;
  BookSettings settingsDrawerSnapshot_;
  bool hasSettingsDrawerSnapshot_ = false;
  uint8_t bookLayoutAppliedOrientation_ = 0xFF;
  uint32_t statusBarLayoutAppliedSignature_ = 0xFFFFFFFF;
  bool leftButtonLongPressProcessed = false;

  EpubReadingStats readingStats_;

  void renderScreen(bool clearFramebuffer = true);
  void pageTurn(bool forward);
  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  void renderStatusBar(int orientedMarginRight, int orientedMarginBottom, int orientedMarginLeft) const;
  void drawReadingGuideLines(const Page& page, int orientedMarginTop, int orientedMarginRight,
                             int orientedMarginBottom, int orientedMarginLeft, int fontId) const;

  void saveProgress(int spineIndex, int currentPage, int pageCount, bool saveRecentNow = true);
  void loadProgress();
  void ensureMenuDrawer();
  void toggleMenuDrawer();
  void openTableOfContents();
  void toggleSettingsDrawer();
  void onTocChapterSelected(int spineIndex);
  void onBookmarkDrawerSelected(int storageIndex);
  void onAnnotationDrawerSelected(int storageIndex);
  void goToAnnotationPage(int spineIndex, int pageNumber);
  void deleteCache();
  void goHome();
  void deleteProgress();
  void deleteBook();
  void generateFullData();
  void prewarmCurrentSectionImages();
  void regenerateThumbnail();
  void openKOReaderSyncFromMenu();
  void onPercentDrawerSelected(int percent);
  void jumpToPercent(int percent);
  void displayBookTitle();
  void drawLoadingScreen();
  void preloadNextSection();
  void dismissMenuDrawerForBlockingWork(bool repaintReaderScreen = true);
  void readerPopup(const char* message);
  void handleChapterLoadFailure();
  ScreenComponents::LoadingProgressLayout loadingProgressShow(const char* message, int progressPercent0to100);

  void loadBookmarks();
  void saveBookmarks();
  void addBookmark();
  void removeBookmark(int index);
  bool isCurrentPageBookmarked() const;
  void goToBookmark(int index);
  std::string getCurrentChapterTitle() const;
  void drawBookmarkIndicator();

  EpubAnnotationUi annUi_;
  EpubDictionaryUi dictUi_;
  OrientationPickerUi orientationPicker_;
  PresetPickerUi presetPicker_;
  QuickActionsMenuUi quickActionsUi_;
  ReaderButtonBindings btnBindings_;

  void applyBookSettings();
  void saveBookSettings();
  void loadBookSettings();

  void initStats();
  void maybeCommitReadingSessionCount();
  void startPageTimer();
  void pauseReadingStats();
  void endPageTimer();
  void saveBookStats();

  ViewportInfo calculateViewport();
  bool buildSection(int spineIndex, const ViewportInfo& info, bool showProgress = false, bool skipImages = false);
  std::unique_ptr<Section> loadSection(int spineIndex, const ViewportInfo& info, bool showProgress = true);

  void setupOrientation();
  bool syncSettingsFromGlobalIfNeeded();
  uint32_t currentStatusBarLayoutSignature() const;
  bool statusBarLayoutChangedSinceApplied() const;
  void markStatusBarLayoutApplied();
  void onBookSettingsLiveLayoutSync();
  void ensureThumbnailExists(bool coverAvailable);
  bool displayCoverOrTitle();
  void loadCurrentSection(bool showProgress = true);
  void updateExternalState();
  void fastPath();
  bool slowPath();
  void displayBookStats();
};
