# Importing projects from MAXQDA and NVivo

Tessera reads only open, documented formats. Native project files of other programs are recognised
(by content, not only by extension) and explained, but not converted.

## Supported

- REFI-QDA projects (`.qdpx`): documents, codes, codings, memos, variables, sets, links, PDF and image sources.
- REFI-QDA codebooks (`.qdc`): the code tree (names, colors, descriptions) only.

Both MAXQDA (Project > Export > REFI-QDA project / codebook) and NVivo (File > Export > Project or
Codebook, REFI-QDA format) can produce these files. Use `File > Import > QDPX / codebook`.

## Not supported (and why)

- MAXQDA `.mqda`, `.mx24`, `.mx22`, `.mx20`: a SQLite database (detected through its `sys_Setup` table).
  Issue #51 lists the table names, but the meaning of the code positions, the RTF blob encoding,
  colors and variable types has not been verified against real files, and no vendor sample may be
  committed. Converting without that verification would silently misplace codings, so the file is
  refused with instructions to export REFI-QDA. Detection opens the file read-only and immutable
  (`mode=ro&immutable=1`); the file is never modified.
- NVivo `.nvp` (Windows) and `.nvpx`: proprietary and undocumented. Same handling.

A native reader can be added once a documented schema or user-provided test files built for the purpose exist.