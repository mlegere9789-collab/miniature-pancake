"""Offline unit tests for Stock Asset Licensing.

No network required — all Anthropic API responses are faked. Run with:
    python -m unittest modules.stock_licensing.test_stock_licensing
or via the whole suite:
    python -m unittest discover -s modules -p "test_*.py"
"""

from __future__ import annotations

import unittest

from .assets import load_assets
from .config import Settings
from .formatter import format_parse_failure_description, format_review_description
from .keywords import MetadataParseError, build_prompt, parse_reply

SAMPLE_ASSET = {
    "id": "sunset-beach-01",
    "filename": "sunset-beach-01.jpg",
    "category": "photo",
    "subject": "golden hour beach with silhouetted palm trees",
    "notes": "shot on a 50mm, warm color grade",
}

VALID_REPLY = (
    '{"title": "Golden Hour Beach With Silhouetted Palm Trees", '
    '"description": "A warm, golden-hour shot of a beach with palm trees '
    'silhouetted against the sky.", '
    '"keywords": ["beach", "sunset", "palm tree", "golden hour", "silhouette"]}'
)


def make_settings(**overrides) -> Settings:
    base = dict(
        anthropic_api_key="sk-ant-test",
        model="claude-3-5-haiku-20241022",
        max_tokens=500,
        max_per_run=5,
    )
    base.update(overrides)
    return Settings(**base)


class TestSettings(unittest.TestCase):
    def test_configured_requires_api_key(self):
        self.assertTrue(make_settings().configured)
        self.assertFalse(make_settings(anthropic_api_key=None).configured)


class TestAssets(unittest.TestCase):
    def test_load_missing_file_is_empty(self):
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as d:
            self.assertEqual(load_assets(Path(d) / "nope.json"), [])

    def test_load_round_trips(self):
        import json
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "assets.json"
            path.write_text(json.dumps([SAMPLE_ASSET]), encoding="utf-8")
            self.assertEqual(load_assets(path), [SAMPLE_ASSET])

    def test_load_skips_malformed_entries(self):
        import json
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "assets.json"
            path.write_text(
                json.dumps([SAMPLE_ASSET, {"filename": "missing id"}, "not a dict"]),
                encoding="utf-8",
            )
            self.assertEqual(load_assets(path), [SAMPLE_ASSET])

    def test_load_non_list_json_is_empty(self):
        import json
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "assets.json"
            path.write_text(json.dumps({"not": "a list"}), encoding="utf-8")
            self.assertEqual(load_assets(path), [])


class TestKeywords(unittest.TestCase):
    def test_build_prompt_includes_asset_fields(self):
        prompt = build_prompt(SAMPLE_ASSET)
        self.assertIn("golden hour beach with silhouetted palm trees", prompt)
        self.assertIn("shot on a 50mm, warm color grade", prompt)
        self.assertIn("photo", prompt)

    def test_build_prompt_handles_missing_fields(self):
        prompt = build_prompt({"id": "x", "filename": "x.jpg"})
        self.assertIn("x.jpg", prompt)
        self.assertIn("none", prompt)

    def test_parse_valid_reply(self):
        metadata = parse_reply(VALID_REPLY)
        self.assertEqual(
            metadata.title, "Golden Hour Beach With Silhouetted Palm Trees"
        )
        self.assertIn("golden-hour", metadata.description)
        self.assertEqual(len(metadata.keywords), 5)

    def test_parse_strips_markdown_fence(self):
        fenced = f"```json\n{VALID_REPLY}\n```"
        metadata = parse_reply(fenced)
        self.assertEqual(
            metadata.title, "Golden Hour Beach With Silhouetted Palm Trees"
        )

    def test_parse_rejects_non_json(self):
        with self.assertRaises(MetadataParseError):
            parse_reply("Sure, here are some great keywords!")

    def test_parse_rejects_missing_title(self):
        with self.assertRaises(MetadataParseError):
            parse_reply('{"description": "x", "keywords": []}')

    def test_parse_rejects_non_string_keywords(self):
        with self.assertRaises(MetadataParseError):
            parse_reply('{"title": "t", "description": "d", "keywords": [1, 2]}')

    def test_parse_rejects_empty_title(self):
        with self.assertRaises(MetadataParseError):
            parse_reply('{"title": "  ", "description": "d", "keywords": []}')


class TestFormatter(unittest.TestCase):
    def test_review_description_includes_metadata(self):
        from .keywords import AssetMetadata

        metadata = AssetMetadata(title="T", description="D", keywords=["a", "b"])
        text = format_review_description(SAMPLE_ASSET, metadata)
        self.assertIn("T", text)
        self.assertIn("D", text)
        self.assertIn("a, b", text)
        self.assertIn("sunset-beach-01", text)
        self.assertIn("sunset-beach-01.jpg", text)

    def test_parse_failure_description_includes_raw_text(self):
        text = format_parse_failure_description(SAMPLE_ASSET, "not json at all")
        self.assertIn("not json at all", text)
        self.assertIn("sunset-beach-01", text)


class TestSeenStore(unittest.TestCase):
    def test_mark_and_check(self):
        import tempfile
        from pathlib import Path

        from . import dedup

        with tempfile.TemporaryDirectory() as d:
            orig = dedup.SEEN_FILE
            dedup.SEEN_FILE = Path(d) / "seen.json"
            try:
                store = dedup.SeenStore()
                self.assertFalse(store.is_seen("sunset-beach-01"))
                store.mark("sunset-beach-01")
                store.save()
                reloaded = dedup.SeenStore()
                self.assertTrue(reloaded.is_seen("sunset-beach-01"))
            finally:
                dedup.SEEN_FILE = orig


if __name__ == "__main__":
    unittest.main()
