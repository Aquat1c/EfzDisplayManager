import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("embed_config", Path(__file__).parents[1] / "tools/embed_config.py")
embed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(embed)


class TemplateTest(unittest.TestCase):
    def test_template_and_idempotence(self):
        with tempfile.TemporaryDirectory() as directory:
            source, target = Path(directory) / "settings.ini", Path(directory) / "generated/header.h"
            source.write_text('; comment\n[Display]\nFilter=Linear\nVSync=Auto\n', encoding="ascii")
            embed.generate(source, target)
            output, timestamp = target.read_text(), target.stat().st_mtime_ns
            self.assertIn('{L"VSync", L"Auto"}', output)
            self.assertIn('; comment\\r\\n[Display]', output)
            embed.generate(source, target)
            self.assertEqual(target.stat().st_mtime_ns, timestamp)

    def test_rejects_invalid_template_before_overwriting(self):
        with tempfile.TemporaryDirectory() as directory:
            source, target = Path(directory) / "settings.ini", Path(directory) / "header.h"
            target.write_text("keep", encoding="ascii")
            for text in ("[Other]\nVSync=Auto\n", "[Display]\nVSync=Auto\nVSync=1\n"):
                source.write_text(text, encoding="ascii")
                with self.assertRaises((ValueError, embed.configparser.Error)):
                    embed.generate(source, target)
                self.assertEqual(target.read_text(), "keep")
