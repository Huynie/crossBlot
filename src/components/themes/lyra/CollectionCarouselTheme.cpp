#include "CollectionCarouselTheme.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "CoverTiles.h"
#include "activities/reader/BookReadingStats.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// Duet's cover proportions (COVER_GRID_MAX_COVER_WIDTH / HEIGHT).
constexpr int kCoverAspectW = 123;
constexpr int kCoverAspectH = 180;
constexpr int kCornerRadius = 6;
constexpr int kSelectionPadding = 6;
constexpr int kTitleLines = 1;  // long titles truncate with an ellipsis
constexpr int kArrowSize = 7;
constexpr int kProgressBarHeight = 6;

struct Layout {
  int headerY, headerLineH, counterY;
  int centerX, centerY, centerW, centerH;
  int nearW, nearInnerH, nearOuterH, nearY;
  int farW, farInnerH, farOuterH, farY;
  int leftNearX, rightNearX, leftFarX, rightFarX;
  int progressY, progressLabelY, titleY, titleLineH, textW;
};

// Duet's calculateCoverCarouselLayout, stacked under a collection header.
Layout layoutFor(const GfxRenderer& renderer, const Rect& rect, const int sidePadding) {
  Layout l{};
  const int pageWidth = rect.width;
  l.headerLineH = renderer.getLineHeight(UI_12_FONT_ID);
  l.headerY = rect.y;
  l.counterY = l.headerY + l.headerLineH;
  CollectionCarouselTheme::coverSize(renderer, l.centerW, l.centerH);
  l.centerX = (pageWidth - l.centerW) / 2;
  l.centerY = l.counterY + renderer.getLineHeight(SMALL_FONT_ID) + 24;  // breathing room under the count

  // The side covers share the space beside the (large) centre cover: the near
  // cover tucks a quarter of its width under the centre, the far cover runs
  // to the screen edge under the near one. Heights keep Duet's ratios.
  constexpr int edgePadding = 8;
  const int side = std::max(24, l.centerX - edgePadding);
  l.nearW = side * 75 / 100;
  l.farW = side * 50 / 100;
  l.nearInnerH = std::max(1, l.centerH * 66 / 100);
  l.nearOuterH = std::max(1, l.centerH * 76 / 100);
  l.farInnerH = std::max(1, l.centerH * 43 / 100);
  l.farOuterH = std::max(1, l.centerH * 52 / 100);
  l.nearY = l.centerY + (l.centerH - std::max(l.nearInnerH, l.nearOuterH)) / 2;
  l.farY = l.centerY + (l.centerH - std::max(l.farInnerH, l.farOuterH)) / 2;
  const int tucked = l.nearW / 4;
  l.leftNearX = l.centerX - l.nearW + tucked;
  l.rightNearX = l.centerX + l.centerW - tucked;
  l.leftFarX = edgePadding;
  l.rightFarX = pageWidth - edgePadding - l.farW;
  (void)sidePadding;

  l.textW = std::max(40, pageWidth - sidePadding * 2);
  l.titleLineH = renderer.getLineHeight(UI_12_FONT_ID);
  l.progressY = l.centerY + l.centerH + kSelectionPadding + 24;  // gap under the cover
  l.progressLabelY = l.progressY + kProgressBarHeight + 8;  // space under the bar
  l.titleY = l.progressLabelY + renderer.getLineHeight(SMALL_FONT_ID) + 14;  // space above the title
  return l;
}

