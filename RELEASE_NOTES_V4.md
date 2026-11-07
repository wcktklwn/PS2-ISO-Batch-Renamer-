# PS2 Batch Renamer V4

## Changes after V4.1.0

The changes below are in the current source, on top of the V4.1.0 release
recorded further down. The application window still reads V4.1. The committed
project version in `CMakeLists.txt` is 4.2.0.

### ZSO and VCD

- ZSO files are read as ZISO images. Each block is restored with LZ4 in
  memory, or copied through when the index marks it uncompressed. The image
  stays in memory and is not written out as a temporary ISO.
- VCD files are read as POPStarter images. The 1 MiB header is skipped. The
  reader accepts 2,352-byte Mode 2 Form 1 sectors (user data at offset 24),
  2,352-byte Mode 1 sectors (user data at offset 16), and cooked 2,048-byte
  sectors, then uses the same identifier as an ISO.
- **Include game ID in filename** applies to `.iso`, `.chd`, `.zso`, and
  `.vcd`. An existing destination file is left in place and the row is
  reported as failed.
- Image readers are closed before a rename, so Windows does not keep a VCD
  locked.

### Auto and Open PS2 Loader folders

- **Auto** is the default image type in the window. It scans `.iso`, `.chd`,
  `.zso`, and `.vcd`. A caller that omits the image type still scans ISO only
  and renames in the file's own folder.
- The scan reads the selected folder and, unless that folder is already named
  `CD`, `DVD`, or `POPS`, its direct children with those names. It does not
  walk any deeper.
- **ISO**, **CHD**, **ZSO**, and **VCD** rename a file in the folder where it
  was found.
- **Auto** moves a PlayStation 2 CD into `CD/`, a PlayStation 2 DVD into
  `DVD/`, and a PlayStation disc into `POPS/` under the library root.
  Selecting one of those three folders uses its parent as the library root, so
  a disc is not placed in `CD/CD`.
- Auto creates the destination folder when it is missing and reuses an
  existing folder regardless of letter case. A file that is already at the
  destination path is skipped.
- Media comes from the embedded Redump catalog, `media_catalog.txt`
  (23,148 serials). The source dats are dated 2026-06-15: 11,774 PlayStation 2
  discs and 10,914 PlayStation discs. `C` is PlayStation 2 CD, `D` is
  PlayStation 2 DVD, and `P` is PlayStation. Bin/cue dumps are CD and ISO dumps
  are DVD, the same split as the Redump CD and DVD media lists. The catalog is
  separate from `gameid.txt`.
- A PlayStation boot path goes to `POPS` even when that serial is also a
  PlayStation 2 disc. A serial with a tied CD/DVD vote is omitted. A serial
  missing from the catalog stays in its current folder when the image is small
  enough to be either format. A CHD with CD track metadata is a CD. A CHD
  without that metadata is a DVD. Any other image whose user data is larger
  than an 80-minute CD (737,280,000 bytes) is a DVD.

### Database

- The built-in `gameid.txt` now contains **24,532** unique records. The
  V4.1.0 audit figure of 13,442, and the corrections listed below, are
  unchanged. The later growth is the Redump PlayStation catalog plus 55
  on-disc rows.
- An ID is 11 characters with the dot in the same place: `AAAA_BBB.CC`, or an
  on-disc product code such as `LSP06010.10`. The first matching line still
  wins. `^!` in a stored title still becomes `!` in the filename.
- PlayStation serials imported from Redump are a 4-character prefix plus
  exactly five letters or digits. Six-digit Lightspan codes and other lengths
  were skipped by that import and added afterwards in the 11-character
  product-code form.
- Those 55 rows, and the PlayStation catalog, are in the embedded database.
  **Refresh online** replaces the session database with GitHub `main/gameid.txt`.
  Until that published file includes the new rows, **Reset to built-in** is
  what makes them active. Serials that were already in the published database
  resolve from either copy once the image can be read.
- The 55 added records are the Lightspan on-disc codes for Googol, Str.at.e.s.,
  Timeless Math, Quaddle Family Mysteries, Write Away, Three Decoders, Unknown
  Variable, Story Lane Theater, Science Is Elementary, and Timeless Jade
  Trade, plus:
  - `SCUS_941.93` — **PlayStation-Pepsi Sampler Disc (USA)**
  - `SCUS_944.22` — **Star Ocean - The Second Story (USA) (Disc 2)**
  - `SLUS_808.89` — **Pong - The Next Level (USA) (Trade Demo)** (retail
    `SLUS_008.89` is unchanged)
  - `PSRM0171.00` — **Syphon Filter 2 (USA) (Disc 2) (Beta) (2000-01-11)**

### Identification

