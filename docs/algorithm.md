# V4 algorithm parity notes

V4 deliberately preserves the successful identifier and database lookup
behavior of V3 and the `GameName (game-id)` variant. The desktop interface,
error handling, and CHD input path are new; the identification sequence and
generated names are not redesigned.

## Disc identification sequence

1. Read two bytes at disc offset `0x80A4` as a big-endian root-directory
   sector number.
2. Multiply that value by the 2,048-byte disc sector size.
3. Read the 2,048-byte root directory and locate the first `EM.CNF` byte
   sequence.
4. Move back 31 bytes from that match and read the four-byte big-endian
   `SYSTEM.CNF` extent.
5. Multiply the extent by 2,048 and read one sector of `SYSTEM.CNF`. When that
   read does not fit, the first 64 bytes are used instead.
6. Locate the first byte sequence `30 3A 5C` (`0:\`). When it is present, read
   the following 11 bytes as the game ID and leave them byte-exact. That is
   the PlayStation 2 path, and it does not mark the disc as PlayStation.
7. When `0:\` is absent, read the PlayStation `BOOT` line. A missing backslash
   (`cdrom:SLUS_001.52`), a forward slash, a directory
   (`EXE\SCUS_946.08`, `TEKKEN3\SLUS_004.02`), a hyphen (`SLUS-000.05`), and a
   compact name (`SCUS943.93`) all normalize to `AAAA_BBB.CC`. An on-disc
   product code that is already 11 characters with the dot in the same place,
   such as `LSP06010.10`, is kept as the database key. If the boot file is
   `PSX.EXE` or another name without a serial, and the root directory contains
   exactly one serial-like filename, that filename is used.
8. Search the database from top to bottom and use the first line whose first
   11 characters match the extracted ID.

For CHD input, libchdr exposes the cooked 2,048-byte data sectors directly to
the same identifier. V4 does not extract the complete image or invoke
`chdman.exe`, `ps2_chd.exe`, or `chd_ps2_renamer.exe`.

For ZSO input, the reader follows the ZISO index. The high bit of an index
entry marks an uncompressed block. Otherwise the block is raw LZ4, which is
decompressed to the original ISO bytes. Index alignment shifts, used when the
source ISO is larger than 2 GiB, are applied. The identifier then sees the
same byte stream as the source ISO. The image is not expanded onto disk.

For VCD input, the 1 MiB POPStarter header is skipped. The reader looks for
the ISO 9660 `CD001` descriptor and accepts 2,352-byte Mode 2 Form 1 sectors
(user data at offset 24), 2,352-byte Mode 1 sectors (user data at offset 16),
or cooked 2,048-byte sectors. Cooked 2,048-byte sectors are exposed to the
same identifier.

PlayStation discs used by POPStarter store the serial on the `BOOT` line.
The line may use `cdrom:\`, `cdrom:\\`, `cdrom:/`, or `cdrom:` with no slash,
and the executable may sit in a directory or use a hyphen or compact serial.
Those forms are normalized and uppercased, and the path sets
`Identification::playstation1`. A PlayStation 2 `cdrom0:\` serial is still
taken from the original `0:\` match, stays byte-exact, and does not set the
flag. A root-directory serial used because `SYSTEM.CNF` itself is missing
does not set the flag either; Auto then follows the media catalog.

## Auto placement

Auto scans `.iso`, `.chd`, `.zso`, and `.vcd` in the selected folder and in
direct `CD`, `DVD`, and `POPS` children. The result keeps the concrete image
type so the renamed file keeps its extension. Other image types still rename
inside the file's own directory and only accept their own extension.

In Auto, a PlayStation boot path or a PlayStation serial from
`media_catalog.txt` goes to `POPS`. A PlayStation 2 serial goes to `CD` or
`DVD` from that same catalog. The catalog is the Redump PS2 dat's bin/cue
versus iso split, which matches the Redump CD and DVD media lists, plus the
Redump PlayStation serial set. It is not stored in `gameid.txt`.

When a serial is absent, CD track metadata makes a CHD a CD and a CHD without
that metadata is a DVD. Any other image whose user data exceeds 737,280,000
bytes is a DVD. A smaller unknown image stays in its current folder. Auto
creates `CD`, `DVD`, or `POPS` when it needs to, reuses an existing folder
regardless of case, and skips a file that is already at the destination path.

## Filename parity

| Option | Result |
|---|---|
| Include game ID off | `Database Title` plus `.iso`, `.chd`, `.zso`, or `.vcd` |
| Include game ID on | `Database Title (GAME_ID)` plus the source extension |

When a database is loaded, V4 decodes the legacy batch-file escape `^!` as
`!` and removes trailing spaces or tabs used as line padding. These are storage
artifacts from the previous shell pipeline, not game-title characters. Apart
from those compatibility normalizations, V4 does not silently rewrite a title.
A title containing a character that Windows cannot use in a filename is shown
as a failed preview item on both platforms.

## Regression coverage

The automated core tests verify:

- the original root-directory and `SYSTEM.CNF` offsets;
- exact 11-byte game-ID extraction;
- first-match behavior for duplicate database IDs;
- removal of legacy `^!` escaping and trailing database padding;
- both title-only and title-plus-ID naming;
- malformed images and missing IDs remaining visible as failures;
- destinations never being overwritten;
- no automatic `failed.txt` or `PS2_iso_files.txt` output;
- an ISO-to-CHD round trip when `chdman` is available in the test environment;
- plain and LZ4 ZSO blocks, including an alignment shift;
- VCD Mode 2/2352 cooked sectors;
- POPStarter `cdrom:\` serials, doubled backslashes, and mixed case;
- PlayStation boot paths with no backslash, a directory, a hyphen, a compact
  serial, a product code, and a single root-directory serial;
- Auto placement into `CD`, `DVD`, and `POPS`, including subdirectory scan;
- the bundled example ZSO and VCD images when `examples/` is present.
