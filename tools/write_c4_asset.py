"""Install C4 or DS dance assets into W25Q64 via ST-Link. Run only with the safe flashing wiring.

Backs up the affected external-flash sectors before erasing. The temporary
programmer runs from RAM and never erases/writes the MCU firmware, recordings
or settings. The asset header is committed last, after payload verification.
"""
import argparse
from datetime import datetime
import hashlib
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import tempfile
import time

from import_c4 import fnv

BASE, END, BUFFER, MAILBOX = 0x100000, 0x200000, 0x20001000, 0x20000F00
CHUNK = 12288


def validate_asset(data):
    if len(data) < 33 or len(data) > END - BASE:
        raise ValueError("Invalid asset size")
    magic, rate, count, checksum = struct.unpack_from("<4I", data)
    if magic != 0x314D4350 or rate != 16000 or count != len(data) - 32 or fnv(data[32:]) != checksum:
        raise ValueError("Invalid PCM header or checksum")
    return (len(data) + 4095) // 4096 * 4096


class OpenOcd:
    def __init__(self, executable, log):
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        self.process = subprocess.Popen(
            [executable, "-f", "interface/stlink.cfg", "-f", "target/stm32f1x.cfg",
             "-c", f"tcl_port {port}", "-c", "gdb_port disabled",
             "-c", "telnet_port disabled", "-c", "init"], stdout=log, stderr=log)
        try:
            for _ in range(100):
                if self.process.poll() is not None:
                    raise RuntimeError("OpenOCD exited; see openocd.log")
                try:
                    self.socket = socket.create_connection(("127.0.0.1", port), timeout=0.2)
                    self.socket.settimeout(30)
                    break
                except OSError:
                    time.sleep(0.1)
            else:
                raise RuntimeError("OpenOCD did not start")
        except BaseException:
            self.process.terminate()
            self.process.wait(timeout=5)
            raise

    def call(self, command):
        # Tcl catch preserves errors which the raw RPC protocol otherwise hides.
        wrapped = f'if {{[catch {{{command}}} result]}} {{set result "ERROR:$result"}}; set result'
        self.socket.sendall(wrapped.encode() + b"\x1a")
        response = bytearray()
        while not response.endswith(b"\x1a"):
            part = self.socket.recv(65536)
            if not part:
                raise RuntimeError("OpenOCD disconnected")
            response.extend(part)
        result = response[:-1].decode(errors="replace")
        if result.startswith("ERROR:"):
            raise RuntimeError(result)
        return result

    def word(self, address):
        result = self.call(f"mdw 0x{address:x} 1")
        match = re.search(r"0x[0-9a-fA-F]+:\s+([0-9a-fA-F]+)", result)
        if not match:
            raise RuntimeError(f"Unexpected memory reply: {result}")
        return int(match[1], 16)

    def command(self, opcode, address, length, checksum=0):
        self.call(f"halt; mww 0x{MAILBOX+8:x} 0x{address:x}; "
                  f"mww 0x{MAILBOX+12:x} {length}; mww 0x{MAILBOX+16:x} 0x{checksum:x}; "
                  f"mww 0x{MAILBOX+4:x} 1; mww 0x{MAILBOX:x} {opcode}; resume")
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            status = self.word(MAILBOX + 4)
            if status == 2:
                return
            if status not in (1,):
                raise RuntimeError(f"W25Q64 operation failed: status=0x{status:x}")
            time.sleep(0.02)
        raise TimeoutError("W25Q64 operation timed out")

    def close(self):
        try:
            self.call("reset run")
        finally:
            self.socket.close()
            self.process.terminate()
            self.process.wait(timeout=5)


