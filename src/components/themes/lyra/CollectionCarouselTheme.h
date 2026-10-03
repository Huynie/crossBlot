#pragma once

#include <functional>
#include <string>

#include "components/themes/lyra/LyraFlowTheme.h"

// Crossblot: "Collection Carousel" Home theme. CrumBLE's collection browsing
// (collection name with ◀ ▶ on top, L/R cycles collections) drawn as Duet's
// five-cover carousel (geometry from Duet v0.1.0-alpha.9's cover carousel,
// MIT, Lauren Landau): a large centre cover with a double selection ring, two
// slimmer perspective covers each side, then the book's title / author /
// position. Reuses Flow's icon bar, black list selection and SD-baked tiles.
namespace CollectionCarouselMetrics {
constexpr ThemeMetrics values = [] {
  ThemeMetrics v = LyraFlowMetrics::values;
  v.homeRecentBooksCount = 5;
  v.homeTopPadding = 41;
  return v;
}();
}  // namespace CollectionCarouselMetrics

class CollectionCarouselTheme : public LyraFlowTheme {
 public:
  struct Item {
    std::string thumbPath;  // resolved cover thumb at coverSize(); empty = placeholder card
    std::string title;
    std::string author;
    float progressPercent = -1.0f;  // centre book only; < 0 = unread
    uint32_t readingSeconds = 0;    // centre book only
  };

  // Centre cover size; thumbs for the carousel are generated at exactly this.
  static void coverSize(const GfxRenderer& renderer, int& width, int& height);

  // Draws header + carousel + footer into rect (the band between the status
  // header and the icon bar). itemAt(i) is called only for the five visible
  // positions. centerIndex < 0 when the collection is empty.
  void drawCollectionCarousel(GfxRenderer& renderer, Rect rect, const char* collectionName, bool headerFocused,
                              bool canCycleCollections, int itemCount, int centerIndex, bool carouselFocused,
                              const std::function<Item(int index)>& itemAt, const char* emptyMessage) const;

  // Home draws this theme through drawCollectionCarousel instead.
  void drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                           int selectorIndex, bool& coverRendered, bool& coverBufferStored, bool& bufferRestored,
                           const std::function<bool()>& storeCoverBuffer, const BookReadingStats* stats = nullptr,
                           float progressPercent = -1.0f, const GlobalReadingStats* globalStats = nullptr,
                           const char* currentChapterTitle = nullptr) const override;
};
