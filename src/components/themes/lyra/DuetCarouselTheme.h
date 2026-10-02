#pragma once

#include "components/themes/lyra/LyraFlowTheme.h"

// Crossblot: Duet's five-cover library carousel as a Home theme (geometry
// ported from Duet v0.1.0-alpha.9 FileBrowserActivity's cover carousel, MIT,
// Lauren Landau). A large centre cover with a double selection ring, two
// slimmer perspective covers on each side, then title / author / position and
// reading status underneath. Reuses Flow's icon bar, black list selection and
// SD-baked side tiles; it has no collection shelf.
namespace DuetCarouselMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraFlowMetrics::values;
  v.homeCoverHeight = 330;      // centre cover height; thumbs are generated at this size
  v.homeCoverTileHeight = 470;  // cover + title / author / status footer
  v.homeRecentBooksCount = 5;   // five covers visible at once
  v.homeTopPadding = 41;
  v.homeMenuTopOffset = 10;
  return v;
}();
}  // namespace DuetCarouselMetrics

class DuetCarouselTheme : public LyraFlowTheme {
 public:
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           const std::function<bool()>& storeCoverBuffer, const BookReadingStats* stats = nullptr,
                           float progressPercent = -1.0f, const GlobalReadingStats* globalStats = nullptr,
                           const char* currentChapterTitle = nullptr) const override;
};
