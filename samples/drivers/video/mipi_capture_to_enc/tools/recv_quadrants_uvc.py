#!/usr/bin/env python3
import argparse
import os
import queue
import select
import subprocess
import threading
import time
from typing import Dict, Iterator, Optional, Tuple

import cv2


def _iter_jpegs_from_stream(chunks: Iterator[bytes]) -> Iterator[bytes]:
    buf = bytearray()
    soi = b"\xff\xd8"
    eoi = b"\xff\xd9"

    for chunk in chunks:
        if not chunk:
            continue
        buf.extend(chunk)

        while True:
            start = buf.find(soi)
            if start < 0:
                if len(buf) > (4 * 1024 * 1024):
                    del buf[:-2]
                break
            if start > 0:
                del buf[:start]

            end = buf.find(eoi, 2)
            if end < 0:
                break
            jpeg = bytes(buf[: end + 2])
            del buf[: end + 2]
            yield jpeg


def _parse_quadrant_from_com(jpeg: bytes) -> Optional[Tuple[int, str]]:
    if len(jpeg) < 10 or jpeg[0:2] != b"\xff\xd8":
        return None

    idx = 2
    for _ in range(32):
        if idx + 4 > len(jpeg) or jpeg[idx] != 0xFF:
            return None
        marker = jpeg[idx + 1]
        if marker == 0xDA:
            return None
        seg_len = (jpeg[idx + 2] << 8) | jpeg[idx + 3]
        if seg_len < 2:
            return None
        payload_start = idx + 4
        payload_end = idx + 2 + seg_len
        if payload_end > len(jpeg):
            return None

        if marker == 0xFE:
            payload = jpeg[payload_start:payload_end].decode("ascii", errors="ignore")
            qid = None
            name = ""
            for part in payload.split(";"):
                part = part.strip()
                if part.startswith("qid="):
                    try:
                        qid = int(part.split("=", 1)[1], 10)
                    except Exception:
                        qid = None
                elif part.startswith("name="):
                    name = part.split("=", 1)[1]
            if qid is None or qid not in (0, 1, 2, 3):
                return None
            return qid, name

        idx = payload_end

    return None


