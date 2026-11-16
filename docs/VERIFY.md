# On-device spike

**Items 1–9 are answered in `docs/VERIFY-FINDINGS.md`.** Only item 10 remains, after task 30.

Do this yourself, on the Kobo and against your BookOrbit server, while the scaffold and wave 1 run in the cloud. Wave 2 depends on the answers. Record each answer under its item, then commit this file.

Before starting: back up the whole Kobo user partition, and note the exact Kobalt version you install.

## Kobo and Kobalt

1. **Kobalt sessions in Nickel's database.** Install `Kobalt-qt5.zip`, read for a few minutes in Kobalt, close the book, connect USB, copy `.kobo/KoboReader.sqlite`. Check the event and analytics tables for session start and end rows (event types 1020 and 1021) for that book.
   - Answer:
2. **Position written by Kobalt.** Note the `content` row for the book (`ChapterIDBookmarked`, `___PercentRead`, `DateLastRead`) before and after reading. Then sleep the device with the book open and check whether the row updates without closing the book.
   - Answer:
3. **Jump takes effect.** With the book closed, change its position in the database (on a copy you then put back), open it in Kobalt, and see whether Kobalt starts at the new position.
   - Answer:
4. **Where Kobalt keeps highlights.** Make a highlight in Kobalt. Check Nickel's `Bookmark` table, then look for files under `.adds/` that changed.
   - Answer:
5. **PDF and comic progress.** Read a PDF and a CBZ in Kobo's built-in reader. Check that their `content` rows and events update like Kobalt's.
   - Answer:

## BookOrbit API (3.8.0+)

6. **Endpoints and API keys.** Find the API docs or OpenAPI spec on your server. Save the spec as `docs/bookorbit-openapi.json` if one exists. List which endpoints accept an API key, especially any that write reading sessions or progress.
   - Answer:
7. **Position format.** Read an EPUB on the web reader or phone, then fetch its progress through the API. Record the exact format (CFI, XPointer, percentage).
   - Answer:
8. **Change feed.** Is there an endpoint for changes since a cursor or timestamp, including deleted books?
   - Answer:
9. **Search, series and filters.** Check for server-side search, a paged series list, and filters by format and status.
   - Answer:

## Leave for wave 3

10. **Home screen takeover** is tested after task 30 lands; nothing to do now.
