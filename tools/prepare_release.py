#!/usr/bin/env python3
"""
OpenAir-AQStation - Preparador de Pacote de Release do Firmware ESP32
Gera os binários consolidados (merged), scripts de gravação, documentação e ZIP.
"""

import os
import sys
import shutil
import hashlib
import zipfile
import subprocess
from pathlib import Path

WORKSPACE_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = WORKSPACE_ROOT / ".pio" / "build" / "esp32doit-devkit-v1"
RELEASE_DIR = WORKSPACE_ROOT / "release"

INSTRUCOES_TXT = """================================================================================
OpenAir-AQStation - Firmware ESP32 DOIT DevKit v1
Instruções de Gravação (Flashing Guide)
================================================================================

Este pacote contém os binários compilados para a estação de monitoramento de
qualidade do ar OpenAir-AQStation baseada no microcontrolador ESP32-WROOM-32
(ESP32 DOIT DevKit v1 / CH9102X / CP2102).

CONTEÚDO DO PACOTE:
-------------------
1. firmware-merged.bin     -> Binário Único Consolidado (Gravação direta no offset 0x0)
2. firmware.bin            -> Binário da aplicação principal (offset 0x10000)
3. bootloader.bin          -> Bootloader de 2º estágio da Espressif (offset 0x1000)
4. partitions.bin          -> Tabela de partições NVS/SPIFFS/App (offset 0x8000)
5. boot_app0.bin           -> Seletor de partição de boot OTA (offset 0xe000)
6. flash.sh                -> Script de gravação rápida para Linux e macOS
7. flash.bat               -> Script de gravação rápida para Windows

--------------------------------------------------------------------------------
MÉTODO 1: GRAVAÇÃO RÁPIDA VIA BINÁRIO ÚNICO (RECOMENDADO)
--------------------------------------------------------------------------------
O arquivo 'firmware-merged.bin' une todos os blocos em um único binário, permitindo
a gravação com apenas um comando no offset 0x0 ou através de gravadores Web
(como Adafruit WebSerial ESPTool ou ESP Web Tools no Chrome/Edge).

No Linux / macOS:
  chmod +x flash.sh
  ./flash.sh /dev/ttyUSB0
  (ou via esptool direto):
  esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 write_flash 0x0 firmware-merged.bin

No Windows:
  flash.bat COM3
  (ou via esptool direto):
  esptool.py --chip esp32 --port COM3 --baud 921600 write_flash 0x0 firmware-merged.bin

--------------------------------------------------------------------------------
MÉTODO 2: GRAVAÇÃO MODULAR (ARQUIVOS SEPARADOS)
--------------------------------------------------------------------------------
Caso prefira gravar os blocos separadamente:

esptool.py --chip esp32 --port <PORTA> --baud 921600 write_flash \\
  0x1000  bootloader.bin \\
  0x8000  partitions.bin \\
  0xe000  boot_app0.bin \\
  0x10000 firmware.bin

--------------------------------------------------------------------------------
PRÓXIMOS PASSOS APÓS A GRAVAÇÃO:
--------------------------------------------------------------------------------
1. Abra o Monitor Serial a 115200 bps:
   pio device monitor -b 115200
2. Conecte-se ao Wi-Fi criado pela placa:
   SSID:  OpenAir-Station-XXXX (onde XXXX são os últimos dígitos do MAC)
   Senha: openair123
3. Acesse http://192.168.4.1 no navegador para configurar a rede e o papel
   da estação (Matriz ou Satélite).
================================================================================
"""

FLASH_SH = """#!/usr/bin/env bash
PORT="${1:-/dev/ttyUSB0}"
BAUD="${2:-921600}"

echo "=========================================================="
echo "OpenAir-AQStation - Gravador de Firmware ESP32"
echo "Porta: $PORT | Baudrate: $BAUD"
echo "=========================================================="

if ! command -v esptool.py &> /dev/null && ! command -v esptool &> /dev/null; then
  echo "ERRO: esptool.py não encontrado. Instale com: pip install esptool"
  exit 1
fi

ESPTOOL_BIN="esptool.py"
if ! command -v esptool.py &> /dev/null; then
  ESPTOOL_BIN="esptool"
fi

$ESPTOOL_BIN --chip esp32 --port "$PORT" --baud "$BAUD" write_flash 0x0 firmware-merged.bin
"""

FLASH_BAT = """@echo off
setlocal
set PORT=%1
if "%PORT%"=="" set PORT=COM3
set BAUD=%2
if "%BAUD%"=="" set BAUD=921600

echo ==========================================================
echo OpenAir-AQStation - Gravador de Firmware ESP32
echo Porta: %PORT% ^| Baudrate: %BAUD%
echo ==========================================================

where esptool.py >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
  where esptool >nul 2>nul
  if %ERRORLEVEL% NEQ 0 (
    echo ERRO: esptool nao encontrado no PATH. Instale com: pip install esptool
    pause
    exit /b 1
  )
  set ESPTOOL_BIN=esptool
) else (
  set ESPTOOL_BIN=esptool.py
)

%ESPTOOL_BIN% --chip esp32 --port %PORT% --baud %BAUD% write_flash 0x0 firmware-merged.bin
pause
"""


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()


