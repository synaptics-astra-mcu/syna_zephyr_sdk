#!/usr/bin/env python3
import argparse, struct, zlib
from pathlib import Path
import serial

MAGIC = 0x51425553  # "QBUS"
HDR_FMT = "<I H B B I I"
HDR_SZ = struct.calcsize(HDR_FMT)

WIN_NAME = "SR100 Quadrants"
_win_inited = False

def read_exact(ser, n):
    buf = bytearray()
    while len(buf) < n:
        chunk = ser.read(n - len(buf))
        if not chunk:
            raise TimeoutError("serial read timeout")
        buf += chunk
    return bytes(buf)

def read_hdr_resync(ser):
    """
    Read a header and resync if the stream is misaligned.
    We scan for MAGIC and version==1 at the start of a 16-byte window.
    """
    buf = bytearray(read_exact(ser, HDR_SZ))
    while True:
        magic, ver, qid, _flags, length, _crc = struct.unpack(HDR_FMT, buf)
        if magic == MAGIC and ver == 1:
            return magic, ver, qid, _flags, length, _crc
        # Slide window by 1 byte and pull 1 more.
        buf = buf[1:] + read_exact(ser, 1)

def show_jpeg(jpeg: bytes, title: str, *, win_x: int = 50, win_y: int = 50):
    # Display using OpenCV only (no PIL fallback) for consistent behavior.
    import cv2
    import numpy as np

    global _win_inited

    img = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
    if img is None:
        raise ValueError("OpenCV failed to decode JPEG")

    if not _win_inited:
        cv2.namedWindow(WIN_NAME, cv2.WINDOW_AUTOSIZE)
        cv2.moveWindow(WIN_NAME, int(win_x), int(win_y))
        _win_inited = True

    # Add a small title overlay so the user knows which quadrant is shown.
    try:
        cv2.putText(img, title, (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 255, 0), 2)
    except Exception:
        pass

    cv2.imshow(WIN_NAME, img)
    # Block until a key press so each quadrant is visible.
    cv2.waitKey(0)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="e.g. /dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=10.0, help="Serial read timeout (seconds)")
    ap.add_argument("--outdir", default="/tmp/cdc_dumps")
    ap.add_argument("--show", action="store_true")
    ap.add_argument("--win-x", type=int, default=50, help="OpenCV window X position")
    ap.add_argument("--win-y", type=int, default=50, help="OpenCV window Y position")
    ap.add_argument("--toggle-dtr", action="store_true",
                    help="Toggle DTR low->high on open (recommended)")
    args = ap.parse_args()

    outdir = Path(args.outdir); outdir.mkdir(parents=True, exist_ok=True)

    with serial.Serial(args.port, args.baud, timeout=args.timeout) as ser:
        # Host DTR triggers the device to start sending. Some host stacks
        # don't emit a control-line-state change unless DTR actually toggles.
        if args.toggle_dtr:
            ser.dtr = False
            try:
                ser.reset_input_buffer()
            except Exception:
                pass
            import time
            time.sleep(0.1)
        ser.dtr = True

        for _ in range(4):
            magic, ver, qid, _, length, _crc = read_hdr_resync(ser)

            jpeg = read_exact(ser, length)
            out = outdir / f"q{qid}.jpg"
            out.write_bytes(jpeg)
            print(f"Wrote {out} ({length} bytes)")

            if args.show:
                try:
                    show_jpeg(jpeg, f"Quadrant {qid}", win_x=args.win_x, win_y=args.win_y)
                except Exception as e:
                    print(f"Warning: failed to display quadrant {qid}: {e}")

        if args.show:
            try:
                import cv2
                cv2.destroyAllWindows()
            except Exception:
                pass

if __name__ == "__main__":
    main()
