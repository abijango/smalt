"""Fixture library served by the mock: four books, deterministic file bytes.

Response shapes follow bookorbit/bookorbit packages/types/src/koreader.ts and
server/src/modules/koreader/ at commit c77966cc529423e993fae8a298aa77048b43bba2.
"""
import hashlib
from dataclasses import dataclass, field
from typing import Optional

from .partial_md5 import partial_md5

USERNAME = "test-reader"
PASSWORD = "fake-password"  # Fake. Tests use it; it protects nothing.
SERVER_VERSION = "9.9.9-mock"
PLUGIN_VERSION = "0.0.0-mock"

# The names a current BookOrbit server advertises
# (server/src/modules/koreader/koreader-package.service.ts, SERVER_CAPABILITIES).
ALL_CAPABILITIES = [
    "catalogBulkManifest",
    "catalogDashboardSections",
    "bookmarkSync",
    "readAloudDeliveryVariants",
    "readAloudDeliverySettings",
    "catalogListsV2",
    "catalogPublishedSort",
    "sidecarState",
    "annotationPendingBooks",
]


def file_bytes(file_id: int, size: int) -> bytes:
    out = bytearray()
    block = 0
    while len(out) < size:
        out += hashlib.sha256(f"{file_id}:{block}".encode()).digest()
        block += 1
    return bytes(out[:size])


@dataclass
class FileFixture:
    id: int
    book_id: int
    format: str
    size: int
    data: bytes = field(repr=False, default=b"")
    hash: str = ""

    def __post_init__(self):
        self.data = file_bytes(self.id, self.size)
        self.hash = partial_md5(self.data)


@dataclass
class BookFixture:
    id: int
    title: str
    authors: list
    series_id: Optional[int]
    series_name: Optional[str]
    series_index: Optional[float]
    file: FileFixture
    read_status: str = "unread"
    rating: Optional[int] = None

    def list_item(self):
        # Shape: KoreaderCatalogBookListItem.
        return {
            "id": self.id,
            "title": self.title,
            "authors": self.authors,
            "seriesId": self.series_id,
            "seriesName": self.series_name,
            "seriesIndex": self.series_index,
            "progressPercentage": None,
            "lastReadAt": None,
            "readStatus": self.read_status,
            "formats": [self.file.format],
            "hasCover": True,
            "thumbnailUrl": f"/api/v1/koreader/plugin/catalog/books/{self.id}/thumbnail",
            "detailUrl": f"/api/v1/koreader/plugin/catalog/books/{self.id}",
            "addedAt": "2026-01-01T00:00:00.000Z",
            "updatedAt": "2026-01-02T00:00:00.000Z",
        }

    def detail(self):
        item = self.list_item()
        item.update(
            {
                "subtitle": None,
                "description": f"About {self.title}.",
                "publisher": "Mock Press",
                "publishedDate": None,
                "publishedYear": 2020,
                "language": "en",
                "isbn10": None,
                "isbn13": None,
                "libraryId": 1,
                "libraryName": "Mock Library",
                "rating": self.rating,
                "pageCount": 300,
                "collections": [],
                "genres": [],
                "tags": [],
                "progress": None,
                "files": [
                    {
                        "id": self.file.id,
                        "format": self.file.format,
                        "role": "content",
                        "downloadVariant": "original",
                        "sizeBytes": self.file.size,
                        "durationSeconds": None,
                        "downloadUrl": f"/api/v1/koreader/plugin/catalog/files/{self.file.id}/download",
                        "devicePath": f"{self.title}.{self.file.format}",
                    }
                ],
                "relatedSections": [],
            }
        )
        return item


def build_library():
    return [
        BookFixture(1, "The Salt Road", ["Ada Quill"], 10, "Salt Trilogy", 1,
                    FileFixture(101, 1, "epub", 300_000)),
        BookFixture(2, "Salt Harvest", ["Ada Quill"], 10, "Salt Trilogy", 2,
                    FileFixture(102, 2, "epub", 150_000)),
        BookFixture(3, "Tidewrack", ["Bo Marsh", "Cy Reed"], None, None, None,
                    FileFixture(103, 3, "cbz", 1_200_000)),
        BookFixture(4, "Lantern Atlas", ["Di Vale"], None, None, None,
                    FileFixture(104, 4, "pdf", 20_000)),
    ]


def _png_1x1() -> bytes:
    import struct
    import zlib

    def chunk(kind: bytes, payload: bytes) -> bytes:
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)  # 1x1, 8-bit RGB
    pixels = zlib.compress(b"\x00\x3e\x6f\x6a")  # one teal pixel
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", pixels) + chunk(b"IEND", b"")


# A valid 1x1 PNG, for thumbnails.
THUMBNAIL_PNG = _png_1x1()