def find_boot_app0() -> Path | None:
    home = Path.home()
    candidates = list((home / ".platformio").glob("**/boot_app0.bin"))
    if candidates:
        return candidates[0]
    return None


def find_esptool() -> str:
    # 1. PATH esptool.py ou esptool
    for name in ["esptool.py", "esptool"]:
        p = shutil.which(name)
        if p:
            return p

    # 2. PlatformIO packages
    home = Path.home()
    pio_esptools = list((home / ".platformio").glob("**/esptool.py"))
    if pio_esptools:
        return f"{sys.executable} {pio_esptools[0]}"

    return "esptool.py"


def main():
    print("==================================================")
    print(" Preparando Pacote de Artefatos para Release ")
    print("==================================================")

    if not BUILD_DIR.exists():
        print(f"ERRO: Diretório de build não encontrado em {BUILD_DIR}")
        print("Execute 'pio run -e esp32doit-devkit-v1' antes deste script.")
        sys.exit(1)

    required_binaries = ["bootloader.bin", "partitions.bin", "firmware.bin"]
    for b in required_binaries:
        if not (BUILD_DIR / b).exists():
            print(f"ERRO: Binário necessário '{b}' não encontrado em {BUILD_DIR}")
            sys.exit(1)

    # Limpar/Criar pasta release
    if RELEASE_DIR.exists():
        shutil.rmtree(RELEASE_DIR)
    RELEASE_DIR.mkdir(parents=True, exist_ok=True)

    # Copiar binários obrigatórios
    for b in required_binaries:
        src = BUILD_DIR / b
        dst = RELEASE_DIR / b
        shutil.copy2(src, dst)
        print(f"  [OK] Copiado {b} ({src.stat().st_size:,} bytes)")

    # Copiar boot_app0.bin
    boot_app0 = find_boot_app0()
    has_boot_app0 = False
    if boot_app0 and boot_app0.exists():
        shutil.copy2(boot_app0, RELEASE_DIR / "boot_app0.bin")
        has_boot_app0 = True
        print(f"  [OK] Copiado boot_app0.bin de {boot_app0}")
    else:
        print("  [AVISO] boot_app0.bin não encontrado, prosseguindo sem ele.")

    # Escrever documentação e scripts
    (RELEASE_DIR / "INSTRUCOES_GRAVACAO.txt").write_text(INSTRUCOES_TXT, encoding="utf-8")
    print("  [OK] Gerado INSTRUCOES_GRAVACAO.txt")

    flash_sh_path = RELEASE_DIR / "flash.sh"
    flash_sh_path.write_text(FLASH_SH, encoding="utf-8")
    flash_sh_path.chmod(0o755)
    print("  [OK] Gerado flash.sh (executável)")

    (RELEASE_DIR / "flash.bat").write_text(FLASH_BAT, encoding="utf-8")
    print("  [OK] Gerado flash.bat")

    # Gerar firmware-merged.bin
    merged_bin = RELEASE_DIR / "firmware-merged.bin"
    esptool_cmd = find_esptool()
    print(f"  [INFO] Usando esptool: {esptool_cmd}")

    cmd = (
        f"{esptool_cmd} --chip esp32 merge_bin -o {merged_bin} "
        f"--flash_mode dio --flash_freq 40m --flash_size 4MB "
        f"0x1000 {RELEASE_DIR}/bootloader.bin "
        f"0x8000 {RELEASE_DIR}/partitions.bin "
    )
    if has_boot_app0:
        cmd += f"0xe000 {RELEASE_DIR}/boot_app0.bin "
    cmd += f"0x10000 {RELEASE_DIR}/firmware.bin"

    print("  [EXEC] Mesclando binários com esptool merge_bin...")
    res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"  [AVISO] Falha ao gerar firmware-merged.bin:\n{res.stderr}\n{res.stdout}")
    else:
        print(f"  [OK] firmware-merged.bin criado com sucesso ({merged_bin.stat().st_size:,} bytes)")

    # Gerar arquivo ZIP com todos os arquivos de release
    zip_filename = "OpenAir-AQStation-ESP32-Release.zip"
    zip_path = RELEASE_DIR / zip_filename
    print(f"  [COMPACTAR] Gerando {zip_filename}...")

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for item in sorted(RELEASE_DIR.iterdir()):
            if item.name == zip_filename:
                continue
            zf.write(item, arcname=item.name)
            print(f"    + Adicionado: {item.name}")

    print(f"  [OK] Pacote ZIP criado com sucesso: {zip_path.stat().st_size:,} bytes")

    # Sumário de Verificação de Integridade
    print("\n--------------------------------------------------")
    print(" Sumário dos Artefatos de Release Gerados:")
    print("--------------------------------------------------")
    for item in sorted(RELEASE_DIR.iterdir()):
        size_kb = item.stat().st_size / 1024
        sha = sha256_file(item)
        print(f"{item.name:<32} {size_kb:>8.2f} KB | SHA256: {sha[:16]}...")
    print("==================================================")
    print(" Pacote pronto para publicação em Release!")
    print("==================================================")


if __name__ == "__main__":
    main()
