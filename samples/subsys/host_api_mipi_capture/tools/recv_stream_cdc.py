#!/usr/bin/env python3
from __future__ import annotations

import argparse
import contextlib
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

import serial
from serial import SerialException

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

if len(Path(__file__).resolve().parents) > 4:
    REPO_ROOT = Path(__file__).resolve().parents[4]
    sys.path.insert(0, str(REPO_ROOT / "tools" / "scripts" / "host_api"))

JPEG_TAG = b"JPEG"
JPEG_COL_SIZE = 416
JPEG_ROW_SIZE = 82
JPEG_PAYLOAD_SIZE = JPEG_COL_SIZE * JPEG_ROW_SIZE
WINDOWS_COM_RE = re.compile(r"^COM\d+$", re.IGNORECASE)


def normalize_port_name(port: str) -> str:
    if os.name == "nt" and WINDOWS_COM_RE.match(port):
        if int(port[3:]) >= 10:
            return rf"\\.\{port.upper()}"
    return port


def maybe_proxy_to_windows() -> None:
    if os.name == "nt":
        return

    argv = sys.argv
    port = None
    for idx, arg in enumerate(argv[1:], start=1):
        if arg == "--port" and idx + 1 < len(argv):
            port = argv[idx + 1]
            break

    if not port or not WINDOWS_COM_RE.match(port):
        return

    python_exe = shutil.which("python.exe") or shutil.which("py.exe")
    if python_exe is None:
        raise RuntimeError("Windows python.exe/py.exe not found in PATH for COM proxy")

    win_script = subprocess.run(
        ["wslpath", "-w", os.path.abspath(__file__)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()

    cmd = [python_exe]
    if os.path.basename(python_exe).lower() == "py.exe":
        cmd.append("-3")
    cmd.extend([win_script, *argv[1:]])

    raise SystemExit(subprocess.run(cmd).returncode)


def read_exact(ser, size: int) -> bytes:
    buf = bytearray()
    while len(buf) < size:
        chunk = ser.read(size - len(buf))
        if not chunk:
            return bytes(buf)
        buf.extend(chunk)
    return bytes(buf)


def extract_clean_jpeg(payload: bytes) -> bytes:
    start = payload.find(b"\xff\xd8")
    if start == -1:
        raise RuntimeError("JPEG SOI marker not found in payload")

    for idx in range(start + 2, len(payload)):
        if payload[idx - 1] == 0xFF and payload[idx] == 0xD9:
            return payload[start:idx + 1]

    raise RuntimeError("JPEG EOI marker not found in payload")


def recover_stream(ser: serial.Serial) -> None:
    try:
        ser.reset_input_buffer()
    except Exception:
        pass
    time.sleep(0.1)


@contextlib.contextmanager
def suppress_stderr():
    saved_stderr_fd = os.dup(2)
    try:
        with open(os.devnull, "w", encoding="utf-8") as devnull:
            os.dup2(devnull.fileno(), 2)
            yield
    finally:
        os.dup2(saved_stderr_fd, 2)
        os.close(saved_stderr_fd)


def read_tagged_jpeg(ser: serial.Serial, idle_exit_seconds: float | None = None) -> bytes:
    window = bytearray()
    idle_start = None

    while True:
        chunk = read_exact(ser, 1)
        if not chunk:
            if idle_exit_seconds is not None:
                if idle_start is None:
                    idle_start = time.monotonic()
                elif (time.monotonic() - idle_start) >= idle_exit_seconds:
                    raise TimeoutError("stream idle timeout")
            continue
        idle_start = None
        window += chunk
        if len(window) > len(JPEG_TAG):
            window = window[-len(JPEG_TAG):]
        if bytes(window) == JPEG_TAG:
            break

    size_hdr = read_exact(ser, 4)
    if len(size_hdr) != 4:
        raise TimeoutError("timed out while reading JPEG size header")
    col_size = (size_hdr[0] << 8) | size_hdr[1]
    row_size = (size_hdr[2] << 8) | size_hdr[3]
    payload = read_exact(ser, col_size * row_size)
    if len(payload) != col_size * row_size:
        raise TimeoutError("timed out while reading JPEG payload")
    return extract_clean_jpeg(payload)


def main() -> int:
    maybe_proxy_to_windows()
    ap = argparse.ArgumentParser(
        description="Receive JPEG-tagged frames from host_api_mipi_capture stream CDC"
    )
    ap.add_argument("--port", help="e.g. /dev/ttyACM1")
    ap.add_argument("--usb", action="store_true",
                    help="Use direct USB access instead of a serial /dev/ttyACM* device (WSL-friendly)")
    ap.add_argument("--usb-vid", type=lambda v: int(v, 0), default=0xCAFE,
                    help="USB vendor ID for --usb mode (default: 0xCAFE)")
    ap.add_argument("--usb-pid", type=lambda v: int(v, 0), default=0x4002,
                    help="USB product ID for --usb mode (default: 0x4002)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=10.0)
    ap.add_argument("--outdir", default="/tmp/host_api_mipi_stream")
    ap.add_argument("--save-latest", action="store_true", help="continuously overwrite latest.jpg")
    ap.add_argument("--show", action="store_true", help="display frames with OpenCV")
    ap.add_argument(
        "--idle-exit-seconds",
        type=float,
        default=2.0,
        help="Exit after this many idle seconds once at least one frame was received",
    )
    args = ap.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    cv2 = None
    np = None
    if args.show:
        import cv2  # type: ignore
        import numpy as np  # type: ignore

    frame_count = 0
    start = time.time()

    if not args.usb and not args.port:
        ap.error("--port is required unless --usb is used")

    if args.usb:
        from usb_cdc_transport import UsbCdcTransport

        ser_ctx = UsbCdcTransport(
            vid=args.usb_vid,
            pid=args.usb_pid,
            channel=1,
            timeout_s=args.timeout,
            settle_ms=1000,
            baudrate=args.baud,
        )
    else:
        ser_ctx = serial.Serial(normalize_port_name(args.port), args.baud, timeout=args.timeout)

    with ser_ctx as ser:
        if not args.usb:
            ser.dtr = False
            try:
                ser.reset_input_buffer()
            except Exception:
                pass
            time.sleep(0.1)
            ser.dtr = True

        while True:
            try:
                idle_exit_seconds = args.idle_exit_seconds if frame_count > 0 else None
                jpeg = read_tagged_jpeg(ser, idle_exit_seconds=idle_exit_seconds)
            except (RuntimeError, TimeoutError, SerialException) as exc:
                if isinstance(exc, TimeoutError) and str(exc) == "stream idle timeout":
                    print("stream idle timeout reached; closing viewer")
                    break
                print(f"WARN: stream resync after error: {exc}", file=sys.stderr)
                recover_stream(ser)
                continue

            frame_count += 1
            elapsed = max(time.time() - start, 1e-6)
            fps = frame_count / elapsed
            print(f"frame={frame_count} bytes={len(jpeg)} fps={fps:.1f}")

            if args.save_latest:
                (outdir / "latest.jpg").write_bytes(jpeg)

            if args.show:
                with suppress_stderr():
                    img = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
                if img is not None:
                    cv2.putText(img, f"frame {frame_count}", (10, 30),
                                cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 255, 0), 2)
                    cv2.imshow("Host API MIPI JPEG Stream", img)
                    if cv2.waitKey(1) & 0xFF == ord("q"):
                        break

    if args.show and cv2 is not None:
        cv2.destroyAllWindows()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
