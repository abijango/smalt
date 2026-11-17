"""Mock BookOrbit server: the KOReader plugin protocol Smalt uses.

Endpoint paths, request fields and response shapes come from
docs/VERIFY-FINDINGS.md and, where that is silent, from the public BookOrbit
repository at commit c77966cc529423e993fae8a298aa77048b43bba2:

- server/src/modules/koreader/koreader-plugin.controller.ts (plugin routes)
- server/src/modules/koreader/koreader-catalog.controller.ts (catalog routes)
- server/src/modules/koreader/koreader.controller.ts (kosync routes)
- server/src/modules/koreader/dto/*.ts (request validation, mirrored below)
- server/src/modules/koreader/koreader-stats.service.ts (page-stats reply)
- server/src/modules/koreader/koreader-plugin.service.ts (match, sweep, ...)
- server/src/modules/koreader/koreader-package.service.ts (capabilities)

Whatever those files don't pin down is marked "mock choice" below.

Test control (not part of the protocol, never behind auth):

    POST /__mock/reset              forget everything, restore defaults
    POST /__mock/config             {capabilities, serverVersion, ranges, rules}
    GET  /__mock/requests[?clear=1] every protocol request seen so far
    GET  /__mock/state              what the mock stored (page stats, sweeps, ...)
    GET  /__mock/ready              "ok"

A rule changes how matching requests are answered:

    {"path": "/plugin/page-stats", "method": "POST",   # path is after /api/v1/koreader
     "skip": 0, "times": 1,                              # let `skip` through, then apply `times` times (null = always)
     "delay_ms": 0,                                      # sleep before answering
     "status": 500, "body": {...} | "raw_body": "...",   # answer with this instead
     "content_type": "application/json",
     "drop": true,                                       # send headers and part of the body, then cut the connection
     "drop_after_bytes": 50000,                          # downloads: cut after this many body bytes
     "chunk_delay_ms": 2,                                # downloads: pause between 16 KiB chunks
     "after_processing": true}                           # run the real handler first (a lost reply)
"""
import asyncio
import hashlib
import json
import re
import time
from typing import Annotated, Any, List, Optional

from fastapi import APIRouter, Depends, FastAPI, Request, Response
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse, StreamingResponse
from pydantic import BaseModel, ConfigDict, Field

from . import fixtures

PREFIX = "/api/v1/koreader"

HEX32 = r"^[0-9a-fA-F]{32}$"
DEVICE_ID = r"^[A-Za-z0-9_-]{1,100}$"
DEVICE_DATETIME = r"^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$"

SETTABLE_READ_STATUSES = ["unread", "want_to_read", "reading", "on_hold", "read", "abandoned"]
BOOK_STATE_STATUSES = ["reading", "complete", "abandoned"]
CATALOG_SORTS = ["title", "author", "published", "recently_added", "recently_updated", "recently_read", "series"]
SECTION_KINDS = ["libraries", "collections", "smart-scopes", "authors", "series", "shelves"]
DASHBOARD_SECTION_TYPES = ["random", "recently-added", "want-to-read", "up-next-in-series", "smart-scope"]

MAX_PAGE_STATS_EVENTS = 500
MAX_ANNOTATION_CHANGES = 50


# --- Request models, mirroring server/src/modules/koreader/dto/*.ts ---------

class Loose(BaseModel):
    model_config = ConfigDict(extra="allow")


class Device(Loose):
    deviceId: str = Field(pattern=DEVICE_ID)
    deviceModel: str = Field(max_length=100)
    pluginVersion: str = Field(max_length=20)
    deviceTime: Optional[str] = Field(default=None, pattern=DEVICE_DATETIME)


class MatchBook(Loose):
    hash: str = Field(pattern=HEX32)
    title: Optional[str] = Field(default=None, max_length=500)
    authors: Optional[str] = Field(default=None, max_length=1000)
    lastOpen: Optional[int] = Field(default=None, ge=0)
    source: Optional[str] = Field(default=None, pattern=r"^(current_file|file|statistics)$")
    bookFileId: Optional[int] = Field(default=None, ge=1)
    metadataAmbiguous: Optional[bool] = None


class MatchCheck(Device):
    hashes: List[Annotated[str, Field(pattern=HEX32)]] = Field(min_length=1, max_length=500)
    books: Optional[List[MatchBook]] = Field(default=None, max_length=500)


