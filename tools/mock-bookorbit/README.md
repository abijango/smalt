# Mock BookOrbit server

Owned by task 11 (`docs/tasks/11-bookorbit-client.md`). A Python FastAPI server that speaks the KOReader plugin protocol Smalt uses (`/api/v1/koreader/...`), so every task can test against it without a real BookOrbit.

```bash
python3 -m pip install -r tools/mock-bookorbit/requirements.txt
cd tools/mock-bookorbit
python3 -m mock_bookorbit --port 8765         # prints "listening 8765"
python3 -m mock_bookorbit --port 0            # a free port; the line says which
python3 -m pytest tests                       # the mock's own tests
```

The C++ tests under `tests/bookorbit/` start it themselves on a free port and stop it at the end (`tests/bookorbit/mock_server.h`). Set `SMALT_PYTHON` to use a particular interpreter.

## What it serves

Fake credentials: user `test-reader`, password `fake-password` (the client sends its MD5 as `x-auth-key`). Four fixture books with deterministic file bytes: files 101 and 102 are EPUBs (a series, "Salt Trilogy"), 103 a 1.2 MB CBZ, 104 a PDF. Each file's KOReader partial MD5 is computed at start-up, so `match-check` works on a file the client just downloaded.

| Area | Endpoints |
| --- | --- |
| kosync | `GET users/auth`, `GET syncs/progress/{digest}`, `PUT syncs/progress` |
| Plugin | `GET plugin/version` (capabilities), `POST plugin/match-check`, `page-stats`, `sweeps`, `progress`, `book-states`, `annotations/exchange`, `annotations/exchange-ack` |
| Catalog | `plugin/catalog/root`, `dashboard`, `dashboard/discover`, `dashboard/sections/{type}`, `sections/{kind}`, `books`, `books/{id}`, `manifest`, `files/{id}/download` (with `Range`), `books/{id}/thumbnail`, `PUT books/{id}/read-status`, `PUT books/{id}/rating` |

Request validation mirrors BookOrbit's own (hash patterns, 500 events and 50 books per page-stats request, rating and status values, ...), so a client that sends something the real server would refuse gets a 400 here too. Page-stats events are de-duplicated by `(startTime, page)`; the reply carries `accepted`, `duplicates` and `watermark` per book. Where the protocol notes don't pin a detail down (the dedup key, the manifest cursor, the subset of fields in a book-states result) the code says "mock choice".

Sources for every shape are named at the top of `mock_bookorbit/app.py`.

## Test control

None of this is part of the protocol, and none of it needs credentials.

| Request | Does |
| --- | --- |
| `POST /__mock/reset` | Forget everything and go back to the defaults |
| `POST /__mock/config` | `{"capabilities": [...] \| null, "serverVersion": "...", "ranges": true, "rules": [...]}`. `null` capabilities means an old server whose `plugin/version` answers 404; a list restricts what `plugin/version` advertises, and the routes behind a missing capability answer 404 |
| `GET /__mock/requests[?clear=1]` | Every protocol request seen: method, path, query, key headers, body |
| `GET /__mock/state` | What the mock stored (page events per book, sweeps, progress, statuses, file sizes and digests) |

A rule changes how matching requests are answered:

```json
{"path": "/plugin/page-stats", "method": "POST",
 "skip": 0, "times": 1,
 "delay_ms": 2000,
 "status": 500, "body": {"message": "boom"},
 "raw_body": "{not json", "content_type": "application/json",
 "drop": true,
 "drop_after_bytes": 50000, "chunk_delay_ms": 2,
 "after_processing": true}
```

`skip` lets that many matching requests through first; `times` limits how many are affected (default: all). `status`/`body`/`raw_body` replace the answer, `delay_ms` makes it slow, `drop` sends headers and part of a body and then cuts the connection, `drop_after_bytes` does the same for a download, and `after_processing` runs the real handler first and then replaces the answer (a reply lost after the server stored the data).

Connections that die before any response, servers that never answer and redirects to other hosts are done with a local `QTcpServer` in the C++ tests instead (`tests/bookorbit/fake_http_server.h`).