// One perspective side cover from its baked 2bpp SD tile, baked from the
// cover thumb on first use. Falls back to an outline silhouette.
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
      if (Storage.openFileForRead("CCAR", thumbPath, file)) {
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
    return;
  }
  const int topL = y + (hMax - leftH) / 2;
  const int topR = y + (hMax - rightH) / 2;
  renderer.drawLine(x, topL, x + w - 1, topR, true);
  renderer.drawLine(x, topL + leftH - 1, x + w - 1, topR + rightH - 1, true);
  renderer.drawLine(x, topL, x, topL + leftH - 1, true);
  renderer.drawLine(x + w - 1, topR, x + w - 1, topR + rightH - 1, true);
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
    if (Storage.openFileForRead("CCAR", thumbPath, file)) {
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

void drawCenteredText(GfxRenderer& renderer, const int fontId, const int pageWidth, const int y, const char* text,
                      const EpdFontFamily::Style style) {
  const int w = renderer.getTextWidth(fontId, text, style);
  renderer.drawText(fontId, (pageWidth - w) / 2, y, text, true, style);
}

void drawSelectionRing(GfxRenderer& renderer, const Layout& l) {
  const int outer = kSelectionPadding + 3;
  renderer.drawRoundedRect(l.centerX - kSelectionPadding, l.centerY - kSelectionPadding,
                           l.centerW + kSelectionPadding * 2, l.centerH + kSelectionPadding * 2, 3,
                           kCornerRadius + kSelectionPadding, true);
  renderer.drawRoundedRect(l.centerX - outer, l.centerY - outer, l.centerW + outer * 2, l.centerH + outer * 2, 1,
                           kCornerRadius + outer, true);
}

void drawArrow(GfxRenderer& renderer, const int tipX, const int midY, const bool pointsLeft) {
  const int baseX = pointsLeft ? tipX + kArrowSize : tipX - kArrowSize;
  const int xs[3] = {tipX, baseX, baseX};
  const int ys[3] = {midY, midY - kArrowSize, midY + kArrowSize};
  renderer.fillPolygon(xs, ys, 3, true);
}
}  // namespace

void CollectionCarouselTheme::coverSize(const GfxRenderer& renderer, int& width, int& height) {
  // About Lyra Carousel's centre cover (296x468), sized to leave room for the
  // collection header above and the title / author / position below.
  height = std::min(468, renderer.getScreenHeight() * 53 / 100);
  width = std::max(1, height * kCoverAspectW / kCoverAspectH);
  const int maxWidth = renderer.getScreenWidth() * 58 / 100;
  if (width > maxWidth) {
    width = maxWidth;
    height = std::max(1, width * kCoverAspectH / kCoverAspectW);
  }
}

void CollectionCarouselTheme::drawRecentBookCover(GfxRenderer&, Rect, const std::vector<RecentBook>&, int,
                                                  bool& coverRendered, bool& coverBufferStored, bool&,
                                                  const std::function<bool()>&, const BookReadingStats*, float,
                                                  const GlobalReadingStats*, const char*) const {
  coverRendered = true;
  coverBufferStored = false;
}