class PageEvent(Loose):
    page: int = Field(ge=0)
    startTime: int = Field(ge=1)
    durationSeconds: int = Field(ge=0, le=86400)
    totalPages: int = Field(ge=1)


class PageStatsBook(Loose):
    hash: str = Field(pattern=HEX32)
    bookFileId: Optional[int] = Field(default=None, ge=1)
    events: List[PageEvent] = Field(min_length=1)


class PageStats(Device):
    books: List[PageStatsBook] = Field(min_length=1, max_length=50)


class Annotation(Loose):
    datetime: str = Field(pattern=DEVICE_DATETIME)
    datetimeUpdated: Optional[str] = Field(default=None, pattern=DEVICE_DATETIME)
    drawer: str = Field(pattern=r"^(lighten|underscore|strikeout|invert)$")
    color: Optional[str] = Field(default=None, max_length=30)
    text: Optional[str] = Field(default=None, max_length=10000)
    note: Optional[str] = Field(default=None, max_length=5000)
    chapter: Optional[str] = Field(default=None, max_length=500)
    pageno: Optional[int] = Field(default=None, ge=0)
    posFormat: str = Field(pattern=r"^(xpointer|pdf)$")
    pos0: str = Field(max_length=4000)
    pos1: Optional[str] = Field(default=None, max_length=4000)


class ExchangeKey(Loose):
    k: str = Field(pattern=HEX32)
    dt: str = Field(pattern=DEVICE_DATETIME)


class ExchangeBook(Loose):
    hash: str = Field(pattern=HEX32)
    keys: List[ExchangeKey] = Field(max_length=5000)
    keysComplete: bool
    sidecarState: Optional[str] = Field(default=None, pattern=r"^(known|new)$")
    changes: List[Annotation] = Field(max_length=50)


class AnnotationExchange(Device):
    books: List[ExchangeBook] = Field(min_length=1, max_length=20)


class AckApplied(Loose):
    serverId: int = Field(ge=1)
    version: int = Field(ge=1)
    status: str = Field(pattern=r"^(applied|failed)$")


class AckDeleted(Loose):
    serverId: int = Field(ge=1)
    status: str = Field(pattern=r"^(applied|failed)$")


class AckBook(Loose):
    hash: str = Field(pattern=HEX32)
    applied: List[AckApplied] = Field(max_length=200)
    deleted: List[AckDeleted] = Field(max_length=200)


class AnnotationAck(Device):
    books: List[AckBook] = Field(min_length=1, max_length=20)


class BookState(Loose):
    hash: str = Field(pattern=HEX32)
    status: Optional[str] = Field(default=None, pattern=r"^(reading|complete|abandoned)$")
    statusModified: Optional[str] = Field(default=None, pattern=r"^\d{4}-\d{2}-\d{2}$")
    rating: Optional[int] = Field(default=None, ge=1, le=5)
    ratingCleared: Optional[bool] = None
    reviewNote: Optional[str] = Field(default=None, max_length=10000)
    reviewCleared: Optional[bool] = None
    reviewModified: Optional[str] = Field(default=None, pattern=r"^\d{4}-\d{2}-\d{2}$")


class BookStates(Device):
    books: List[BookState] = Field(min_length=1, max_length=200)


class BulkProgressItem(Loose):
    hash: str = Field(pattern=HEX32)
    percentage: float = Field(ge=0, le=1)
    progress: Optional[str] = Field(default=None, max_length=4000)
    timestamp: Optional[int] = Field(default=None, ge=0)


class BulkProgress(Device):
    items: List[BulkProgressItem] = Field(min_length=1, max_length=100)


class Sweep(Device):
    booksMatched: int = Field(ge=0)
    pageStatsUploaded: int = Field(ge=0)
    annotationsUpserted: int = Field(ge=0)


class SaveProgress(Loose):
    document: str
    percentage: float = Field(ge=0, le=1)
    progress: Optional[str] = None
    device: Optional[str] = None
    device_id: Optional[str] = None
    timestamp: Optional[float] = None


class ReadStatusBody(Loose):
    status: str = Field(pattern="^(" + "|".join(SETTABLE_READ_STATUSES) + ")$")


class RatingBody(Loose):
    rating: Optional[float] = Field(default=None, ge=0, le=5)


# --- State ---------------------------------------------------------------------

