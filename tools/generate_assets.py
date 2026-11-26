#!/usr/bin/env python3
"""
Gera o header include/PortalImages.h a partir das imagens em tools/images.
Otimiza 1.webp/1.png e 3.webp para uso em PROGMEM na Flash do ESP32.
"""
import os
from PIL import Image
import io

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
IMAGES_DIR = os.path.join(SCRIPT_DIR, "images")
HEADER_PATH = os.path.join(PROJECT_ROOT, "include", "PortalImages.h")

def resize_image(input_path, max_width, max_height, output_format='WEBP', quality=85):
    im = Image.open(input_path)
    # Mantém aspect ratio
    im.thumbnail((max_width, max_height), Image.Resampling.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, format=output_format, quality=quality, optimize=True)
    return buf.getvalue()

def bytes_to_c_array(data, name):
    lines = [f"// {name}: {len(data)} bytes", f"static const unsigned char {name}[] PROGMEM = {{"]
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
        if i + 16 < len(data):
            hex_str += ","
        lines.append(f"    {hex_str}")
    lines.append("};")
    lines.append(f"static const size_t {name}_LEN = {len(data)};\n")
    return "\n".join(lines)

def main():
    print("Otimizando imagens para o ESP32...")

    # Logo horizontal (3.webp): display 240x44 -> 384x128
    logo_src = os.path.join(IMAGES_DIR, "3.webp") if os.path.exists(os.path.join(IMAGES_DIR, "3.webp")) else os.path.join(IMAGES_DIR, "3.png")
    with open(logo_src, "rb") as f:
        logo_data = f.read()
    
    # Se 3.webp já for < 25KB usamos direto, senão redimensionamos
    if len(logo_data) > 25000:
        logo_webp = resize_image(logo_src, 384, 128, 'WEBP', quality=85)
    else:
        logo_webp = logo_data
    print(f"Logo WebP: {len(logo_webp)} bytes")

    total_bytes = len(logo_webp)
    print(f"Total de assets em Flash: {total_bytes} bytes (~{total_bytes/1024:.1f} KB)")

    header_content = f"""#ifndef PORTAL_IMAGES_H
#define PORTAL_IMAGES_H

#include <Arduino.h>

// =============================================================================
// Assets de Imagem para o Captive Portal em PROGMEM (Flash)
// Gerado automaticamente por tools/generate_assets.py
// Total: {total_bytes} bytes (~{total_bytes/1024:.1f} KB)
// =============================================================================

{bytes_to_c_array(logo_webp, "IMAGE_LOGO_WEBP")}

#endif // PORTAL_IMAGES_H
"""
    with open(HEADER_PATH, "w") as f:
        f.write(header_content)
    print(f"Header salvo com sucesso em: {HEADER_PATH}")

if __name__ == "__main__":
    main()
