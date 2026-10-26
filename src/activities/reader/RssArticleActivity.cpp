#include "RssArticleActivity.h"

#include <BidiUtils.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "apps_local/rss/RssTime.h"
#include "components/UITheme.h"
#include "fontIds.h"

void RssArticleActivity::onEnter() {
  Activity::onEnter();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  requestUpdate();
}

void RssArticleActivity::onExit() {
  Activity::onExit();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  lines.clear();
}

void RssArticleActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const auto [prevTriggered, nextTriggered, fromTilt] = ReaderUtils::detectPageTurn(mappedInput);
  (void)fromTilt;
  if (prevTriggered && currentPage > 0) {
    currentPage--;
    requestUpdate();
  } else if (nextTriggered && currentPage < totalPages - 1) {
    currentPage++;
    requestUpdate();
  }
}

void RssArticleActivity::render(RenderLock&&) {
  if (!initialized) initializeLayout();

  if (lines.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(UI_12_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_NO_ARTICLE_TEXT), true,
                              EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  if (currentPage < 0) currentPage = 0;
  if (currentPage >= totalPages) currentPage = totalPages - 1;

  renderer.clearScreen();
  renderPage();
}

void RssArticleActivity::initializeLayout() {
  fontId = SETTINGS.getReaderFontId();
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
  marginTop += SETTINGS.screenMargin;
  marginLeft += SETTINGS.screenMargin;
  marginRight += SETTINGS.screenMargin;
  marginBottom += std::max(SETTINGS.screenMargin, static_cast<uint8_t>(UITheme::getInstance().getStatusBarHeight()));

  viewportWidth = renderer.getScreenWidth() - marginLeft - marginRight;
  const int viewportHeight = renderer.getScreenHeight() - marginTop - marginBottom;

  buildLines();
  pageStarts.clear();
  pageStarts.reserve(MAX_ARTICLE_LINES);
  pageStarts.push_back(0);
  int used = 0;
  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    const int height = renderer.getLineHeight(lineFont(i));
    if (used > 0 && used + height > viewportHeight) {
      pageStarts.push_back(i);
      used = 0;
    }
    used += height;
  }
  totalPages = static_cast<int>(pageStarts.size());
  initialized = true;
}

int RssArticleActivity::lineFont(const int index) const {
  return index >= titleEnd && index < headerEnd ? UI_10_FONT_ID : fontId;
}

void RssArticleActivity::buildLines() {
  lines.clear();
  lines.reserve(MAX_ARTICLE_LINES);

  // Header lines share the existing bounded line storage; page offsets avoid
  // duplicating body text when the title and metadata use different heights.
  if (!item.title.empty()) {
    if (renderer.isSdCardFont(fontId)) renderer.ensureSdCardFontReady(fontId, item.title.c_str(), 0x02);
    auto title = renderer.wrappedText(fontId, item.title.c_str(), viewportWidth, 12, EpdFontFamily::BOLD);
    lines.insert(lines.end(), title.begin(), title.end());
  }
  titleEnd = static_cast<int>(lines.size());
  const auto appendMetadata = [&](const std::string& value) {
    if (value.empty()) return;
    auto wrapped = renderer.wrappedText(UI_10_FONT_ID, value.c_str(), viewportWidth, 4);
    lines.insert(lines.end(), wrapped.begin(), wrapped.end());
  };
  appendMetadata(rsstime::formatPublished(item.published, SETTINGS.clockUtcOffsetQ));
  appendMetadata(source + (source.empty() || item.author.empty() ? "" : " / ") + item.author);
  appendMetadata(availability);
  if (!lines.empty()) lines.emplace_back();
  headerEnd = static_cast<int>(lines.size());
  std::string text = item.content.empty() ? tr(STR_NO_ARTICLE_TEXT) : item.content;
  std::replace(text.begin(), text.end(), '\r', '\n');

  size_t start = 0;
  while (start <= text.size() && static_cast<int>(lines.size()) < MAX_ARTICLE_LINES) {
    size_t end = text.find('\n', start);
    std::string paragraph = end == std::string::npos ? text.substr(start) : text.substr(start, end - start);

    while (!paragraph.empty() && paragraph.front() == ' ') paragraph.erase(paragraph.begin());
    while (!paragraph.empty() && paragraph.back() == ' ') paragraph.pop_back();

    if (paragraph.empty()) {
      if (!lines.empty() && !lines.back().empty()) lines.emplace_back();
    } else {
      if (renderer.isSdCardFont(fontId)) renderer.ensureSdCardFontReady(fontId, paragraph.c_str(), 0x01);
      auto wrapped = renderer.wrappedText(fontId, paragraph.c_str(), viewportWidth,
                                          MAX_ARTICLE_LINES - static_cast<int>(lines.size()));
      lines.insert(lines.end(), wrapped.begin(), wrapped.end());
    }

    if (end == std::string::npos) break;
    start = end + 1;
  }
}

void RssArticleActivity::renderPage() {
  const int startLine = pageStarts[currentPage];
  const int endLine = currentPage + 1 < totalPages ? pageStarts[currentPage + 1] : static_cast<int>(lines.size());

  auto renderLines = [&]() {
    int y = marginTop;
    for (int i = startLine; i < endLine; i++) {
      const auto& line = lines[i];
      const int drawFont = lineFont(i);
      const auto style = i < titleEnd ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
      if (!line.empty()) {
        int x = marginLeft;
        uint8_t effectiveAlignment = i < headerEnd ? CrossPointSettings::LEFT_ALIGN : SETTINGS.paragraphAlignment;
        const bool lineIsRtl = BidiUtils::startsWithRtl(line.c_str(), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH);
        if (lineIsRtl && (effectiveAlignment == CrossPointSettings::LEFT_ALIGN ||
                          effectiveAlignment == CrossPointSettings::JUSTIFIED)) {
          effectiveAlignment = CrossPointSettings::RIGHT_ALIGN;
        }
        const int textWidth = renderer.getTextAdvanceX(drawFont, line.c_str(), style);
        if (effectiveAlignment == CrossPointSettings::CENTER_ALIGN) {
          x = marginLeft + (viewportWidth - textWidth) / 2;
        } else if (effectiveAlignment == CrossPointSettings::RIGHT_ALIGN) {
          x = marginLeft + viewportWidth - textWidth;
        }
        renderer.drawText(drawFont, x, y, line.c_str(), true, style);
      }
      y += renderer.getLineHeight(drawFont);
    }
  };

  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  renderLines();
  scope.endScanAndPrewarm();

  renderLines();
  std::string title;
  if (SETTINGS.statusBarTitle != CrossPointSettings::STATUS_BAR_TITLE::HIDE_TITLE) title = item.title;
  const float progress = totalPages > 0 ? (currentPage + 1) * 100.0f / totalPages : 0;
  GUI.drawStatusBar(renderer, progress, currentPage + 1, totalPages, title);
  ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh);

  if (SETTINGS.textAntiAliasing) {
    ReaderUtils::renderAntiAliased(renderer, [&renderLines]() { renderLines(); });
  }
}