class State:
    def __init__(self):
        self.reset()

    def reset(self):
        self.books = fixtures.build_library()
        self.by_hash = {b.file.hash: b for b in self.books}
        self.files = {b.file.id: b.file for b in self.books}
        self.capabilities = list(fixtures.ALL_CAPABILITIES)
        self.version_known = True
        self.server_version = fixtures.SERVER_VERSION
        self.ranges = True
        self.rules: list = []
        self.requests: list = []
        self.progress: dict = {}
        self.page_events: dict = {}  # hash -> {(startTime, page): event}
        self.sweeps: list = []
        self.states: dict = {}
        self.annotations: dict = {}  # hash -> {key: annotation}
        self.link_version = 1
        self.user = fixtures.USERNAME
        self.key = hashlib.md5(fixtures.PASSWORD.encode()).hexdigest()

    def library_version(self) -> str:
        return hashlib.md5(f"lib|{self.link_version}|{len(self.books)}".encode()).hexdigest()[:16]

    def has(self, capability: str) -> bool:
        return capability in self.capabilities


def nest_error(status: int, message: Any, error: str):
    return JSONResponse({"statusCode": status, "message": message, "error": error}, status_code=status)


def create_app(state: Optional[State] = None) -> FastAPI:
    st = state or State()
    app = FastAPI(title="Mock BookOrbit", docs_url=None, redoc_url=None, openapi_url=None)
    app.state.mock = st

    @app.exception_handler(RequestValidationError)
    async def validation_error(_request: Request, exc: RequestValidationError):
        messages = [f"{'.'.join(str(p) for p in e['loc'][1:])}: {e['msg']}" for e in exc.errors()]
        return nest_error(400, messages, "Bad Request")

    # --- Rules and request log ------------------------------------------------

    def find_rule(method: str, path: str):
        for rule in st.rules:
            if rule.get("method") and rule["method"].upper() != method:
                continue
            if rule.get("path") != path:
                continue
            if rule.get("_seen", 0) < rule.get("skip", 0):
                rule["_seen"] = rule.get("_seen", 0) + 1
                continue
            times = rule.get("times")
            if times is not None:
                if rule.get("_used", 0) >= times:
                    continue
            rule["_used"] = rule.get("_used", 0) + 1
            return rule
        return None

    def rule_response(rule) -> Response:
        if rule.get("drop"):
            async def body():
                yield b'{"partial":'
                await asyncio.sleep(0.05)
                raise RuntimeError("mock: connection dropped on purpose")

            return StreamingResponse(body(), media_type="application/json",
                                     headers={"Content-Length": "4096"})
        if "raw_body" in rule:
            return Response(rule["raw_body"], status_code=rule.get("status", 200),
                            media_type=rule.get("content_type", "application/json"))
        body = rule.get("body", {"statusCode": rule.get("status", 500), "message": "mock rule"})
        return JSONResponse(body, status_code=rule.get("status", 200))

    @app.middleware("http")
    async def protocol_middleware(request: Request, call_next):
        path = request.url.path
        if not path.startswith(PREFIX + "/"):
            return await call_next(request)
        short = path[len(PREFIX):]
        raw = await request.body()
        try:
            parsed: Any = json.loads(raw) if raw else None
        except ValueError:
            parsed = raw.decode("utf-8", "replace")
        entry = {
            "method": request.method,
            "path": short,
            "query": dict(request.query_params),
            "headers": {k: request.headers.get(k) for k in
                        ("x-auth-user", "x-auth-key", "range", "accept", "content-type",
                         "x-bookorbit-device-id", "x-bookorbit-plugin-version", "accept-encoding")
                        if request.headers.get(k) is not None},
            "body": parsed,
            "time": time.time(),
        }
        st.requests.append(entry)
        rule = find_rule(request.method, short)
        entry["rule"] = bool(rule)
        if rule and not rule.get("after_processing"):
            if rule.get("delay_ms"):
                await asyncio.sleep(rule["delay_ms"] / 1000.0)
            if any(k in rule for k in ("status", "body", "raw_body", "drop")):
                return rule_response(rule)
        request.state.rule = rule
        response = await call_next(request)
        if rule and rule.get("after_processing"):
            if rule.get("delay_ms"):
                await asyncio.sleep(rule["delay_ms"] / 1000.0)
            if any(k in rule for k in ("status", "body", "raw_body", "drop")):
                return rule_response(rule)
        return response

    # --- Test control -----------------------------------------------------------

    @app.get("/__mock/ready")
    async def ready():
        return Response("ok", media_type="text/plain")

    @app.post("/__mock/reset")
    async def reset():
        st.reset()
        return {"ok": True}

    @app.post("/__mock/config")
    async def config(body: dict):
        if "capabilities" in body:
            if body["capabilities"] is None:
                st.version_known = False  # an old server: no /plugin/version at all
                st.capabilities = []
            else:
                st.version_known = True
                st.capabilities = list(body["capabilities"])
        if "serverVersion" in body:
            st.server_version = body["serverVersion"]
        if "ranges" in body:
            st.ranges = bool(body["ranges"])
        if "rules" in body:
            st.rules = [dict(r) for r in body["rules"]]
        if "libraryVersionBump" in body:
            st.link_version += int(body["libraryVersionBump"])
        return {"ok": True}

    @app.get("/__mock/requests")
    async def requests_log(clear: int = 0):
        out = list(st.requests)
        if clear:
            st.requests.clear()
        return out

    @app.get("/__mock/state")
    async def mock_state():
        return {
            "pageEvents": {h: len(e) for h, e in st.page_events.items()},
            "pageEventsDetail": {h: sorted(f"{k[0]}:{k[1]}" for k in e) for h, e in st.page_events.items()},
            "sweeps": st.sweeps,
            "progress": st.progress,
            "bookStates": st.states,
            "annotations": {h: list(a.keys()) for h, a in st.annotations.items()},
            "readStatus": {b.id: b.read_status for b in st.books},
            "ratings": {b.id: b.rating for b in st.books},
            "files": {f.id: {"size": f.size, "hash": f.hash, "sha256": hashlib.sha256(f.data).hexdigest()}
                  for f in st.files.values()},
        }

    # --- Auth ---------------------------------------------------------------------

    class AuthFailure(Exception):
        def __init__(self, message: str):
            self.message = message

    @app.exception_handler(AuthFailure)
    async def auth_failure(_request: Request, exc: AuthFailure):
        return nest_error(401, exc.message, "Unauthorized")

    async def check_auth(request: Request):
        user = request.headers.get("x-auth-user")
        key = request.headers.get("x-auth-key")
        if not user or not key:
            raise AuthFailure("Missing KOReader credentials")
        incoming = key if re.fullmatch(r"[0-9a-fA-F]{32}", key) else hashlib.md5(key.encode()).hexdigest()
        if user != st.user or incoming.lower() != st.key:
            raise AuthFailure("Invalid credentials")

    api = APIRouter(prefix=PREFIX, dependencies=[Depends(check_auth)])

    def require_capability(name: str) -> Optional[Response]:
        # Older servers answer 404 for routes they don't have.
        if not st.has(name):
            return nest_error(404, "Cannot GET (route not available on this server)", "Not Found")
        return None

    # --- kosync -----------------------------------------------------------------

    @api.get("/users/auth")
    async def users_auth(request: Request):
        return {"authorized": "OK", "username": request.headers.get("x-auth-user")}

    @api.get("/syncs/progress/{document}")
    async def get_progress(request: Request, document: str):
        return st.progress.get(document, {})  # {} when unknown, as the real server does

    @api.put("/syncs/progress")
    async def put_progress(request: Request, body: SaveProgress):
        if body.document not in st.by_hash:
            return nest_error(404, "Book not found for the given document hash", "Not Found")
        stored = {
            "document": body.document,
            "percentage": body.percentage,
            "progress": body.progress,
            "device": body.device,
            "device_id": body.device_id,
            "timestamp": int(body.timestamp) if body.timestamp is not None else int(time.time()),
        }
        st.progress[body.document] = stored
        return {"document": body.document, "timestamp": stored["timestamp"]}  # mock choice

    # --- Plugin -------------------------------------------------------------------

    @api.get("/plugin/version")
    async def plugin_version(request: Request):
        if not st.version_known:
            return nest_error(404, "Cannot GET /api/v1/koreader/plugin/version", "Not Found")
        return {
            "pluginVersion": fixtures.PLUGIN_VERSION,
            "serverVersion": st.server_version,
            "capabilities": list(st.capabilities),
        }

    @api.post("/plugin/match-check")
    async def match_check(request: Request, body: MatchCheck):
        hashes = list(dict.fromkeys(h.lower() for h in body.hashes))
        matches = []
        for h in hashes:
            book = st.by_hash.get(h)
            if book:
                matches.append({"hash": h, "bookId": book.id, "bookFileId": book.file.id})
        return {"matches": matches, "libraryVersion": st.library_version()}

    @api.post("/plugin/page-stats")
    async def page_stats(request: Request, body: PageStats):
        total = sum(len(b.events) for b in body.books)
        if total > MAX_PAGE_STATS_EVENTS:
            return nest_error(400, f"Too many page stat events in one request (max {MAX_PAGE_STATS_EVENTS})",
                              "Bad Request")
        results, unmatched = [], []
        for book in body.books:
            h = book.hash.lower()
            if h not in st.by_hash:
                unmatched.append(h)
                continue
            store = st.page_events.setdefault(h, {})
            accepted = duplicates = 0
            for ev in book.events:
                key = (ev.startTime, ev.page)  # mock choice: how the real server dedups is not specified
                if key in store:
                    duplicates += 1
                else:
                    store[key] = ev.model_dump()
                    accepted += 1
            # Watermark covers every event in the batch, duplicates included
            # (koreader-stats.service.ts).
            watermark = max((e.startTime for e in book.events), default=0)
            results.append({"hash": h, "accepted": accepted, "duplicates": duplicates, "watermark": watermark})
        return {"results": results, "unmatched": unmatched}

    @api.post("/plugin/sweeps")
    async def sweeps(request: Request, body: Sweep):
        st.sweeps.append({
            "deviceId": body.deviceId, "booksMatched": body.booksMatched,
            "pageStatsUploaded": body.pageStatsUploaded, "annotationsUpserted": body.annotationsUpserted,
        })
        return {"ok": True, "lastSweepAt": time.strftime("%Y-%m-%dT%H:%M:%S.000Z", time.gmtime()),
                "libraryVersion": st.library_version()}

    @api.post("/plugin/progress")
    async def bulk_progress(request: Request, body: BulkProgress):
        results, unmatched = [], []
        for item in body.items:
            h = item.hash.lower()
            if h not in st.by_hash:
                unmatched.append(h)
                continue
            st.progress[h] = {"document": h, "percentage": item.percentage, "progress": item.progress,
                              "device": body.deviceModel, "device_id": body.deviceId,
                              "timestamp": item.timestamp or int(time.time())}
            results.append({"hash": h, "accepted": True})
        return {"results": results, "unmatched": unmatched}

    @api.post("/plugin/book-states")
    async def book_states(request: Request, body: BookStates):
        results, unmatched = [], []
        for item in body.books:
            h = item.hash.lower()
            if h not in st.by_hash:
                unmatched.append(h)
                continue
            st.states[h] = item.model_dump(exclude_none=True)
            results.append({"hash": h, "statusApplied": item.status is not None, "status": item.status,
                            "ratingApplied": item.rating is not None, "rating": item.rating})  # mock choice (subset)
        return {"results": results, "unmatched": unmatched}

    @api.post("/plugin/annotations/exchange")
    async def annotations_exchange(request: Request, body: AnnotationExchange):
        total = sum(len(b.changes) for b in body.books)
        if total > MAX_ANNOTATION_CHANGES:
            return nest_error(400, f"Too many annotation changes in one request (max {MAX_ANNOTATION_CHANGES})",
                              "Bad Request")
        results, unmatched = [], []
        for book in body.books:
            h = book.hash.lower()
            b = st.by_hash.get(h)
            if not b:
                unmatched.append(h)
                continue
            store = st.annotations.setdefault(h, {})
            created = updated = unchanged = 0
            for ch in book.changes:
                key = hashlib.md5(f"{ch.datetime}|{ch.pos0}".encode()).hexdigest()
                if key in store:
                    updated += 1 if store[key] != ch.model_dump() else 0
                    unchanged += 0 if store[key] != ch.model_dump() else 1
                else:
                    created += 1
                store[key] = ch.model_dump()
            results.append({"hash": h, "bookId": b.id,
                            "applied": {"created": created, "updated": updated, "moved": 0,
                                        "unchanged": unchanged, "skippedDeleted": 0, "deviceDeleted": 0},
                            "toApply": {"add": [], "edit": [], "delete": []},
                            "more": False, "skippedNoPosition": 0})
        return {"results": results, "unmatched": unmatched, "positionIncompatible": []}

    @api.post("/plugin/annotations/exchange-ack")
    async def annotations_ack(request: Request, body: AnnotationAck):
        results, unmatched = [], []
        for book in body.books:
            h = book.hash.lower()
            if h not in st.by_hash:
                unmatched.append(h)
                continue
            results.append({"hash": h, "acked": len(book.applied) + len(book.deleted)})
        return {"results": results, "unmatched": unmatched, "positionIncompatible": []}

    # --- Catalog --------------------------------------------------------------------

    catalog = "/plugin/catalog"

    @api.get(catalog + "/root")
    async def catalog_root(request: Request):
        sections = [{"id": s, "title": s.replace("-", " ").title(), "section": s,
                     "href": f"{PREFIX}/plugin/catalog/sections/{s}"} for s in SECTION_KINDS]
        return {"sections": sections}

    @api.get(catalog + "/dashboard")
    async def catalog_dashboard(request: Request):
        return {"continueReading": [b.list_item() for b in st.books[:2]],
                "discover": [b.list_item() for b in st.books[2:]],
                "totalBooks": len(st.books)}

    @api.get(catalog + "/dashboard/discover")
    async def catalog_discover(request: Request):
        return {"items": [b.list_item() for b in st.books[2:]]}

    @api.get(catalog + "/dashboard/sections/{section_type}")
    async def catalog_dashboard_section(request: Request, section_type: str):
        missing = require_capability("catalogDashboardSections")
        if missing:
            return missing
        if section_type not in DASHBOARD_SECTION_TYPES:
            return nest_error(400, "Validation failed (enum string is expected)", "Bad Request")
        return {"type": section_type, "items": [b.list_item() for b in st.books if b.series_id]}

    @api.get(catalog + "/sections/{kind}")
    async def catalog_section(request: Request, kind: str, page: int = 1, size: int = 100):
        if page < 1 or not 1 <= size <= 100:
            return nest_error(400, ["page/size out of range"], "Bad Request")
        items = []
        if kind == "series":
            items = [{"id": "series-10", "title": "Salt Trilogy", "section": "series", "seriesId": 10, "count": 2}]
        elif kind == "authors":
            names = sorted({a for b in st.books for a in b.authors})
            items = [{"id": f"author-{i}", "title": n, "section": "authors", "authorId": i}
                     for i, n in enumerate(names, 1)]
        return {"section": kind, "items": items, "page": page, "size": size, "total": len(items),
                "hasNext": False, "hasPrevious": page > 1}

    @api.get(catalog + "/books")
    async def catalog_books(request: Request):
        q = request.query_params
        errors = []
        page = int(q.get("page", "1")) if q.get("page", "1").isdigit() else 0
        size = int(q.get("size", "20")) if q.get("size", "20").isdigit() else 0
        if page < 1:
            errors.append("page must not be less than 1")
        if not 1 <= size <= 100:
            errors.append("size must be between 1 and 100")
        sort = q.get("sort", "recently_added")
        if sort not in CATALOG_SORTS:
            errors.append("sort must be one of the following values: " + ", ".join(CATALOG_SORTS))
        order = q.get("order")
        if order is not None and order not in ("asc", "desc"):
            errors.append("order must be one of the following values: asc, desc")
        ids = None
        if "ids" in q:
            ids = [int(x) for x in q["ids"].split(",") if x.strip().isdigit()]
            if len(ids) > 200:
                errors.append("ids must contain no more than 200 elements")
        if errors:
            return nest_error(400, errors, "Bad Request")
        books = list(st.books)
        if ids is not None:
            books = [b for b in books if b.id in ids]
        if "seriesId" in q:
            books = [b for b in books if str(b.series_id) == q["seriesId"]]
        if "q" in q and q["q"]:
            needle = q["q"].lower()
            books = [b for b in books if needle in b.title.lower() or any(needle in a.lower() for a in b.authors)]
        if sort == "title":
            books.sort(key=lambda b: b.title.lower())
        if order == "desc":
            books.reverse()
        total = len(books)
        window = books[(page - 1) * size: page * size]
        return {"items": [b.list_item() for b in window], "total": total, "page": page, "size": size,
                "hasNext": page * size < total, "hasPrevious": page > 1,
                "nextUrl": None, "previousUrl": None}

    @api.get(catalog + "/manifest")
    async def catalog_manifest(request: Request, cursor: Optional[str] = None, size: int = 100):
        missing = require_capability("catalogBulkManifest")
        if missing:
            return missing
        if not 1 <= size <= 200:
            return nest_error(400, ["size must be between 1 and 200"], "Bad Request")
        # Mock choice: the cursor is the id of the last book of the previous page.
        after = int(cursor) if cursor and cursor.isdigit() else 0
        rows = [b for b in st.books if b.id > after][:size]
        items = [{"bookId": b.id, "title": b.title, "files": [{"id": b.file.id, "format": b.file.format,
                                                                 "sizeBytes": b.file.size,
                                                                 "fileHash": b.file.hash}]} for b in rows]
        has_next = bool(rows) and rows[-1].id < max(b.id for b in st.books)
        return {"items": items, "hasNext": has_next, "nextCursor": str(rows[-1].id) if has_next else None,
                "manifestVersion": st.library_version(), "restartRequired": False}

    @api.get(catalog + "/books/{book_id}")
    async def catalog_book(request: Request, book_id: int):
        for b in st.books:
            if b.id == book_id:
                return b.detail()
        return nest_error(404, "Book not found", "Not Found")

    @api.put(catalog + "/books/{book_id}/read-status")
    async def set_read_status(request: Request, book_id: int, body: ReadStatusBody):
        for b in st.books:
            if b.id == book_id:
                b.read_status = body.status
                return {"readStatus": body.status}
        return nest_error(404, "Book not found", "Not Found")

    @api.put(catalog + "/books/{book_id}/rating")
    async def set_rating(request: Request, book_id: int, body: RatingBody):
        for b in st.books:
            if b.id == book_id:
                b.rating = int(body.rating) if body.rating else None
                return {"rating": b.rating}
        return nest_error(404, "Book not found", "Not Found")

    @api.get(catalog + "/books/{book_id}/thumbnail")
    async def thumbnail(request: Request, book_id: int):
        if not any(b.id == book_id for b in st.books):
            return nest_error(404, "Book not found", "Not Found")
        return Response(fixtures.THUMBNAIL_PNG, media_type="image/png")

    @api.get(catalog + "/files/{file_id}/download")
    async def download(request: Request, file_id: int):
        f = st.files.get(file_id)
        if not f:
            return nest_error(404, "File not found", "Not Found")
        data = f.data
        total = len(data)
        start, status = 0, 200
        headers = {"Content-Disposition": f'attachment; filename="{f.id}.{f.format}"',
                   "Accept-Ranges": "bytes" if st.ranges else "none"}
        range_header = request.headers.get("range")
        if range_header and st.ranges:
            m = re.fullmatch(r"bytes=(\d+)-(\d*)", range_header.strip())
            if not m:
                return Response(status_code=416, headers={"Content-Range": f"bytes */{total}"})
            start = int(m.group(1))
            if start >= total:
                return Response(status_code=416, headers={"Content-Range": f"bytes */{total}"})
            end = int(m.group(2)) if m.group(2) else total - 1
            end = min(end, total - 1)
            data = data[start:end + 1]
            status = 206
            headers["Content-Range"] = f"bytes {start}-{end}/{total}"
        headers["Content-Length"] = str(len(data))
        rule = getattr(request.state, "rule", None)
        cut = rule.get("drop_after_bytes") if rule else None
        pause = (rule.get("chunk_delay_ms", 0) if rule else 0) / 1000.0

        async def stream():
            chunk = 16384
            sent = 0
            for i in range(0, len(data), chunk):
                piece = data[i:i + chunk]
                if cut is not None and sent + len(piece) > cut:
                    yield piece[:max(0, cut - sent)]
                    await asyncio.sleep(0.05)
                    raise RuntimeError("mock: connection dropped on purpose")
                sent += len(piece)
                yield piece
                await asyncio.sleep(pause)

        return StreamingResponse(stream(), status_code=status, headers=headers,
                                 media_type={"epub": "application/epub+zip", "pdf": "application/pdf",
                                             "cbz": "application/vnd.comicbook+zip"}.get(f.format,
                                                                                         "application/octet-stream"))

    app.include_router(api)
    return app
