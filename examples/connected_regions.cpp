#include "BitmapPlusPlus.hpp"
#include <cmath>
#include <iostream>

namespace {
  int g_failures = 0;

  void expect(const bool condition, const char *message) {
    if (!condition) {
      std::cerr << "FAILED: " << message << std::endl;
      ++g_failures;
    }
  }

  bool nearly_equal(const double a, const double b) { return std::abs(a - b) < 1e-9; }

  bool same_region(const bmp::ConnectedRegion &r, const std::int32_t x, const std::int32_t y,
                   const std::int32_t w, const std::int32_t h, const std::size_t area) {
    return r.x == x && r.y == y && r.width == w && r.height == h && r.area == area;
  }

  // Icons touching the image borders on a black background.
  // Width 17 makes BMP rows need padding bytes (17*3=51 -> 52), exercising the
  // save/load row-padding round trip.
  bmp::Bitmap make_edge_icons() {
    bmp::Bitmap image(17, 16);
    image.clear(bmp::Black);
    image.fill_rect(0, 0, 3, 3, bmp::White);   // top-left corner icon
    image.fill_rect(14, 0, 3, 4, bmp::Red);    // touches the right edge
    image.fill_rect(6, 15, 4, 1, bmp::Green);  // touches the bottom edge
    return image;
  }
} // namespace

