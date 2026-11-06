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
5. Multiply the extent by 2,048 and read the first 64 bytes of `SYSTEM.CNF`.
6. Locate the first byte sequence `30 3A 5C` (`0:\`).
7. Read the following 11 bytes as the game ID.
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

PlayStation discs used by POPStarter store the serial after `cdrom:\` rather
than `cdrom0:\`. Some images double the backslash or use mixed case
(`Slus_005.98`). When `0:\` is absent, the 11 bytes after `:\` are read,
extra backslashes are skipped, and the serial is uppercased. That path sets
`Identification::playstation1`. A PlayStation 2 `cdrom0:\` serial is still
taken from the original `0:\` match and does not set the flag.

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
- Auto placement into `CD`, `DVD`, and `POPS`, including subdirectory scan;
- the bundled example ZSO and VCD images when `examples/` is present.
