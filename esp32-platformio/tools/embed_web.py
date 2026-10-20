"""PlatformIO pre-build hook: embed the editable web/index.html as flash-resident C++ HTML."""
from pathlib import Path
Import("env")
root = Path(env["PROJECT_DIR"])
source = (root / "web" / "index.html").read_text(encoding="utf-8")
delimiter = "FREEZYWEBUI"
if f"){delimiter}\"" in source:
    raise ValueError("Invalid HTML raw-string delimiter")
header = ("// Generated from web/index.html at build time. Do not edit by hand.\n"
          "#pragma once\n#include <Arduino.h>\n"
          f"static const char kWebHtml[] PROGMEM = R\"{delimiter}({source}){delimiter}\";\n")
output = root / "include" / "WebUI.h"
if not output.exists() or output.read_text(encoding="utf-8") != header:
    output.write_text(header, encoding="utf-8")