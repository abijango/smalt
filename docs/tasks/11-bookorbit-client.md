# 11: BookOrbit client and mock server

**Wave 1.**

## Goal

A Qt Network client for the protocol BookOrbit's own KOReader plugin uses, plus a mock server so every other task can test without a real server.

## Spec sections

Decisions after verification, Sync engine (Networking, Order of a full sync), Feature design (Home screen and library, Large-library UX, Downloads). Endpoint details: `docs/VERIFY-FINDINGS.md` ("BookOrbit (KOReader plugin protocol)").

## Owns

`src/bookorbit/`, `tools/mock-bookorbit/`, `tests/bookorbit/`.

## Must not touch

`include/smalt/` (propose changes), everything else.

## Build

- Base `{server}/api/v1/koreader/`. Headers `x-auth-user` (username) and `x-auth-key` (MD5 of the password), read from `.adds/bookorbit/config.ini`. Plugin endpoints (camelCase) add `deviceId`, `deviceModel`, `pluginVersion` and `deviceTime` (local wall clock, `YYYY-MM-DD HH:MM:SS`) to every body; kosync endpoints use snake_case.
- **Capabilities:** fetch and cache the server's capability list and version; only call optional endpoints (for example dashboard sections) when advertised. Treat 4xx as a definite answer, 5xx and timeouts as unknown.
- **Book identity:** KOReader's partial MD5 of a file (port `util.partialMD5` exactly). `plugin/match-check` with hashes plus title, authors and `bookFileId` returns `bookId`, `bookFileId`, `positionCompatible` and `libraryVersion`.
- **Progress:** `GET syncs/progress/{digest}`, `PUT syncs/progress` (`document`, `percentage`, `progress`, `device`, `device_id`, `timestamp`); `plugin/progress` for bulk.
- **Sessions:** `plugin/page-stats` with per-book `{hash, bookFileId?, events: [{page, startTime, durationSeconds, totalPages}]}` in batches; store the acknowledged watermark. Then `plugin/sweeps` with counts after every sync that uploaded sessions; this tells the server the device measures its own sessions.
- **Highlights:** `plugin/annotations/exchange` and `exchange-ack` (one-way use: send local creates, edits and deletions; ignore incoming except for listing on the detail screen). Bookmarks endpoints are not used.
- **Status and ratings:** `plugin/catalog/books/{id}/read-status`, `…/rating`, `plugin/book-states`.
- **Catalog:** root, dashboard, discover, dashboard sections (up-next-in-series, want-to-read), sections (authors, series, collections, smart-scopes, libraries, shelves), books (`q`, `page`, `size`, `sort`, `order`, `seriesId`, `collectionId`, `ids`), book detail, manifest (cursor), file download with resume, thumbnails.
- Timeouts: 5 s connect, 15 s per request. Bundled CA certificates. Same-host redirects only, as the plugin does.
- Typed errors: offline, timeout, auth rejected, server error, bad response.
- Mock server (Python FastAPI) implementing these endpoints with fixtures, including slow responses, 401s, dropped connections and missing capabilities.

## Acceptance tests

- Partial MD5 matches KOReader's for a set of sample files.
- Every call against the mock: success, timeout, 401, 500, malformed JSON.
- Page-stats retries never double-count (watermark respected); a sweep follows every upload.
- Download resumes after a dropped connection.
- No call blocks the calling thread.