def _ffmpeg_chunks(dev: str, width: int, height: int, fps: int, *, idle_timeout_s: float) -> Iterator[bytes]:
    if os.name == "nt":
        input_fmt = "dshow"
    else:
        input_fmt = "v4l2"

    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-f", input_fmt]
    if input_fmt == "dshow":
        cmd += ["-vcodec", "mjpeg"]
    elif input_fmt == "v4l2":
        cmd += ["-input_format", "mjpeg"]
    cmd += [
        "-video_size", f"{width}x{height}",
        "-framerate", str(fps),
        "-i", dev,
        "-c:v", "copy",
        "-f", "mjpeg",
        "pipe:1",
    ]

    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    except FileNotFoundError:
        raise SystemExit("ffmpeg was not found. Install FFmpeg and add ffmpeg.exe to PATH.") from None

    assert proc.stdout is not None
    try:
        last_data = time.time()
        if os.name == "nt":
            chunks: "queue.Queue[Optional[bytes]]" = queue.Queue()

            def reader() -> None:
                while True:
                    chunk = proc.stdout.read(64 * 1024)
                    if not chunk:
                        chunks.put(None)
                        return
                    chunks.put(chunk)

            threading.Thread(target=reader, daemon=True).start()
            while True:
                try:
                    chunk = chunks.get(timeout=0.25)
                except queue.Empty:
                    if (time.time() - last_data) > idle_timeout_s:
                        break
                    continue
                if chunk is None:
                    break
                last_data = time.time()
                yield chunk
        else:
            while True:
                ready, _w, _x = select.select([proc.stdout], [], [], 0.25)
                if not ready:
                    if (time.time() - last_data) > idle_timeout_s:
                        break
                    continue

                chunk = proc.stdout.read(64 * 1024)
                if not chunk:
                    break
                last_data = time.time()
                yield chunk
    finally:
        try:
            proc.terminate()
        except Exception:
            pass
        try:
            proc.wait(timeout=2.0)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dev", default="/dev/video0", help="V4L2 device (e.g. /dev/video0)")
    ap.add_argument("--width", type=int, default=960)
    ap.add_argument("--height", type=int, default=540)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--show", action="store_true", help="Show quadrants after capture")
    ap.add_argument("--show-mode", choices=["seq", "grid"], default="seq")
    ap.add_argument("--seq-delay-ms", type=int, default=600, help="Delay between quadrants in seq mode")
    ap.add_argument("--step", action="store_true", help="Advance to next quadrant on key press")
    ap.add_argument("--outdir", default="/tmp/uvc_dumps", help="Output directory for q0..q3 jpgs")
    ap.add_argument("--timeout-s", type=float, default=30.0)
    ap.add_argument("--win-x", type=int, default=50, help="OpenCV window X position")
    ap.add_argument("--win-y", type=int, default=50, help="OpenCV window Y position")
    args = ap.parse_args()

    if args.show and (args.show_mode == "seq") and (not args.step):
        args.step = True

    os.makedirs(args.outdir, exist_ok=True)

    frames: Dict[int, bytes] = {}
    paths = {}
    start = time.time()

    for jpeg in _iter_jpegs_from_stream(
        _ffmpeg_chunks(args.dev, args.width, args.height, args.fps,
                       idle_timeout_s=max(1.0, args.timeout_s))
    ):
        if time.time() - start > args.timeout_s:
            raise SystemExit(f"Timed out waiting for 4 quadrants (got {sorted(frames.keys())}).")

        meta = _parse_quadrant_from_com(jpeg)
        if meta is None:
            continue
        qid, name = meta
        if qid in frames:
            continue
        frames[qid] = jpeg
        print(f"Captured q{qid} ({name}) bytes={len(jpeg)}", flush=True)

        path = os.path.join(args.outdir, f"q{qid}.jpg")
        with open(path, "wb") as out:
            out.write(jpeg)
        paths[qid] = path
        print(f"Wrote {path}", flush=True)

        if len(frames) == 4:
            break

    if len(frames) < 4:
        raise SystemExit(f"Timed out waiting for 4 quadrants (got {sorted(frames.keys())}).")

    if not args.show:
        return

    if args.show_mode == "grid":
        q0 = cv2.imread(paths[0], cv2.IMREAD_COLOR)
        q1 = cv2.imread(paths[1], cv2.IMREAD_COLOR)
        q2 = cv2.imread(paths[2], cv2.IMREAD_COLOR)
        q3 = cv2.imread(paths[3], cv2.IMREAD_COLOR)
        if any(img is None for img in (q0, q1, q2, q3)):
            raise SystemExit("OpenCV failed to decode one of the quadrant JPEGs")
        grid = cv2.vconcat([cv2.hconcat([q0, q1]), cv2.hconcat([q2, q3])])
        win = "Quadrants (q0 q1 / q2 q3)"
        cv2.namedWindow(win, cv2.WINDOW_AUTOSIZE)
        cv2.moveWindow(win, int(args.win_x), int(args.win_y))
        cv2.imshow(win, grid)
        while True:
            if cv2.waitKey(50) & 0xFF in (ord("q"), 27):
                break
            if cv2.getWindowProperty(win, cv2.WND_PROP_VISIBLE) < 1:
                break
    else:
        win = "Quadrants"
        for qi in range(4):
            img = cv2.imread(paths[qi], cv2.IMREAD_COLOR)
            if img is None:
                raise SystemExit(f"OpenCV failed to decode {paths[qi]}")

            try:
                cv2.destroyWindow(win)
            except Exception:
                pass
            cv2.namedWindow(win, cv2.WINDOW_AUTOSIZE)
            cv2.moveWindow(win, int(args.win_x), int(args.win_y))
            cv2.imshow(win, img)
            for _ in range(5):
                cv2.waitKey(1)
            key = cv2.waitKey(0) & 0xFF
            if key in (ord("q"), 27):
                return
            if cv2.getWindowProperty(win, cv2.WND_PROP_VISIBLE) < 1:
                return


if __name__ == "__main__":
    main()
