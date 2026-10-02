#include "DuetCarouselTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "CoverTiles.h"
#include "RecentBooksStore.h"
#include "activities/reader/BookReadingStats.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Duet's cover proportions (COVER_GRID_MAX_COVER_WIDTH / HEIGHT).
constexpr int kCoverAspectW = 123;
constexpr int kCoverAspectH = 180;
constexpr int kCornerRadius = 6;
constexpr int kSelectionPadding = 6;
constexpr int kTitleLines = 2;

struct Layout {
  int centerX, centerY, centerW, centerH;
  int nearW, nearInnerH, nearOuterH, nearY;
  int farW, farInnerH, farOuterH, farY;
  int leftNearX, rightNearX, leftFarX, rightFarX;
  int titleY, titleLineH, authorY, counterY, progressY, textW;
};

// Same arithmetic as Duet's calculateCoverCarouselLayout, anchored to the top
// of the Home cover band instead of centring in a file-browser content area.
Layout layoutFor(const GfxRenderer& renderer, const Rect& rect, const int sidePadding) {
  Layout l{};
  const int pageWidth = rect.width;
  l.centerH = DuetCarouselMetrics::values.homeCoverHeight;
  l.centerW = std::max(1, l.centerH * kCoverAspectW / kCoverAspectH);
  const int maxCenterW = pageWidth * 52 / 100;
  if (l.centerW > maxCenterW) {
    l.centerW = maxCenterW;
    l.centerH = std::max(1, l.centerW * kCoverAspectH / kCoverAspectW);
  }
  l.centerX = (pageWidth - l.centerW) / 2;
  l.centerY = rect.y + 14;

  l.nearW = std::max(1, l.centerW * 34 / 100);
  l.farW = std::max(1, l.centerW * 22 / 100);
  l.nearInnerH = std::max(1, l.centerH * 66 / 100);
  l.nearOuterH = std::max(1, l.centerH * 76 / 100);
  l.farInnerH = std::max(1, l.centerH * 43 / 100);
  l.farOuterH = std::max(1, l.centerH * 52 / 100);
  l.nearY = l.centerY + (l.centerH - std::max(l.nearInnerH, l.nearOuterH)) / 2;
  l.farY = l.centerY + (l.centerH - std::max(l.farInnerH, l.farOuterH)) / 2;

  constexpr int nearOverlap = 4;
  constexpr int farOverlap = 2;
  constexpr int nearInset = 10;
  const int baseLeftNearX = l.centerX - l.nearW + nearOverlap;
  const int baseRightNearX = l.centerX + l.centerW - nearOverlap;
  l.leftNearX = baseLeftNearX + nearInset;
  l.rightNearX = baseRightNearX - nearInset;
  l.leftFarX = std::max(sidePadding, baseLeftNearX - l.farW + farOverlap);
  l.rightFarX = std::min(pageWidth - sidePadding - l.farW, baseRightNearX + l.nearW - farOverlap);

  l.textW = std::max(40, pageWidth - sidePadding * 2);
  l.titleLineH = renderer.getLineHeight(UI_12_FONT_ID);
  l.titleY = l.centerY + l.centerH + kSelectionPadding + 10;
  l.authorY = l.titleY + l.titleLineH * kTitleLines + 2;
  l.counterY = l.authorY + renderer.getLineHeight(UI_10_FONT_ID) + 4;
  l.progressY = l.counterY + renderer.getLineHeight(SMALL_FONT_ID) + 5;
  return l;
}

// Draws one perspective side cover from its baked 2bpp SD tile, baking the
// tile from the cover thumb on first use. Falls back to an outline card.
void drawSideCover(GfxRenderer& renderer, const std::string& thumbPath, const uint8_t role, const int x, const int y,
                   const int w, const int leftH, const int rightH) {
  const int hMax = std::max(leftH, rightH);
  const int stride = (w + 3) / 4;
  const size_t bytes = static_cast<size_t>(stride) * static_cast<size_t>(hMax);
  bool ready = false;
  std::unique_ptr<uint8_t[]> tile(thumbPath.empty() ? nullptr : new (std::nothrow) uint8_t[bytes]);
  if (tile) {
    std::memset(tile.get(), 0xFF, bytes);
    ready = CoverTiles::loadTile(thumbPath, role, CoverTiles::kFormat2bpp, w, hMax, stride, leftH, rightH, w,
                                 tile.get(), bytes);
    if (!ready) {
      std::memset(tile.get(), 0xFF, bytes);
      FsFile file;
      if (Storage.openFileForRead("DUET", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderer.renderPerspectiveBitmapToPacked2bpp(bitmap, w, leftH, rightH, tile.get());
          ready = true;
          CoverTiles::saveTile(thumbPath, role, CoverTiles::kFormat2bpp, w, hMax, stride, leftH, rightH, w, tile.get(),
                               bytes);
        }
        file.close();
      }
    }
  }
  renderer.fillRect(x, y, w, hMax, false);
  if (ready) {
    renderer.drawPacked2bpp(tile.get(), stride, x, y, w, hMax);
  } else {
    // Placeholder: the perspective silhouette as an outline.
    const int topL = y + (hMax - leftH) / 2;
    const int topR = y + (hMax - rightH) / 2;
    renderer.drawLine(x, topL, x + w - 1, topR, true);
    renderer.drawLine(x, topL + leftH - 1, x + w - 1, topR + rightH - 1, true);
    renderer.drawLine(x, topL, x, topL + leftH - 1, true);
    renderer.drawLine(x + w - 1, topR, x + w - 1, topR + rightH - 1, true);
  }
}

