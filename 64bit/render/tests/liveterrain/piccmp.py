"""piccmp.py: the picture compare of a region of two pictures (ticket 4906, epic 0465 T4; COMMON 25's numbers).

Per pixel the difference is the largest absolute difference of the three colour channels (0-255). Over the pixels of
the region it reports the median, the 95th percentile and the maximum (COMMON 25), the mean, the count over
--thr (default 24, 4901's plotboxes threshold) and over 8, and writes a difference image: A | B | |A - B| x --gain of
the region's bounding box, with the region's outline in both pictures and the pixels outside the region dimmed.

    python -I render/tests/liveterrain/piccmp.py A.png B.png (--rect X0,Y0,X1,Y1 [--rect ...] | --mask MASK.png)
           [--thr 24] [--gain 4] [--diff DIFF.png] [--json OUT]

A rectangle is half-open: X0 <= x < X1, Y0 <= y < Y1, in pixels of the pictures. A mask is a picture of the same size
whose non-zero pixels (any channel) are the region.

Input checks, each refused with exit 2 and a line starting "piccmp: REFUSED": a picture missing or unreadable; the two
pictures of different sizes; no region, or both --rect and --mask; a rectangle that is malformed, empty or not inside
the picture; a mask of another size or with no pixel set; --thr outside 0..255; --gain below 1. Exit 0 otherwise.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np


class Refused(Exception):
    pass


def load_rgb(path: str) -> np.ndarray:
    from PIL import Image
    p = Path(path)
    if not p.is_file():
        raise Refused("picture not found: %s" % path)
    try:
        with Image.open(str(p)) as im:
            return np.asarray(im.convert("RGB")).astype(np.int16)
    except Exception as e:  # noqa: BLE001 - any unreadable picture is a refusal
        raise Refused("picture unreadable (%s): %s" % (e, path))


def parse_rect(s: str, w: int, h: int) -> tuple:
    try:
        x0, y0, x1, y1 = (int(v) for v in s.split(","))
    except ValueError:
        raise Refused("a rectangle is X0,Y0,X1,Y1 integers, not %r" % (s,))
    if x1 <= x0 or y1 <= y0:
        raise Refused("rectangle %s is empty" % s)
    if x0 < 0 or y0 < 0 or x1 > w or y1 > h:
        raise Refused("rectangle %s is not inside the picture (%d x %d)" % (s, w, h))
    return x0, y0, x1, y1


def region_mask(shape, rects=None, mask_path=None) -> np.ndarray:
    h, w = shape[:2]
    if rects and mask_path:
        raise Refused("give --rect or --mask, not both")
    if not rects and not mask_path:
        raise Refused("no region (--rect or --mask)")
    m = np.zeros((h, w), bool)
    if rects:
        for s in rects:
            x0, y0, x1, y1 = parse_rect(s, w, h)
            m[y0:y1, x0:x1] = True
    else:
        mk = load_rgb(mask_path)
        if mk.shape[:2] != (h, w):
            raise Refused("mask %d x %d, pictures %d x %d" % (mk.shape[1], mk.shape[0], w, h))
        m = mk.max(axis=2) > 0
        if not m.any():
            raise Refused("the mask has no pixel set")
    return m


def compare(A: np.ndarray, B: np.ndarray, m: np.ndarray, thr: int = 24) -> tuple[dict, np.ndarray]:
    if A.shape != B.shape:
        raise Refused("picture sizes differ: %s and %s" % (A.shape[:2][::-1], B.shape[:2][::-1]))
    if not 0 <= thr <= 255:
        raise Refused("--thr outside 0..255")
    d = np.abs(A - B).max(axis=2)
    v = d[m]
    res = {"pixels": int(v.size), "median": float(np.median(v)), "p95": float(np.percentile(v, 95)),
           "max": int(v.max()), "mean": round(float(v.mean()), 4), "thr": thr,
           "over_thr": int((v > thr).sum()), "over_8": int((v > 8).sum()), "equal": int((v == 0).sum())}
    ys, xs = np.nonzero(m)
    res["bbox"] = [int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1]
    return res, d


def diff_image(A, B, d, m, bbox, gain: int) -> np.ndarray:
    x0, y0, x1, y1 = bbox
    ca, cb = A[y0:y1, x0:x1].copy(), B[y0:y1, x0:x1].copy()
    cm = m[y0:y1, x0:x1]
    cd = np.clip(np.abs(A - B)[y0:y1, x0:x1] * gain, 0, 255)
    for c in (ca, cb, cd):
        c[~cm] = c[~cm] // 3
    # outline of the region: region pixels with a neighbour outside it
    edge = cm & ~(np.roll(cm, 1, 0) & np.roll(cm, -1, 0) & np.roll(cm, 1, 1) & np.roll(cm, -1, 1))
    edge[0, :] |= cm[0, :]
    edge[-1, :] |= cm[-1, :]
    edge[:, 0] |= cm[:, 0]
    edge[:, -1] |= cm[:, -1]
    for c in (ca, cb):
        c[edge] = (255, 0, 255)
    sep = np.full((ca.shape[0], 4, 3), 255, np.int16)
    return np.concatenate([ca, sep, cb, sep, cd], axis=1).astype(np.uint8)


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--rect", action="append")
    p.add_argument("--mask")
    p.add_argument("--thr", type=int, default=24)
    p.add_argument("--gain", type=int, default=4)
    p.add_argument("--diff")
    p.add_argument("--json")
    a = p.parse_args(argv)
    try:
        if a.gain < 1:
            raise Refused("--gain below 1")
        A, B = load_rgb(a.a), load_rgb(a.b)
        if A.shape != B.shape:
            raise Refused("picture sizes differ: %s and %s" % (A.shape[:2][::-1], B.shape[:2][::-1]))
        m = region_mask(A.shape, a.rect, a.mask)
        res, d = compare(A, B, m, a.thr)
    except Refused as e:
        print("piccmp: REFUSED %s" % e)
        return 2
    res.update({"a": a.a, "b": a.b, "region": {"rects": a.rect} if a.rect else {"mask": a.mask}})
    if a.diff:
        from PIL import Image
        Path(a.diff).parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(diff_image(A, B, d, m, res["bbox"], a.gain)).save(a.diff)
        res["diff_image"] = a.diff
    txt = json.dumps(res, indent=1)
    if a.json:
        Path(a.json).parent.mkdir(parents=True, exist_ok=True)
        Path(a.json).write_text(txt, encoding="utf-8")
    print(txt)
    return 0


if __name__ == "__main__":
    sys.exit(main())
