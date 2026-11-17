"""Tests for the mock server itself (run: python3 -m pytest tools/mock-bookorbit/tests)."""
import hashlib
import os
import sys

import pytest
from fastapi.testclient import TestClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from mock_bookorbit import fixtures  # noqa: E402
from mock_bookorbit.app import PREFIX, create_app  # noqa: E402

KEY = hashlib.md5(fixtures.PASSWORD.encode()).hexdigest()
AUTH = {"x-auth-user": fixtures.USERNAME, "x-auth-key": KEY}
DEVICE = {"deviceId": "dev-1", "deviceModel": "Kobo Test", "pluginVersion": "0.1.0",
          "deviceTime": "2026-11-17 10:00:00"}


@pytest.fixture()
def client():
    with TestClient(create_app()) as c:
        yield c


def url(path):
    return PREFIX + path


def test_auth_required_and_checked(client):
    assert client.get(url("/users/auth")).status_code == 401
    bad = dict(AUTH, **{"x-auth-key": "0" * 32})
    assert client.get(url("/users/auth"), headers=bad).status_code == 401
    ok = client.get(url("/users/auth"), headers=AUTH)
    assert ok.status_code == 200 and ok.json() == {"authorized": "OK", "username": fixtures.USERNAME}


def test_auth_beats_validation(client):
    assert client.post(url("/plugin/match-check"), json={}).status_code == 401


def test_version_and_capabilities(client):
    body = client.get(url("/plugin/version"), headers=AUTH).json()
    assert "catalogDashboardSections" in body["capabilities"] and body["serverVersion"]
    client.post("/__mock/config", json={"capabilities": ["catalogListsV2"]})
    assert client.get(url("/plugin/version"), headers=AUTH).json()["capabilities"] == ["catalogListsV2"]
    assert client.get(url("/plugin/catalog/dashboard/sections/want-to-read"), headers=AUTH).status_code == 404
    client.post("/__mock/config", json={"capabilities": None})
    assert client.get(url("/plugin/version"), headers=AUTH).status_code == 404


def test_match_check(client):
    book = client.app.state.mock.books[0]
    body = dict(DEVICE, hashes=[book.file.hash, "f" * 32])
    reply = client.post(url("/plugin/match-check"), headers=AUTH, json=body).json()
    assert reply["matches"] == [{"hash": book.file.hash, "bookId": 1, "bookFileId": 101}]
    assert reply["libraryVersion"]
    assert client.post(url("/plugin/match-check"), headers=AUTH, json=dict(DEVICE, hashes=["nothex"])).status_code == 400


def test_page_stats_dedup_and_watermark(client):
    h = client.app.state.mock.books[0].file.hash
    events = [{"page": 1, "startTime": 1000, "durationSeconds": 30, "totalPages": 100},
              {"page": 2, "startTime": 1100, "durationSeconds": 30, "totalPages": 100}]
    body = dict(DEVICE, books=[{"hash": h, "events": events}])
    first = client.post(url("/plugin/page-stats"), headers=AUTH, json=body).json()
    assert first == {"results": [{"hash": h, "accepted": 2, "duplicates": 0, "watermark": 1100}], "unmatched": []}
    again = client.post(url("/plugin/page-stats"), headers=AUTH, json=body).json()
    assert again["results"][0]["accepted"] == 0 and again["results"][0]["duplicates"] == 2
    assert client.get("/__mock/state").json()["pageEvents"][h] == 2
    unknown = client.post(url("/plugin/page-stats"), headers=AUTH,
                          json=dict(DEVICE, books=[{"hash": "a" * 32, "events": events}])).json()
    assert unknown == {"results": [], "unmatched": ["a" * 32]}


def test_page_stats_validation(client):
    h = "a" * 32
    bad = dict(DEVICE, books=[{"hash": h, "events": [{"page": 1, "startTime": 0,
                                                       "durationSeconds": 1, "totalPages": 1}]}])
    assert client.post(url("/plugin/page-stats"), headers=AUTH, json=bad).status_code == 400
    many = [{"page": 1, "startTime": 1 + i, "durationSeconds": 1, "totalPages": 1} for i in range(501)]
    assert client.post(url("/plugin/page-stats"), headers=AUTH,
                       json=dict(DEVICE, books=[{"hash": h, "events": many}])).status_code == 400


