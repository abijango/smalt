# 11: BookOrbit API client and mock server

**Wave 1.**

## Goal

A Qt Network client for BookOrbit's REST API using an API key, plus a mock server so every other task can test without a real server.

## Spec sections

Sync engine (Networking, Order of a full sync), Feature design (Home screen and library, Large-library UX, Downloads), Verify first.

## Owns

`src/bookorbit/`, `tools/mock-bookorbit/`, `tests/bookorbit/`.

## Must not touch

`include/smalt/` (propose changes), everything else.

## Build

- If `docs/bookorbit-openapi.json` exists, generate or hand-write types from it. If not, write the client against the endpoint list in `docs/VERIFY.md` item 6, and isolate each endpoint behind one function so it's easy to correct.
- API key from `.adds/bookorbit/config.ini`, sent as the server expects.
- Timeouts: 5 s connect, 15 s per request. Bundled CA certificates, not the system store.
- Calls: library (paged, filters: format, status, on-device), series list (paged), series detail, search (grouped), book detail, cover thumbnail, file download with resume (HTTP Range), push sessions (batch), push and pull progress, push and pull annotations, push and pull status and rating, collections, change feed since cursor.
- Typed errors: offline, timeout, auth rejected, server error, bad response.
- Fallback path: if API keys can't write sessions (VERIFY item 6), a KOReader-sync-protocol uploader behind the same interface.
- Mock server (Python FastAPI or Qt-based) implementing the same endpoints with fixtures, including slow responses, 401s and dropped connections.

## Acceptance tests

- Every call against the mock: success, timeout, 401, 500, malformed JSON.
- Download resumes after a dropped connection.
- No call blocks the calling thread.
