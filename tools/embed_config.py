#!/usr/bin/env python3
"""Embed the shipped INI so generated files and new keys share its defaults."""
import configparser
import json
from pathlib import Path
import sys


def generate(source, destination):
    text = Path(source).read_text(encoding="ascii").replace("\r\n", "\n")
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    config.read_string(text)
    if config.sections() != ["Display"]:
        raise ValueError("expected one [Display] section")
    entries = ",\n".join("    {L" + json.dumps(k) + ", L" + json.dumps(v) + "}"
                         for k, v in config["Display"].items())
    output = ("#pragma once\nnamespace dm {\n"
              "struct IniDefault { const wchar_t* key; const wchar_t* value; };\n"
              "constexpr IniDefault kIniDefaults[] = {\n" + entries + "\n};\n"
              "constexpr char kDefaultIni[] = " + json.dumps(text.replace("\n", "\r\n")) + ";\n}\n")
    target = Path(destination)
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists() or target.read_text(encoding="ascii") != output:
        target.write_text(output, encoding="ascii")


if __name__ == "__main__":
    generate(*sys.argv[1:])