def build_loader(root, dest, base=BASE, end=END):
    gcc = shutil.which("arm-none-eabi-gcc")
    if not gcc:
        raise RuntimeError("arm-none-eabi-gcc not found")
    elf, binary = dest / "loader.elf", dest / "loader.bin"
    subprocess.run([gcc, "-mcpu=cortex-m3", "-mthumb", "-Os", "-nostdlib",
                    "-ffreestanding", "-fno-builtin", "-Wall", "-Wextra", "-Werror",
                    "-DSTM32F103xB", f"-DW25_ASSET_BASE={base:#x}U",
                    f"-DW25_ASSET_END={end:#x}U", "-I" + str(root / "Drivers/CMSIS/Include"),
                    "-I" + str(root / "Drivers/CMSIS/Device/ST/STM32F1xx/Include"),
                    "-T", str(root / "tools/c4_flash_loader.ld"),
                    str(root / "tools/c4_flash_loader.c"), "-o", str(elf)], check=True)
    objcopy = str(Path(gcc).with_name("arm-none-eabi-objcopy.exe"))
    subprocess.run([objcopy, "-O", "binary", str(elf), str(binary)], check=True)
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--openocd", default=shutil.which("openocd") or str(
        Path.home() / "Tools/xpack-openocd-0.12.0-7/bin/openocd.exe"))
    parser.add_argument("--output", type=Path, required=True, help="Backup and operation logs directory")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--ds-lion", action="store_true", help="Install DS lion dance frames")
    group.add_argument("--ds-dance", action="store_true", help="Install DS dance assets instead of C4")
    parser.add_argument("--build-only", action="store_true", help="Compile RAM loader; do not connect hardware")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if args.ds_dance or args.ds_lion:
        if args.ds_lion:
            base, end, label, filename, tag = 0x208000, 0x308000, "ds_lion", "ds_room_lion.bin", b"DSL1"
        else:
            base, end, label, filename, tag = 0x200000, 0x208000, "ds_dance", "ds_room_dance.bin", b"DSD1"
        header_bytes = 12
        data = (root / "assets" / filename).read_bytes()
        if not header_bytes < len(data) <= end - base:
            raise ValueError("Invalid dance asset size")
        magic, size, expected = struct.unpack_from("<4sII", data)
        if magic != tag or size != len(data) - header_bytes or fnv(data[header_bytes:]) != expected:
            raise ValueError("Invalid dance asset header or checksum")
        span = (len(data) + 4095) // 4096 * 4096
        end = base + span
    else:
        base, end = BASE, END
        label, header_bytes = "c4", 32
        data = (root / "assets/c4.pcm.bin").read_bytes()
        span = validate_asset(data)
    args.output.mkdir(parents=True, exist_ok=True)
    backup_path = args.output / f"w25_{label}_before.bin"
    if backup_path.exists() and not args.build_only:
        raise SystemExit("Use a new output directory; refusing to overwrite a backup")
    with tempfile.TemporaryDirectory(prefix=f"smarttone_{label}_") as work:
        work = Path(work)
        binary = build_loader(root, work, base, end)
        if args.build_only:
            shutil.copy2(binary, args.output / f"{label}_ram_loader.bin")
            print(f"RAM loader compiled, {binary.stat().st_size} bytes; hardware untouched")
            return
        with (args.output / "openocd.log").open("w", encoding="utf-8") as log:
            debug = OpenOcd(args.openocd, log)
            try:
                debug.call(f"reset halt; load_image {{{binary.as_posix()}}} 0x20000000 bin; resume 0x20000000")
                time.sleep(0.2)
                if debug.word(MAILBOX + 4) != 0x10:
                    raise RuntimeError("RAM programmer did not start")
                chunk_file = work / "chunk.bin"
                with backup_path.open("xb") as backup:
                    for at in range(0, span, CHUNK):
                        length = min(CHUNK, span - at)
                        debug.command(3, base + at, length)
                        debug.call(f"halt; dump_image {{{chunk_file.as_posix()}}} 0x{BUFFER:x} {length}; resume")
                        old = chunk_file.read_bytes()
                        if len(old) != length:
                            raise RuntimeError("Incomplete external-flash backup")
                        backup.write(old)
                print(f"Backed up {span} bytes to {backup_path}", flush=True)
                for at in range(0, span, 4096):
                    debug.command(1, base + at, 4096)
                # Commit the header after verifying the payload.
                blocks = [(at, data[at:at+CHUNK]) for at in range(header_bytes, len(data), CHUNK)]
                blocks.append((0, data[:header_bytes]))
                for at, block in blocks:
                    chunk_file.write_bytes(block)
                    debug.call(f"halt; load_image {{{chunk_file.as_posix()}}} 0x{BUFFER:x} bin; resume")
                    debug.command(2, base + at, len(block), fnv(block))
                    print(f"Written and verified {at + len(block)}/{len(data)}", flush=True)
                report = (f"{datetime.now().isoformat()}\nAsset 0x{base:x}, {len(data)} bytes\n"
                          f"Asset SHA256 {hashlib.sha256(data).hexdigest()}\n"
                          f"Backup SHA256 {hashlib.sha256(backup_path.read_bytes()).hexdigest()}\n"
                          "Each block read back and FNV-1a verified; header committed last.\n"
                          "MCU firmware, recordings and settings not written.\n")
                (args.output / "asset_write.txt").write_text(report, encoding="utf-8")
            finally:
                debug.close()


if __name__ == "__main__":
    main()