- A PlayStation 2 `0:\` ID is still the next 11 bytes, copied byte-exact,
  including mixed case. That path stays PlayStation 2.
- When `0:\` is absent, the PlayStation `BOOT` or `BOOT2` line is uppercased.
  `cdrom:`, `cdrom:\`, `cdrom:\\`, and `cdrom:/` are accepted, including a
  directory in front of the executable. A hyphen (`SLUS-000.05`) and a compact
  serial (`SCUS943.93`) normalize to `AAAA_BBB.CC`. An 11-character product
  code that starts with a letter and already has the dot in place, such as
  `LSP06010.10` or `PSRM0171.00`, is kept as the key.
- A boot file that is only an executable name, such as `PSX.EXE`, uses the
  root filename when that directory contains exactly one serial-like name.
  Two serials in that directory stay failed.
- A disc with no `SYSTEM.CNF` uses the root filename when that directory
  contains exactly one serial-like name. That fallback leaves the PlayStation
  flag clear, and Auto then follows the media catalog.
- A boot token that is not 11 characters, such as `907127.001`, is outside
  this ID format and stays unresolved.

### Sort files

- **Sort files** sits beside **Include game ID in filename**. It is on by
  default. The scan is ordered from A to Z by filename, then by full path.
- With the box off, the scan keeps directory order: the selected folder, then
  its one-level `CD`, `DVD`, and `POPS` children.
- Changing the checkbox clears the current preview. Scan again before
  applying. Results are not reordered while a scan is running.

### Windows and continuous integration

- On Windows the window is drawn with SDL's Direct3D 11, Direct3D 12, or
  software renderer, in that order. Windows Server 2022 can open the app
  without a desktop OpenGL driver. A startup failure shows a message box.
  macOS and Linux continue to use an OpenGL 3.2 core context.
- Windows builds define `NOMINMAX` and `WIN32_LEAN_AND_MEAN` before
  `windows.h`, so `std::min` and `std::max` compile under MSVC.
- GitHub Actions uses `actions/checkout@v7`, `actions/upload-artifact@v7`, and
  `actions/download-artifact@v8`. The Windows job runs on `windows-2022`.
  Those action versions run on Node.js 24.

## V4.1.0

V4.1.0 adds a validated online-first database with an embedded offline fallback
and replaces the historical title list with a reproducible, conservative audit.
The original V3 disc-identification algorithm remains unchanged.

## Online database with safe fallback

- The app checks the repository's current `main/gameid.txt` automatically at
  startup without blocking folder selection or scanning.
- A download is accepted only after the entire database parses successfully,
  contains at least 10,000 unique records, and remains below the 4 MiB limit.
- HTTP errors, timeouts, duplicate IDs, malformed records, or a partial file
  leave the embedded database active and produce a visible status message.
- **Refresh online**, **Choose local gameid.txt...**, and **Reset to built-in**
  controls make the active source explicit.
- No downloaded database is written to the user's machine.

## V4.1 database audit

The historical database was compared against pinned snapshots of GameDB-PS2,
the GDX English TitlesDB, the PCSX2 GameIndex, and the Get_CONFIG PS2 index. The
generated database contains:

- **13,442 records**
- **13,442 unique canonical game IDs**
- **0 duplicate IDs**
- **0 Windows-unsafe titles**
- **2,041 missing primary-catalogue IDs added**
- **2,339 unsupported prefix-shadow records removed**
- **1,780 conservative automatic title corrections**
- **4,087 unresolved entries retained in a manual review queue**

Important reviewed corrections include:

- `SLES_511.68` — **AFL Live 2003**
- `SLES_518.26` — **AFL Live 2004**
- `SLES_519.03` — **AFL Live 2004 - Aussie Rules Football**
- `SLES_504.87` — **Looney Tunes - Space Race**
- `SLPM_620.89` — **Wacky Races Starring Dastardly & Muttley
  [Cancelled Japanese Release]**
- `SLPM_650.49` — **Beatmania II DX 5th Style - New Songs Collection**
- `SLPM_650.52` — **Gitadora! GuitarFreaks 4th Mix & DrumMania 3rd Mix**
- `SLKA_253.23` — **FIFA 06**
- `SLPM_602.65` — **Steamboy [Trial]**

The complete methodology and source revisions are in
[`docs/database-audit-v4.1.0.md`](docs/database-audit-v4.1.0.md). Every change
is in [`docs/database-changes-v4.1.0.tsv`](docs/database-changes-v4.1.0.tsv),
and unresolved catalogue disagreements are retained in
[`docs/database-review-v4.1.0.tsv`](docs/database-review-v4.1.0.tsv).

## Unofficial English translations

`[English Translation]` is appended only when evidence shows that a fan patch
changes the executable game ID. Same-ID patches and generic loader IDs are not
labelled because the ID alone cannot identify a particular translation.

V4.1.0 includes the verified patched ID `SCES_609.71` as **Kingdom Hearts II -
Final Mix + [English Translation]**. Its retail ID `SLPM_666.75` remains a
separate, unsuffixed record.

## Reliability and portability

- Strict database parsing now rejects malformed IDs with the physical line
  number while retaining legitimate alphanumeric product IDs such as
  `KOEI_SP0.02`.
- Linux uses libcurl for HTTPS; Windows uses the built-in WinHTTP API.
- Third-party source snapshots are pinned by both commit and SHA-256; release
  builds retry cleanly after an interrupted or failed dependency download.
- The online worker is independent of the scan/rename worker. A validated
  update waits until active file work finishes before replacing the in-memory
  database.
- Regression tests cover the reviewed titles, translation/retail pair, strict
  ID parsing, duplicate and incomplete downloads, oversize protection, and the
  offline fallback build.

## Downloads

- **Windows:** `PS2-Batch-Renamer-V4.exe`
- **Linux:** `PS2-Batch-Renamer-V4-Linux-x86_64.AppImage`
- **Standalone/legacy database:** `gameid.txt`
- **Integrity:** `SHA256SUMS.txt`

On Linux:

```bash
chmod +x PS2-Batch-Renamer-V4-Linux-x86_64.AppImage
./PS2-Batch-Renamer-V4-Linux-x86_64.AppImage
```

The selected game folder must be writable. A dual-boot NTFS volume left
hibernated by Windows or mounted read-only by `fuseblk` cannot be renamed until
it is cleanly remounted read/write.

## Preserved behavior

The proven handwritten V3 identification sequence and local external-database
first-match behavior are preserved. V4.1.0 changes database selection,
validation, catalogue quality, and release tooling—not the disc-identification
algorithm.

The earlier programs and their source remain archived under `legacy/`.