// Centre cover: aspect-fill crop into a baked 2bpp tile (role 5), rounded.
bool drawCenterCover(GfxRenderer& renderer, const std::string& thumbPath, const int x, const int y, const int w,
                     const int h) {
  if (thumbPath.empty()) return false;
  const int stride = (w + 3) / 4;
  const size_t bytes = static_cast<size_t>(stride) * static_cast<size_t>(h);
  std::unique_ptr<uint8_t[]> tile(new (std::nothrow) uint8_t[bytes]);
  if (!tile) return false;
  std::memset(tile.get(), 0xFF, bytes);
  bool ready = CoverTiles::loadTile(thumbPath, CoverTiles::kRoleCenterThumb, CoverTiles::kFormat2bpp, w, h, stride, 0,
                                    0, 0, tile.get(), bytes);
  if (!ready) {
    std::memset(tile.get(), 0xFF, bytes);
    FsFile file;
    if (Storage.openFileForRead("DUET", thumbPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
        const float srcRatio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float dstRatio = static_cast<float>(w) / static_cast<float>(h);
        const float cropX = srcRatio > dstRatio ? 1.0f - dstRatio / srcRatio : 0.0f;
        const float cropY = srcRatio < dstRatio ? 1.0f - srcRatio / dstRatio : 0.0f;
        renderer.renderBitmapToPacked2bpp(bitmap, w, h, tile.get(), cropX, cropY);
        ready = true;
        CoverTiles::saveTile(thumbPath, CoverTiles::kRoleCenterThumb, CoverTiles::kFormat2bpp, w, h, stride, 0, 0, 0,
                             tile.get(), bytes);
      }
      file.close();
    }
  }
  if (!ready) return false;
  renderer.fillRect(x, y, w, h, false);
  renderer.drawPacked2bpp(tile.get(), stride, x, y, w, h);
  renderer.maskRoundedRectOutsideCorners(x, y, w, h, kCornerRadius, Color::White);
  return true;
}

std::string thumbPathFor(const RecentBook& book) {
  if (book.coverBmpPath.empty() || book.coverState == RecentBook::CoverState::Missing) return {};
  std::string path = UITheme::getCoverThumbPath(book.coverBmpPath, DuetCarouselMetrics::values.homeCoverHeight);
  return Storage.exists(path.c_str()) ? path : std::string();
}

std::string displayTitle(const RecentBook& book) {
  if (!book.title.empty()) return book.title;
  const size_t slash = book.path.find_last_of('/');
  std::string name = slash == std::string::npos ? book.path : book.path.substr(slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0) name.resize(dot);
  return name;
}
}  // namespace

