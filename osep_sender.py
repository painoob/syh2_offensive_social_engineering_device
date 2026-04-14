#!/usr/bin/env python3
"""
OSEP-32 (SYH2) - Conversor e Enviador Automático de Imagens
============================================================
Monitora uma pasta local, converte qualquer imagem para o formato
aceito pelo display (320x240, JPEG baseline, qualidade 90) e envia
automaticamente via FTP para o dispositivo.

Dependências:
    pip install pillow watchdog

Uso:
    python osep_sender.py

    Ou com argumentos:
    python osep_sender.py --watch ./minhas_imagens --host 192.168.4.1
"""

import os
import sys
import time
import ftplib
import argparse
import threading
from io import BytesIO
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("[ERRO] Pillow não instalado. Execute: pip install pillow")
    sys.exit(1)

try:
    from watchdog.observers import Observer
    from watchdog.events import FileSystemEventHandler
except ImportError:
    print("[ERRO] Watchdog não instalado. Execute: pip install watchdog")
    sys.exit(1)

# ===== Configurações padrão =====
DEFAULT_HOST      = "192.168.4.1"
DEFAULT_PORT      = 21
DEFAULT_USER      = "osep"
DEFAULT_PASS      = "osep1234"
DEFAULT_REMOTE    = "/slides"
DEFAULT_WATCH_DIR = "./upload"
TARGET_WIDTH      = 320
TARGET_HEIGHT     = 240
JPEG_QUALITY      = 90

# Extensões aceitas como entrada
SUPPORTED_EXT = {".jpg", ".jpeg", ".png", ".bmp", ".gif", ".webp", ".tiff", ".tif"}


def get_exif_rotation(img):
    """
    Lê a orientação EXIF e retorna graus de rotação para corrigir a imagem.
    Retorna 0, 90, 180 ou 270.
    """
    try:
        exif = img._getexif()
        if exif is None:
            return 0
        orientation = exif.get(274)  # tag 274 = Orientation
        return {1: 0, 3: 180, 6: 270, 8: 90}.get(orientation, 0)
    except Exception:
        return 0


def convert_image(src_path: Path):
    """
    Converte qualquer imagem para JPEG 320x240 baseline qualidade 90.
    - Corrige rotação EXIF automaticamente (fotos de celular)
    - Se retrato (altura > largura), rotaciona 90° para paisagem
    Retorna um BytesIO com os dados prontos para enviar via FTP.
    """
    try:
        img = Image.open(src_path)

        # Corrige rotação EXIF (fotos de celular costumam ter isso)
        exif_rot = get_exif_rotation(img)
        if exif_rot != 0:
            img = img.rotate(exif_rot, expand=True)
            print(f"    [EXIF] Rotacao corrigida: {exif_rot} graus")

        # Converte para RGB (remove alpha, CMYK, etc)
        if img.mode != "RGB":
            img = img.convert("RGB")

        # Detecta retrato (altura > largura) e rotaciona 90° para paisagem
        if img.height > img.width:
            img = img.rotate(90, expand=True)
            print(f"    [AUTO] Retrato detectado -> rotacionado para paisagem")

        print(f"    [SRC]  Resolucao original: {img.width}x{img.height}")

        # Redimensiona mantendo proporção com letterbox preto
        img.thumbnail((TARGET_WIDTH, TARGET_HEIGHT), Image.LANCZOS)

        # Cria canvas 320x240 preto e centraliza a imagem
        canvas = Image.new("RGB", (TARGET_WIDTH, TARGET_HEIGHT), (0, 0, 0))
        offset_x = (TARGET_WIDTH  - img.width)  // 2
        offset_y = (TARGET_HEIGHT - img.height) // 2
        canvas.paste(img, (offset_x, offset_y))

        # Salva como JPEG baseline (subsampling=0 força 4:4:4, progressive=False = baseline)
        buf = BytesIO()
        canvas.save(
            buf,
            format="JPEG",
            quality=JPEG_QUALITY,
            optimize=False,
            progressive=False,   # baseline — obrigatório para TJpg_Decoder
            subsampling=0
        )
        buf.seek(0)
        return buf

    except Exception as e:
        print(f"  [ERRO] Falha ao converter {src_path.name}: {e}")
        return None


def send_via_ftp(buf: BytesIO, remote_name: str, host: str, port: int,
                 user: str, password: str, remote_dir: str) -> bool:
    """Envia um BytesIO via FTP para o dispositivo."""
    try:
        ftp = ftplib.FTP()
        ftp.connect(host, port, timeout=10)
        ftp.login(user, password)

        # Tenta navegar para o diretório remoto, cria se não existir
        try:
            ftp.cwd(remote_dir)
        except ftplib.error_perm:
            ftp.mkd(remote_dir)
            ftp.cwd(remote_dir)

        ftp.storbinary(f"STOR {remote_name}", buf)
        ftp.quit()
        return True

    except Exception as e:
        print(f"  [ERRO] FTP falhou: {e}")
        return False


