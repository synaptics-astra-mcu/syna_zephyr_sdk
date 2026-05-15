import numpy as np
import matplotlib.pyplot as plt
import sys
import shutil
import subprocess

def fhd(path: str = "frame_dump.raw") -> None:
    """
    Convert a 1920x1080 raw8 frame dump to PNGs.
    Writes:
      - captured_frame_raw.png (no scaling)
      - captured_frame.png     (contrast-stretched)
    """
    w, h = 1920, 1080
    data = np.fromfile(path, dtype=np.uint8)
    expected = w * h
    print("bytes:", data.size, "expected:", expected)

    data = data[:expected]
    img = data.reshape((h, w))

    print("min/max/mean:", int(img.min()), int(img.max()), float(img.mean()))
    print("p1/p50/p99:", np.percentile(img, [1, 50, 99]))

    plt.imsave("captured_frame_raw.png", img, cmap="gray")

    p1, p99 = np.percentile(img, [1, 99])
    if p99 > p1:
        vis = (
            ((img.astype(np.float32) - p1) * (255.0 / (p99 - p1)))
            .clip(0, 255)
            .astype(np.uint8)
        )
    else:
        vis = img
    plt.imsave("captured_frame.png", vis, cmap="gray")

    print("Wrote captured_frame_raw.png and captured_frame.png")


def wqvga(path: str = "frame_dump.raw") -> None:
    """
    Convert a 480x270 raw8 frame dump to a PNG.
    Writes:
      - captured_frame.png
    """
    w, h = 480, 270
    data = np.fromfile(path, dtype=np.uint8)
    expected = w * h
    print("bytes:", data.size, "expected:", expected)

    data = data[:expected]
    img = data.reshape((h, w))

    plt.imsave("captured_frame.png", img, cmap="gray")
    print("Successfully saved image to 'captured_frame.png'")

    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        return

    # Many LP Sense sensor-to-memory dumps use BA81 (SBGGR8/BGGR8 Bayer).
    out_rgb = "captured_frame_rgb.png"
    cmd = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-f",
        "rawvideo",
        "-pixel_format",
        "bayer_bggr8",
        "-video_size",
        f"{w}x{h}",
        "-i",
        path,
        "-vf",
        "format=rgb24",
        "-frames:v",
        "1",
        "-update",
        "1",
        out_rgb,
    ]
    try:
        subprocess.run(cmd, check=True)
        print(f"Successfully saved color image to '{out_rgb}'")
    except Exception:
        pass


def lp960(path: str = "frame_dump.raw") -> None:
    """
    Convert a 960x540 raw8 frame dump to a PNG.
    Writes:
      - captured_frame.png
    """
    w, h = 960, 540
    data = np.fromfile(path, dtype=np.uint8)
    expected = w * h
    print("bytes:", data.size, "expected:", expected)

    if data.size == 480 * 270:
        print("Detected 480x270 dump; use option 2 (WQVGA) for sensor-to-memory.")
        wqvga(path)
        return

    data = data[:expected]
    img = data.reshape((h, w))

    plt.imsave("captured_frame.png", img, cmap="gray")
    print("Successfully saved image to 'captured_frame.png'")

    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        print("ffmpeg not found; skipping color (demosaic) output")
        return

    # LP Sense mode=1 dump is a Bayer RAW8 frame (typically SBGGR8).
    # Use ffmpeg to demosaic into an RGB PNG for easier visual inspection.
    out_rgb = "captured_frame_rgb.png"
    cmd = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-f",
        "rawvideo",
        "-pixel_format",
        "bayer_bggr8",
        "-video_size",
        f"{w}x{h}",
        "-i",
        path,
        "-vf",
        "format=rgb24",
        "-frames:v",
        "1",
        "-update",
        "1",
        out_rgb,
    ]
    try:
        subprocess.run(cmd, check=True)
        print(f"Successfully saved color image to '{out_rgb}'")
    except Exception as e:
        print(f"ffmpeg demosaic failed; skipping color output ({e})")


def _prompt_choice() -> str:
    print("Select input resolution:")
    print("  1) FHD   (1920x1080)")
    print("  2) WQVGA (480x270)")
    print("  3) ENC")
    return input("Enter 1, 2, or 3: ").strip()


def _prompt_path(default: str = "frame_dump.raw") -> str:
    entered = input(f"Raw file path [{default}]: ").strip()
    return entered or default


if __name__ == "__main__":
    choice = _prompt_choice()
    default_path = sys.argv[1] if len(sys.argv) > 1 else "frame_dump.raw"
    path = _prompt_path(default_path)

    if choice == "1":
        fhd(path)
    elif choice == "2":
        wqvga(path)
    elif choice == "3":
        lp960(path)
    else:
        raise SystemExit("Invalid choice. Please enter 1 (FHD), 2 (WQVGA), or 3 (ENC).")