void DuetCarouselTheme::drawRecentBookCover(GfxRenderer& renderer, const Rect rect,
                                            const std::vector<RecentBook>& recentBooks, const int selectorIndex,
                                            bool& coverRendered, bool& coverBufferStored, bool& /*bufferRestored*/,
                                            const std::function<bool()>& /*storeCoverBuffer*/,
                                            const BookReadingStats* stats, const float progressPercent,
                                            const GlobalReadingStats* /*globalStats*/,
                                            const char* /*currentChapterTitle*/) const {
  coverRendered = true;
  coverBufferStored = false;  // always repainted; the side tiles are cheap SD reads

  const int count = static_cast<int>(recentBooks.size());
  const auto& metrics = DuetCarouselMetrics::values;
  const Layout l = layoutFor(renderer, rect, metrics.contentSidePadding);
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  if (count == 0) {
    const char* msg = tr(STR_NO_RECENT_BOOKS);
    const int textW = renderer.getTextWidth(UI_12_FONT_ID, msg, EpdFontFamily::REGULAR);
    renderer.drawText(UI_12_FONT_ID, (rect.width - textW) / 2, l.centerY + l.centerH / 2, msg, true,
                      EpdFontFamily::REGULAR);
    return;
  }

  // Same selector encoding as Flow: < count = that book selected; count + i =
  // keep book i centred without the selection ring (cursor is elsewhere).
  const bool hasSelection = selectorIndex >= 0 && selectorIndex < count;
  int center = 0;
  if (hasSelection) {
    center = selectorIndex;
  } else if (selectorIndex - count >= 0 && selectorIndex - count < count) {
    center = selectorIndex - count;
  }
  const auto wrap = [count](const int i) { return ((i % count) + count) % count; };

  if (count >= 5) {
    drawSideCover(renderer, thumbPathFor(recentBooks[wrap(center - 2)]), CoverTiles::kRoleLeftFar, l.leftFarX, l.farY,
                  l.farW, l.farInnerH, l.farOuterH);
  }
  if (count >= 4) {
    drawSideCover(renderer, thumbPathFor(recentBooks[wrap(center + 2)]), CoverTiles::kRoleRightFar, l.rightFarX,
                  l.farY, l.farW, l.farOuterH, l.farInnerH);
  }
  if (count >= 2) {
    drawSideCover(renderer, thumbPathFor(recentBooks[wrap(center - 1)]), CoverTiles::kRoleLeftNear, l.leftNearX,
                  l.nearY, l.nearW, l.nearInnerH, l.nearOuterH);
  }
  if (count >= 3) {
    drawSideCover(renderer, thumbPathFor(recentBooks[wrap(center + 1)]), CoverTiles::kRoleRightNear, l.rightNearX,
                  l.nearY, l.nearW, l.nearOuterH, l.nearInnerH);
  }

  const RecentBook& book = recentBooks[center];
  if (!drawCenterCover(renderer, thumbPathFor(book), l.centerX, l.centerY, l.centerW, l.centerH)) {
    renderer.fillRect(l.centerX, l.centerY, l.centerW, l.centerH, false);
    renderer.drawRoundedRect(l.centerX, l.centerY, l.centerW, l.centerH, 1, kCornerRadius, true);
    const auto lines = renderer.wrappedText(UI_12_FONT_ID, displayTitle(book).c_str(), l.centerW - 20, 4,
                                            EpdFontFamily::BOLD);
    int y = l.centerY + (l.centerH - static_cast<int>(lines.size()) * l.titleLineH) / 2;
    for (const auto& line : lines) {
      const int lw = renderer.getTextWidth(UI_12_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, l.centerX + (l.centerW - lw) / 2, y, line.c_str(), true, EpdFontFamily::BOLD);
      y += l.titleLineH;
    }
  }

  // Duet's double selection ring around the centre cover.
  if (hasSelection) {
    const int outer = kSelectionPadding + 3;
    renderer.drawRoundedRect(l.centerX - kSelectionPadding, l.centerY - kSelectionPadding,
                             l.centerW + kSelectionPadding * 2, l.centerH + kSelectionPadding * 2, 3,
                             kCornerRadius + kSelectionPadding, true);
    renderer.drawRoundedRect(l.centerX - outer, l.centerY - outer, l.centerW + outer * 2, l.centerH + outer * 2, 1,
                             kCornerRadius + outer, true);
  }

  // Footer: title (two lines), author, "n of N | status", progress bar.
  int y = l.titleY;
  for (const auto& line :
       renderer.wrappedText(UI_12_FONT_ID, displayTitle(book).c_str(), l.textW, kTitleLines, EpdFontFamily::BOLD)) {
    const int lw = renderer.getTextWidth(UI_12_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, (rect.width - lw) / 2, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += l.titleLineH;
  }
  if (!book.author.empty()) {
    const std::string author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), l.textW);
    const int aw = renderer.getTextWidth(UI_10_FONT_ID, author.c_str(), EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, (rect.width - aw) / 2, l.authorY, author.c_str(), true, EpdFontFamily::REGULAR);
  }

  int percent = progressPercent >= 0.0f ? static_cast<int>(progressPercent + 0.5f) : -1;
  const char* status = tr(STR_BOOK_STATUS_UNREAD);
  if (stats != nullptr && stats->isCompleted) {
    status = tr(STR_BOOK_STATUS_FINISHED);
    percent = 100;
  } else if (percent > 0) {
    status = tr(STR_BOOK_STATUS_READING);
  }
  char counter[64];
  if (percent > 0) {
    snprintf(counter, sizeof(counter), "%d of %d  |  %s %d%%", center + 1, count, status, percent);
  } else {
    snprintf(counter, sizeof(counter), "%d of %d  |  %s", center + 1, count, status);
  }
  const std::string counterText = renderer.truncatedText(SMALL_FONT_ID, counter, l.textW);
  const int cw = renderer.getTextWidth(SMALL_FONT_ID, counterText.c_str(), EpdFontFamily::REGULAR);
  renderer.drawText(SMALL_FONT_ID, (rect.width - cw) / 2, l.counterY, counterText.c_str(), true,
                    EpdFontFamily::REGULAR);

  if (percent > 0) {
    const int px = (rect.width - l.centerW) / 2;
    renderer.drawRect(px, l.progressY, l.centerW, 6, true);
    const int filled = std::max(1, ((l.centerW - 2) * std::clamp(percent, 0, 100)) / 100);
    renderer.fillRect(px + 1, l.progressY + 1, filled, 4, true);
  }
}