def test_sweep(client):
    body = dict(DEVICE, booksMatched=1, pageStatsUploaded=2, annotationsUpserted=0)
    reply = client.post(url("/plugin/sweeps"), headers=AUTH, json=body).json()
    assert reply["ok"] is True and reply["libraryVersion"] and reply["lastSweepAt"]
    assert client.get("/__mock/state").json()["sweeps"][0]["pageStatsUploaded"] == 2


def test_rules_status_after_processing_and_times(client):
    h = client.app.state.mock.books[0].file.hash
    client.post("/__mock/config", json={"rules": [
        {"path": "/plugin/page-stats", "method": "POST", "times": 1, "status": 500, "after_processing": True}]})
    body = dict(DEVICE, books=[{"hash": h, "events": [
        {"page": 1, "startTime": 5, "durationSeconds": 1, "totalPages": 9}]}])
    assert client.post(url("/plugin/page-stats"), headers=AUTH, json=body).status_code == 500
    assert client.get("/__mock/state").json()["pageEvents"][h] == 1  # processed despite the 500
    assert client.post(url("/plugin/page-stats"), headers=AUTH, json=body).status_code == 200
    assert client.get("/__mock/state").json()["pageEvents"][h] == 1  # no double count


def test_rule_raw_body(client):
    client.post("/__mock/config", json={"rules": [{"path": "/users/auth", "raw_body": "{not json", "times": 1}]})
    assert client.get(url("/users/auth"), headers=AUTH).text == "{not json"
    assert client.get(url("/users/auth"), headers=AUTH).status_code == 200


def test_request_log(client):
    client.get(url("/users/auth"), headers=AUTH)
    log = client.get("/__mock/requests?clear=1").json()
    assert log[0]["path"] == "/users/auth" and log[0]["headers"]["x-auth-user"] == fixtures.USERNAME
    assert client.get("/__mock/requests").json() == []


def test_download_full_and_range(client):
    f = client.app.state.mock.files[102]
    full = client.get(url("/plugin/catalog/files/102/download"), headers=AUTH)
    assert full.status_code == 200 and full.content == f.data
    part = client.get(url("/plugin/catalog/files/102/download"), headers=dict(AUTH, Range="bytes=1000-"))
    assert part.status_code == 206 and part.content == f.data[1000:]
    assert part.headers["content-range"] == f"bytes 1000-{f.size - 1}/{f.size}"
    past = client.get(url("/plugin/catalog/files/102/download"), headers=dict(AUTH, Range=f"bytes={f.size}-"))
    assert past.status_code == 416
    client.post("/__mock/config", json={"ranges": False})
    ignored = client.get(url("/plugin/catalog/files/102/download"), headers=dict(AUTH, Range="bytes=1000-"))
    assert ignored.status_code == 200 and len(ignored.content) == f.size


def test_books_query_validation(client):
    ok = client.get(url("/plugin/catalog/books"), headers=AUTH, params={"ids": "1,2", "page": 1, "size": 10}).json()
    assert [b["id"] for b in ok["items"]] == [1, 2]
    assert client.get(url("/plugin/catalog/books"), headers=AUTH, params={"size": 101}).status_code == 400
    assert client.get(url("/plugin/catalog/books"), headers=AUTH, params={"sort": "nope"}).status_code == 400


def test_read_status_and_rating(client):
    r = client.put(url("/plugin/catalog/books/1/read-status"), headers=AUTH, json={"status": "reading"})
    assert r.json() == {"readStatus": "reading"}
    assert client.put(url("/plugin/catalog/books/1/read-status"), headers=AUTH, json={"status": "x"}).status_code == 400
    assert client.put(url("/plugin/catalog/books/1/rating"), headers=AUTH, json={"rating": 4}).json() == {"rating": 4}
    assert client.put(url("/plugin/catalog/books/1/rating"), headers=AUTH, json={"rating": None}).json() == {"rating": None}


def test_progress_roundtrip(client):
    h = client.app.state.mock.books[0].file.hash
    assert client.get(url(f"/syncs/progress/{h}"), headers=AUTH).json() == {}
    put = {"document": h, "percentage": 0.5, "progress": "/body/DocFragment[3]", "device": "Kobo",
           "device_id": "dev-1", "timestamp": 1700000000}
    assert client.put(url("/syncs/progress"), headers=AUTH, json=put).status_code == 200
    assert client.get(url(f"/syncs/progress/{h}"), headers=AUTH).json()["percentage"] == 0.5
    put["document"] = "z" * 32
    assert client.put(url("/syncs/progress"), headers=AUTH, json=put).status_code == 404