int main(void) {
  try {
    // --- 1) Edge-touching icons: count, stable order, boxes, areas, centroids ---
    {
      const bmp::Bitmap image = make_edge_icons();
      const bmp::Bitmap original = image; // to verify the source is not modified

      // Threshold 64: red (luminance 76) and green (149) icons stay foreground
      const auto regions = image.extract_regions(64, bmp::Connectivity::Eight);
      expect(regions.size() == 3, "edge icons: expected 3 regions");

      if (regions.size() == 3) {
        // Stable scan order: top-to-bottom, left-to-right
        expect(same_region(regions[0], 0, 0, 3, 3, 9), "edge icons: region 0 box/area mismatch");
        expect(same_region(regions[1], 14, 0, 3, 4, 12), "edge icons: region 1 box/area mismatch");
        expect(same_region(regions[2], 6, 15, 4, 1, 4), "edge icons: region 2 box/area mismatch");

        expect(nearly_equal(regions[0].centroid_x, 1.0) && nearly_equal(regions[0].centroid_y, 1.0),
               "edge icons: region 0 centroid mismatch");
        expect(nearly_equal(regions[1].centroid_x, 15.0) && nearly_equal(regions[1].centroid_y, 1.5),
               "edge icons: region 1 centroid mismatch");
        expect(nearly_equal(regions[2].centroid_x, 7.5) && nearly_equal(regions[2].centroid_y, 15.0),
               "edge icons: region 2 centroid mismatch");

        // Crops must contain exactly the source pixels of each bounding box
        bool crops_ok = true;
        for (const auto &r : regions) {
          if (r.crop.width() != r.width || r.crop.height() != r.height) {
            crops_ok = false;
            break;
          }
          for (std::int32_t cy = 0; cy < r.height && crops_ok; ++cy)
            for (std::int32_t cx = 0; cx < r.width && crops_ok; ++cx)
              crops_ok = (r.crop.get(cx, cy) == image.get(r.x + cx, r.y + cy));
        }
        expect(crops_ok, "edge icons: crop pixels do not match the source image");
        expect(regions[0].crop.get(0, 0) == bmp::White, "edge icons: crop 0 color mismatch");
        expect(regions[1].crop.get(0, 0) == bmp::Red, "edge icons: crop 1 color mismatch");
        expect(regions[2].crop.get(0, 0) == bmp::Green, "edge icons: crop 2 color mismatch");
      }

      // The source bitmap (colors, and implicitly its rows) must be untouched
      expect(image == original, "edge icons: extract_regions() modified the source bitmap");

      // --- BMP round trip: row padding must survive save/load unchanged ---
      const auto fixture_path = std::filesystem::path(BIN_DIR) / "connected_regions_edge_icons.bmp";
      image.save(fixture_path);
      bmp::Bitmap loaded;
      loaded.load(fixture_path);
      expect(loaded == image, "edge icons: save/load round trip altered the bitmap");
      const auto loaded_regions = loaded.extract_regions(64, bmp::Connectivity::Eight);
      expect(loaded_regions.size() == 3, "edge icons: expected 3 regions after reload");
      if (loaded_regions.size() == 3) {
        expect(same_region(loaded_regions[0], 0, 0, 3, 3, 9), "edge icons: reloaded region 0 mismatch");
        expect(same_region(loaded_regions[1], 14, 0, 3, 4, 12), "edge icons: reloaded region 1 mismatch");
        expect(same_region(loaded_regions[2], 6, 15, 4, 1, 4), "edge icons: reloaded region 2 mismatch");
      }
    }

    // --- 2) Diagonal contact: split with 4-connectivity, merged with 8 ---
    {
      bmp::Bitmap diag(9, 9);
      diag.clear(bmp::Black);
      diag.fill_rect(0, 0, 2, 2, bmp::White);
      diag.fill_rect(2, 2, 2, 2, bmp::White); // touches the first square only diagonally

      const auto regions4 = diag.extract_regions(128, bmp::Connectivity::Four);
      expect(regions4.size() == 2, "diagonal: 4-connectivity should give 2 regions");
      if (regions4.size() == 2) {
        expect(same_region(regions4[0], 0, 0, 2, 2, 4), "diagonal: 4-conn region 0 mismatch");
        expect(same_region(regions4[1], 2, 2, 2, 2, 4), "diagonal: 4-conn region 1 mismatch");
      }

      const auto regions8 = diag.extract_regions(128, bmp::Connectivity::Eight);
      expect(regions8.size() == 1, "diagonal: 8-connectivity should give 1 region");
      if (regions8.size() == 1) {
        expect(same_region(regions8[0], 0, 0, 4, 4, 8), "diagonal: 8-conn region box/area mismatch");
        expect(nearly_equal(regions8[0].centroid_x, 1.5) && nearly_equal(regions8[0].centroid_y, 1.5),
               "diagonal: 8-conn centroid mismatch");
      }
    }

    // --- 3) Noise filtering with min_area ---
    {
      bmp::Bitmap noisy(21, 11);
      noisy.clear(bmp::Black);
      noisy.fill_rect(2, 2, 4, 4, bmp::White); // the real icon
      noisy.set(10, 1, bmp::White);            // noise pixels
      noisy.set(15, 8, bmp::White);
      noisy.set(20, 0, bmp::White);

      const auto all = noisy.extract_regions(128, bmp::Connectivity::Eight, 1);
      expect(all.size() == 4, "noise: expected 4 regions without filtering");

      const auto filtered = noisy.extract_regions(128, bmp::Connectivity::Eight, 2);
      expect(filtered.size() == 1, "noise: min_area=2 should keep only the icon");
      if (filtered.size() == 1)
        expect(same_region(filtered[0], 2, 2, 4, 4, 16), "noise: filtered region mismatch");

      const auto none = noisy.extract_regions(128, bmp::Connectivity::Eight, 1000);
      expect(none.empty(), "noise: min_area larger than any region should give 0 regions");
    }

    // --- 4) Edge cases: empty, all black, all white, out-of-range thresholds ---
    {
      const bmp::Bitmap empty;
      expect(empty.extract_regions(128).empty(), "empty bitmap should give 0 regions");

      bmp::Bitmap black(5, 5);
      black.clear(bmp::Black);
      expect(black.extract_regions(128).empty(), "all-black: bright foreground should give 0 regions");

      const auto dark = black.extract_regions(128, bmp::Connectivity::Eight, 1, false);
      expect(dark.size() == 1 && same_region(dark[0], 0, 0, 5, 5, 25),
             "all-black: dark foreground should give 1 full-image region");

      bmp::Bitmap white(5, 5);
      white.clear(bmp::White);
      const auto bright = white.extract_regions(128);
      expect(bright.size() == 1 && same_region(bright[0], 0, 0, 5, 5, 25),
             "all-white: expected 1 full-image region");
      if (bright.size() == 1) {
        expect(nearly_equal(bright[0].centroid_x, 2.0) && nearly_equal(bright[0].centroid_y, 2.0),
               "all-white: centroid mismatch");
      }

      // Negative threshold clamps to 0: every pixel (luminance >= 0) is foreground
      const auto negative = black.extract_regions(-5);
      expect(negative.size() == 1 && same_region(negative[0], 0, 0, 5, 5, 25),
             "negative threshold should clamp to 0");

      // Threshold above 255 clamps to 255: black pixels stay background
      expect(black.extract_regions(300).empty(), "threshold > 255 should clamp to 255");
    }

    // --- 5) Many tiny regions: checkerboard of isolated pixels ---
    {
      bmp::Bitmap checker(16, 16);
      checker.clear(bmp::Black);
      for (std::int32_t y = 0; y < 16; ++y)
        for (std::int32_t x = 0; x < 16; ++x)
          if ((x + y) % 2 == 0)
            checker.set(x, y, bmp::White);

      // 4-connectivity: 128 isolated single-pixel regions
      const auto many = checker.extract_regions(128, bmp::Connectivity::Four);
      expect(many.size() == 128, "checkerboard: expected 128 regions with 4-connectivity");
      bool all_single = true;
      for (const auto &r : many)
        all_single = all_single && (r.area == 1) && (r.width == 1) && (r.height == 1);
      expect(all_single, "checkerboard: every 4-conn region should be a single pixel");

      // 8-connectivity: diagonally touching pixels merge into one region
      const auto merged = checker.extract_regions(128, bmp::Connectivity::Eight);
      expect(merged.size() == 1 && merged[0].area == 128,
             "checkerboard: expected 1 merged region with 8-connectivity");
    }

    if (g_failures == 0) {
      std::cout << "All connected region tests passed" << std::endl;
      return EXIT_SUCCESS;
    }
    std::cerr << g_failures << " connected region test(s) failed" << std::endl;
    return EXIT_FAILURE;
  } catch (const bmp::Exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
}
