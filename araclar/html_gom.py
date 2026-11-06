#!/usr/bin/env python3
"""ag/arayuz.html dosyasını ag/source/arayuz_html.h içine C dizisi olarak gömer.
arayuz.html'i değiştirdikten sonra çalıştır:  python3 araclar/html_gom.py"""
import pathlib

kok = pathlib.Path(__file__).resolve().parent.parent
html = (kok / "ag" / "arayuz.html").read_bytes()
satirlar = []
for i in range(0, len(html), 20):
    satirlar.append("    " + ", ".join(f"0x{b:02x}" for b in html[i:i + 20]) + ",")
cikti = (
    "// Otomatik üretildi: araclar/html_gom.py — elle düzenleme, arayuz.html'i düzenle.\n"
    "#pragma once\n"
    "#include <stddef.h>\n"
    "static const char ARAYUZ_HTML[] = {\n" + "\n".join(satirlar) + "\n};\n"
    f"#define ARAYUZ_HTML_BOYUT ((size_t){len(html)})\n"
)
(kok / "ag" / "source" / "arayuz_html.h").write_text(cikti)
print(f"arayuz_html.h yazıldı ({len(html)} bayt)")