void CollectionCarouselTheme::drawCollectionCarousel(GfxRenderer& renderer, const Rect rect,
                                                     const char* collectionName, const bool headerFocused,
                                                     const bool canCycleCollections, const int itemCount,
                                                     const int centerIndex, const bool carouselFocused,
                                                     const std::function<Item(int index)>& itemAt,
                                                     const char* emptyMessage) const {
  const Layout l = layoutFor(renderer, rect, CollectionCarouselMetrics::values.contentSidePadding);
  const int pageWidth = rect.width;
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  // Collection header: bold with ◀ ▶ when focused, like CrumBLE's shelf tab.
  const char* name = collectionName != nullptr ? collectionName : "";
  const auto headerStyle = headerFocused ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  const int nameW = renderer.getTextWidth(UI_12_FONT_ID, name, headerStyle);
  drawCenteredText(renderer, UI_12_FONT_ID, pageWidth, l.headerY, name, headerStyle);
  if (headerFocused && canCycleCollections) {
    const int midY = l.headerY + l.headerLineH / 2;
    drawArrow(renderer, (pageWidth - nameW) / 2 - 10 - kArrowSize, midY, true);
    drawArrow(renderer, (pageWidth + nameW) / 2 + 10 + kArrowSize, midY, false);
  }

  if (itemCount > 0 && centerIndex >= 0) {
    char counter[32];
    snprintf(counter, sizeof(counter), "%d of %d", centerIndex + 1, itemCount);
    drawCenteredText(renderer, SMALL_FONT_ID, pageWidth, l.counterY, counter, EpdFontFamily::REGULAR);
  }

  if (itemCount <= 0 || centerIndex < 0) {
    renderer.drawRoundedRect(l.centerX, l.centerY, l.centerW, l.centerH, 1, kCornerRadius, true);
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, emptyMessage != nullptr ? emptyMessage : "",
                                            l.centerW - 24, 4, EpdFontFamily::REGULAR);
    int y = l.centerY + (l.centerH - static_cast<int>(lines.size()) * renderer.getLineHeight(UI_10_FONT_ID)) / 2;
    for (const auto& line : lines) {
      drawCenteredText(renderer, UI_10_FONT_ID, pageWidth, y, line.c_str(), EpdFontFamily::REGULAR);
      y += renderer.getLineHeight(UI_10_FONT_ID);
    }
    if (carouselFocused) drawSelectionRing(renderer, l);
    return;
  }

  const auto wrap = [itemCount](const int i) { return ((i % itemCount) + itemCount) % itemCount; };
  if (itemCount >= 5) {
    drawSideCover(renderer, itemAt(wrap(centerIndex - 2)).thumbPath, CoverTiles::kRoleLeftFar, l.leftFarX, l.farY,
                  l.farW, l.farInnerH, l.farOuterH);
  }
  if (itemCount >= 4) {
    drawSideCover(renderer, itemAt(wrap(centerIndex + 2)).thumbPath, CoverTiles::kRoleRightFar, l.rightFarX, l.farY,
                  l.farW, l.farOuterH, l.farInnerH);
  }
  if (itemCount >= 2) {
    drawSideCover(renderer, itemAt(wrap(centerIndex - 1)).thumbPath, CoverTiles::kRoleLeftNear, l.leftNearX, l.nearY,
                  l.nearW, l.nearInnerH, l.nearOuterH);
  }
  if (itemCount >= 3) {
    drawSideCover(renderer, itemAt(wrap(centerIndex + 1)).thumbPath, CoverTiles::kRoleRightNear, l.rightNearX,
                  l.nearY, l.nearW, l.nearOuterH, l.nearInnerH);
  }

  const Item center = itemAt(centerIndex);
  if (!drawCenterCover(renderer, center.thumbPath, l.centerX, l.centerY, l.centerW, l.centerH)) {
    renderer.fillRect(l.centerX, l.centerY, l.centerW, l.centerH, false);
    renderer.drawRoundedRect(l.centerX, l.centerY, l.centerW, l.centerH, 1, kCornerRadius, true);
    const auto lines =
        renderer.wrappedText(UI_12_FONT_ID, center.title.c_str(), l.centerW - 20, 4, EpdFontFamily::BOLD);
    int y = l.centerY + (l.centerH - static_cast<int>(lines.size()) * l.titleLineH) / 2;
    for (const auto& line : lines) {
      drawCenteredText(renderer, UI_12_FONT_ID, pageWidth, y, line.c_str(), EpdFontFamily::BOLD);
      y += l.titleLineH;
    }
  }

  // Duet's double selection ring while the carousel row has focus.
  if (carouselFocused) drawSelectionRing(renderer, l);

  // Reading progress under the cover.
  {
    const int pct =
        center.progressPercent > 0.0f ? std::clamp(static_cast<int>(center.progressPercent + 0.5f), 1, 100) : 0;
    // Borderless: light-gray track, black fill for the part read.
    renderer.fillRectDither(l.centerX, l.progressY, l.centerW, kProgressBarHeight, Color::LightGray);
    if (pct > 0) {
      const int filled = std::max(1, (l.centerW * pct) / 100);
      renderer.fillRect(l.centerX, l.progressY, filled, kProgressBarHeight, true);
    }
    // Time spent on the left, percentage on the right, aligned to the bar.
    if (center.readingSeconds > 0) {
      char timeText[24];
      BookReadingStats::formatDuration(center.readingSeconds, timeText, sizeof(timeText));
      renderer.drawText(SMALL_FONT_ID, l.centerX, l.progressLabelY, timeText, true, EpdFontFamily::REGULAR);
    }
    char pctText[8];
    snprintf(pctText, sizeof(pctText), "%d%%", pct);
    const int pctW = renderer.getTextWidth(SMALL_FONT_ID, pctText, EpdFontFamily::REGULAR);
    renderer.drawText(SMALL_FONT_ID, l.centerX + l.centerW - pctW, l.progressLabelY, pctText, true,
                      EpdFontFamily::REGULAR);
  }

  // Footer: title (two lines), author.
  int y = l.titleY;
  for (const auto& line :
       renderer.wrappedText(UI_12_FONT_ID, center.title.c_str(), l.textW, kTitleLines, EpdFontFamily::BOLD)) {
    drawCenteredText(renderer, UI_12_FONT_ID, pageWidth, y, line.c_str(), EpdFontFamily::BOLD);
    y += l.titleLineH;
  }
  y += 2;
  if (!center.author.empty()) {
    const std::string author = renderer.truncatedText(UI_10_FONT_ID, center.author.c_str(), l.textW);
    drawCenteredText(renderer, UI_10_FONT_ID, pageWidth, y, author.c_str(), EpdFontFamily::REGULAR);
  }
}
