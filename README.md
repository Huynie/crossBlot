# CrossBlot

CrossBlot is a personal e-reader firmware for the **Xteink X3** (it also runs on the X4, which shares the same binary).

It started as an experiment in combining the parts I liked most from several community firmwares in the CrossPoint family into one build:

- the lightweight core and working network features of one,
- the library and home-screen polish of another,
- the cover carousel and button layout of two more.

CrossBlot is built on **CrossInk v1.6.0**. Pieces from the other projects were ported on top of it, adapted, and in places reworked.

None of this would exist without the people below. Every feature listed under "Where everything came from" is their work first.

---

## Where everything came from

### CrossPoint Reader: the original firmware
[crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader), by Dave Allie and contributors.

This is the open-source firmware that every project below descends from:
- EPUB rendering engine
- reader
- file browser
- settings system
- display and input drivers for the Xteink devices

### CrossInk: the base
[uxjulia/CrossInk](https://github.com/uxjulia/CrossInk), v1.6.0.

CrossBlot is a fork of CrossInk, so everything CrossInk does, CrossBlot does too. In particular:
- **OPDS catalog browsing and downloads.** Works with Calibre / Calibre-Web, Kavita / Komga, and public catalogs.
- **Web file transfer**: the browser-based upload, file manager, settings and fonts portal.
- A lean, stable reader core that is careful with memory on the ESP32-C3.
- Reader fonts (Lexend Deca and Bitter), font sizes, and SD-card fonts.
- Reading stats, bookmarks, Focus Reading / Guide Dots, finished-book tracking.
- Reader button remapping and the in-book quick menu.
- Calibre wireless connect, WebDAV, USB transfer, and OTA updates.
- The Lyra, Classic, Minimal and Dashboard themes.

### CrumBLE: library and home-screen look
[imshentastic/CrumBLE](https://github.com/imshentastic/CrumBLE), v4.7.1.

CrumBLE's library features and visual style were ported onto the CrossInk base:
- **Collections.** Create, rename, rearrange and sort collections, and add or remove books. This includes the collection store, library index and series detection.
- **Bookshelf grid**: the 2×2 / 3×3 / 4×4 cover grid for browsing a collection.
- **The Flow home theme**: a cover carousel with a collections shelf, CrumBLE's home icon bar and menu layout, and its perspective cover-tile cache.
- **Black selection highlights** in lists and pop-up menus.
- **Sleep-screen cycling**: tap the power button while asleep to cycle to the next sleep image.
- The collection pickers and menus: add books, the bookshelf picker, sort picker, and rearrange screen.

CrumBLE's own OPDS client and web uploader were deliberately *not* used; CrossInk's versions are used instead.

### Duet: the five-cover carousel
[lauren-alexandra/duet-xteink](https://github.com/lauren-alexandra/duet-xteink), by Lauren Landau.

- The **five-cover carousel** geometry: a large centered cover with a selection ring, and two smaller perspective covers on each side. CrossBlot's Collection Carousel theme is built on it.

### CrossInk Carousel
[chintanvajariya/CrossInk-Carousel](https://github.com/chintanvajariya/CrossInk-Carousel).

- The original multi-cover carousel home that Duet's carousel builds on.

### TenorCross: button mapping
[TenorGroup/cross](https://github.com/TenorGroup/cross-releases) ([cross.tenor.vn](https://cross.tenor.vn/en/tai.html)).

- The **side-button navigation scheme**:
  - On Home and the bookshelf, the side buttons move left and right, and the front buttons move up and down.
  - In Settings, the side buttons step between tabs and the front buttons step through rows.
- CrossBlot re-implements this as an input-axis swap rather than merging Tenor's code. Toggle it under **Settings → Controls → Side-Button Navigation**.

---

## What CrossBlot adds

These are new in CrossBlot, written while gluing the pieces above together:

- **Collection Carousel home theme** (the default):
  - Browse your collections as a five-cover carousel. The collection name has arrows and an "n of N" counter, and you can scroll through the whole collection, not just five books.
  - The centered book shows a borderless progress bar, with time read on the left and percent on the right, then the title and author.
- **Add to collection from the file browser**, for EPUB, XTC/XTCH, TXT and Markdown files.
- **Manga (XTC/XTCH) thumbnails**:
  - White page margins are trimmed so covers fill their frame.
  - Thumbnails stream from the file, so large manga no longer come out blank.
- **Low-memory handling**:
  - When the heap is fragmented after browsing the library, opening a book does a quick silent restart.
  - The XTC reader retries once after a memory error instead of failing.
- **Collections save reliably**: writes are deferred and debounced, and are always flushed before sleep, restart, or leaving the library.
- **Interface touches**:
  - The clock sits in the top-left of the home screen.
  - With the Flow and Collection Carousel themes, Settings uses CrumBLE's layout: a list of categories that each open their own page, with black row highlights.
  - Pop-ups use black highlights.
- **CrossBlot branding**: a boot screen logo (see [Boot screen image](#boot-screen-image)), the web portal, and the device and hotspot names (`CrossBlot-Reader`, `http://crossblot.local/`).

---

## Boot screen image

The logo on the boot screen (and the web portal) is a **temporary placeholder** until an official CrossBlot image is made.

It's based on [**"Miau" by Alexandr Sidorovich**](https://dribbble.com/shots/1636544-Miau) on Dribbble. The artwork was converted to 1-bit black and white and resized for the e-ink display. All credit for the original illustration goes to Alexandr Sidorovich.

Where it lives:
- Source image: `assets/crossblot-logo.png`
- Firmware bitmap: `src/images/CrossblotLogo.h`
- Web portal logo: `web/assets/logo.png`

---

## Installation

1. Download `crossBlot-x3-x4-vX.Y.Z.bin` from this repo's [Releases](https://github.com/Huynie/crossBlot/releases).
2. Flash it over USB, either with a web ESP flasher or with `esptool`. The X3 and X4 use the same binary.
3. On first boot, the home screen uses the **Collection Carousel** theme. You can change it under **Settings → Display → Theme**.

**OTA updates:** these check this repo's latest GitHub release. Releases need to be tagged with a version (e.g. `v1.1.0`), and the firmware file must be named `crossBlot-x3-x4.bin` or `crossBlot-x3-x4-<anything>.bin` (e.g. `crossBlot-x3-x4-v1.1.0.bin`).

**Coming from CrossInk or CrumBLE:** your SD card's `/.crosspoint` data is kept, including settings, progress, stats and collections. CrumBLE settings are migrated on first boot.

---

## Building

CrossBlot uses PlatformIO.

```sh
git submodule update --init --recursive
pio run -e default                    # build for the X3 / X4
pio run -e default --target upload    # build and flash a connected device
```

To build a release with a clean version string:

```sh
CROSSINK_RELEASE_VERSION=1.0.0 pio run -e default
```

The firmware is written to `.pio/build/default/firmware.bin`.

### Desktop simulator

```sh
pio run -e simulator-X3 -t run_simulator
```

- Set `CROSSPOINT_SIM_SD` to a folder to use it as the SD card. Otherwise the simulator uses `fs_/` in the repo.
- Keys:

  | Key | Action |
  |---|---|
  | Arrow keys | Navigate |
  | Return | Confirm |
  | Esc | Back |
  | P | Power |

See [docs/development](./docs/development) for the inherited CrossInk developer docs.

---

## License

- CrossBlot is released under the [MIT License](./LICENSE), the same as CrossPoint Reader, CrossInk, CrumBLE, Duet and the other projects it draws from. Their copyright notices are kept.
- The build bundles **wolfSSL** (`wolfssl/Arduino-wolfSSL`), which is licensed under the **GPL**. Compiled firmware binaries that include it are therefore covered by the GPL. If you redistribute binaries, distribute them under those terms.

CrossBlot is a personal project. It isn't affiliated with Xteink or with any of the upstream projects. Report CrossBlot issues here, not to the upstream authors.
