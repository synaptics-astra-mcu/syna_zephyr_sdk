import numpy as np
import matplotlib.pyplot as plt

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


def _prompt_choice() -> str:
    print("Select input resolution:")
    print("  1) FHD   (1920x1080)")
    print("  2) WQVGA (480x270)")
    return input("Enter 1 or 2: ").strip()


def _prompt_path(default: str = "frame_dump.raw") -> str:
    entered = input(f"Raw file path [{default}]: ").strip()
    return entered or default


if __name__ == "__main__":
    choice = _prompt_choice()
    path = _prompt_path()

    if choice == "1":
        fhd(path)
    elif choice == "2":
        wqvga(path)
    else:
        raise SystemExit("Invalid choice. Please enter 1 (FHD) or 2 (WQVGA).")