def process_file(path: Path, host: str, port: int, user: str,
                 password: str, remote_dir: str, done_dir: Path | None):
    """Converte e envia um arquivo. Move para done_dir se bem-sucedido."""

    if path.suffix.lower() not in SUPPORTED_EXT:
        return

    print(f"\n[→] Processando: {path.name}")

    # Nome de destino sempre .jpg
    remote_name = path.stem + ".jpg"

    print(f"    Convertendo para {TARGET_WIDTH}x{TARGET_HEIGHT} JPEG baseline...")
    buf = convert_image(path)
    if buf is None:
        return

    size_kb = len(buf.getvalue()) / 1024
    print(f"    Tamanho convertido: {size_kb:.1f} KB")

    print(f"    Enviando via FTP → {host}{remote_dir}/{remote_name} ...")
    ok = send_via_ftp(buf, remote_name, host, port, user, password, remote_dir)

    if ok:
        print(f"    [✓] Enviado com sucesso!")
        if done_dir:
            done_dir.mkdir(parents=True, exist_ok=True)
            dest = done_dir / path.name
            # Evita sobrescrever arquivos com mesmo nome
            counter = 1
            while dest.exists():
                dest = done_dir / f"{path.stem}_{counter}{path.suffix}"
                counter += 1
            path.rename(dest)
            print(f"    [→] Movido para: {dest}")
        else:
            path.unlink()
            print(f"    [→] Arquivo original removido.")
    else:
        print(f"    [✗] Falha no envio. Arquivo mantido.")


class ImageHandler(FileSystemEventHandler):
    """Watchdog handler — detecta novos arquivos na pasta monitorada."""

    def __init__(self, host, port, user, password, remote_dir, done_dir):
        self.host       = host
        self.port       = port
        self.user       = user
        self.password   = password
        self.remote_dir = remote_dir
        self.done_dir   = done_dir
        self._pending   = set()
        self._lock      = threading.Lock()

    def on_created(self, event):
        if event.is_directory:
            return
        path = Path(event.src_path)
        if path.suffix.lower() not in SUPPORTED_EXT:
            return
        # Espera 1s para o arquivo terminar de ser copiado/salvo
        threading.Timer(1.0, self._handle, args=[path]).start()

    def on_moved(self, event):
        """Detecta arquivos arrastados para a pasta."""
        if event.is_directory:
            return
        path = Path(event.dest_path)
        if path.suffix.lower() not in SUPPORTED_EXT:
            return
        threading.Timer(1.0, self._handle, args=[path]).start()

    def _handle(self, path: Path):
        with self._lock:
            if str(path) in self._pending:
                return
            self._pending.add(str(path))

        try:
            if path.exists():
                process_file(path, self.host, self.port, self.user,
                             self.password, self.remote_dir, self.done_dir)
        finally:
            with self._lock:
                self._pending.discard(str(path))


def scan_existing(watch_dir: Path, host, port, user, password, remote_dir, done_dir):
    """Processa arquivos já existentes na pasta ao iniciar."""
    files = [f for f in watch_dir.iterdir()
             if f.is_file() and f.suffix.lower() in SUPPORTED_EXT]
    if files:
        print(f"\n[i] {len(files)} arquivo(s) encontrado(s) na pasta. Processando...\n")
        for f in sorted(files):
            process_file(f, host, port, user, password, remote_dir, done_dir)


def main():
    parser = argparse.ArgumentParser(
        description="OSEP-32 — Conversor e enviador automático de imagens via FTP"
    )
    parser.add_argument("--host",    default=DEFAULT_HOST,      help=f"IP do dispositivo (padrão: {DEFAULT_HOST})")
    parser.add_argument("--port",    default=DEFAULT_PORT,       type=int, help=f"Porta FTP (padrão: {DEFAULT_PORT})")
    parser.add_argument("--user",    default=DEFAULT_USER,      help=f"Usuário FTP (padrão: {DEFAULT_USER})")
    parser.add_argument("--pass",    default=DEFAULT_PASS,      dest="password", help=f"Senha FTP (padrão: {DEFAULT_PASS})")
    parser.add_argument("--remote",  default=DEFAULT_REMOTE,    help=f"Pasta remota (padrão: {DEFAULT_REMOTE})")
    parser.add_argument("--watch",   default=DEFAULT_WATCH_DIR, help=f"Pasta local a monitorar (padrão: {DEFAULT_WATCH_DIR})")
    parser.add_argument("--done",    default=None,              help="Pasta para mover arquivos enviados (padrão: apaga o original)")
    parser.add_argument("--no-watch", action="store_true",      help="Processa arquivos existentes e sai, sem monitorar")
    args = parser.parse_args()

    watch_dir = Path(args.watch)
    done_dir  = Path(args.done) if args.done else None

    watch_dir.mkdir(parents=True, exist_ok=True)

    print("=" * 55)
    print("  OSEP-32 (SYH2) — Conversor/Enviador FTP")
    print("=" * 55)
    print(f"  Dispositivo : {args.host}:{args.port}")
    print(f"  Usuário FTP : {args.user}")
    print(f"  Pasta remota: {args.remote}")
    print(f"  Monitorando : {watch_dir.resolve()}")
    if done_dir:
        print(f"  Enviados →  : {done_dir.resolve()}")
    else:
        print(f"  Enviados →  : [arquivo original apagado]")
    print("=" * 55)
    print("\nFormato de saída: 320x240, JPEG baseline, qualidade 90")
    print("Extensões aceitas:", ", ".join(sorted(SUPPORTED_EXT)))
    print()

    # Processa arquivos já na pasta
    scan_existing(watch_dir, args.host, args.port, args.user,
                  args.password, args.remote, done_dir)

    if args.no_watch:
        print("\n[i] Modo --no-watch: encerrando.")
        return

    # Inicia monitoramento contínuo
    handler  = ImageHandler(args.host, args.port, args.user,
                            args.password, args.remote, done_dir)
    observer = Observer()
    observer.schedule(handler, str(watch_dir), recursive=False)
    observer.start()

    print(f"\n[✓] Monitorando '{watch_dir}' — arraste imagens para enviar automaticamente.")
    print("    Pressione Ctrl+C para sair.\n")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n[i] Encerrando...")
        observer.stop()

    observer.join()
    print("[i] Encerrado.")


if __name__ == "__main__":
    main()
