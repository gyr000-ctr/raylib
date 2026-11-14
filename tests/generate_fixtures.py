#!/usr/bin/env python3
"""Generate test fixtures for tests/audio_music_loop_points.c

Creates (under tests/resources/):
  - plain.wav       : 12000 frames, mono s16, 8000 Hz, sample[i] = i, no loop metadata
  - loop_smpl.wav   : same PCM data + smpl chunk with a forward loop [2000, 5000)
  - loop_tags.ogg   : examples/audio/resources/target.ogg with LOOPSTART=1000 and
                      LOOPLENGTH=3000 Vorbis comments injected (audio pages untouched)

Run from the repository root:  python3 tests/generate_fixtures.py
"""

import os
import struct
import sys

SAMPLE_RATE = 8000
FRAME_COUNT = 12000          # < 32767 so the frame index fits in an s16 sample
LOOP_START = 2000
LOOP_END = 5000              # exclusive (smpl stores inclusive end = 4999)

OGG_LOOP_START = 1000
OGG_LOOP_LENGTH = 3000       # loop [1000, 4000)

OUT_DIR = os.path.join("tests", "resources")
TARGET_OGG = os.path.join("examples", "audio", "resources", "target.ogg")


def wav_pcm_frames():
    return b"".join(struct.pack("<h", i) for i in range(FRAME_COUNT))


def fmt_chunk():
    return (b"fmt " + struct.pack("<I", 16) +
            struct.pack("<HHIIHH", 1, 1, SAMPLE_RATE, SAMPLE_RATE * 2, 2, 16))


def smpl_chunk():
    # smpl header: 9 x u32, then one 24-byte loop record
    header = struct.pack("<9I", 0, 0, 0, 60, 0, 0, 0, 1, 0)
    # cuePointID=0, type=0 (forward), start, end (inclusive!), fraction=0, playCount=0 (infinite)
    loop = struct.pack("<6I", 0, 0, LOOP_START, LOOP_END - 1, 0, 0)
    return b"smpl" + struct.pack("<I", len(header) + len(loop)) + header + loop


def write_wav(path, with_smpl):
    data = wav_pcm_frames()
    chunks = fmt_chunk()
    if with_smpl:
        chunks += smpl_chunk()
    chunks += b"data" + struct.pack("<I", len(data)) + data
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks)
    print(f"  wrote {path} ({FRAME_COUNT} frames, smpl={with_smpl})")


# --- Ogg Vorbis comment injection -------------------------------------------

def ogg_crc(data):
    crc = 0
    for byte in data:
        crc ^= byte << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if (crc & 0x80000000) \
                else (crc << 1) & 0xFFFFFFFF
    return crc


def parse_pages(data):
    pages = []
    pos = 0
    while pos < len(data):
        assert data[pos:pos + 4] == b"OggS", f"bad capture at offset {pos}"
        nseg = data[pos + 26]
        segs = list(data[pos + 27:pos + 27 + nseg])
        payload = data[pos + 27 + nseg:pos + 27 + nseg + sum(segs)]
        pages.append({
            "type": data[pos + 5],
            "granule": struct.unpack("<q", data[pos + 6:pos + 14])[0],
            "serial": struct.unpack("<I", data[pos + 14:pos + 18])[0],
            "seq": struct.unpack("<I", data[pos + 18:pos + 22])[0],
            "segs": segs,
            "payload": payload,
        })
        pos += 27 + nseg + sum(segs)
    return pages


def split_packets(page):
    """Split a page payload into packets using its lacing table."""
    packets, cur, off = [], b"", 0
    for seg in page["segs"]:
        cur += page["payload"][off:off + seg]
        off += seg
        if seg < 255:
            packets.append(cur)
            cur = b""
    assert cur == b"" and off == len(page["payload"])
    return packets


def build_page(htype, granule, serial, seq, packets):
    segs, payload = [], b""
    for packet in packets:
        payload += packet
        n = len(packet)
        while n >= 255:
            segs.append(255)
            n -= 255
        segs.append(n)
    header = (b"OggS" + bytes([0, htype]) + struct.pack("<q", granule) +
              struct.pack("<III", serial, seq, 0) + bytes([len(segs)]) + bytes(segs))
    crc = ogg_crc(header + payload)
    return header[:22] + struct.pack("<I", crc) + header[26:] + payload


def patch_ogg(src_path, dst_path):
    pages = parse_pages(open(src_path, "rb").read())
    header_packets = split_packets(pages[1])
    assert len(header_packets) == 2, "expected comment+setup packets on page 1"
    comment, setup = header_packets
    assert comment[:7] == b"\x03vorbis" and setup[:7] == b"\x05vorbis"

    vendor_len = struct.unpack("<I", comment[7:11])[0]
    vendor = comment[11:11 + vendor_len]
    pos = 11 + vendor_len
    count = struct.unpack("<I", comment[pos:pos + 4])[0]
    pos += 4
    comments = []
    for _ in range(count):
        length = struct.unpack("<I", comment[pos:pos + 4])[0]
        pos += 4
        comments.append(comment[pos:pos + length])
        pos += length

    comments.append(f"LOOPSTART={OGG_LOOP_START}".encode())
    comments.append(f"LOOPLENGTH={OGG_LOOP_LENGTH}".encode())

    new_comment = (b"\x03vorbis" + struct.pack("<I", len(vendor)) + vendor +
                   struct.pack("<I", len(comments)) +
                   b"".join(struct.pack("<I", len(c)) + c for c in comments) + b"\x01")

    serial = pages[0]["serial"]
    out = build_page(pages[0]["type"], 0, serial, 0, split_packets(pages[0]))
    out += build_page(0, 0, serial, 1, [new_comment])
    out += build_page(0, 0, serial, 2, [setup])
    for i, page in enumerate(pages[2:]):
        out += build_page(page["type"], page["granule"], serial, i + 3,
                          split_packets(page))

    total_frames = pages[-1]["granule"]
    assert total_frames > OGG_LOOP_START + OGG_LOOP_LENGTH + 4096, \
        f"source ogg too short: {total_frames} frames"

    with open(dst_path, "wb") as f:
        f.write(out)
    print(f"  wrote {dst_path} ({total_frames} frames, "
          f"LOOPSTART={OGG_LOOP_START} LOOPLENGTH={OGG_LOOP_LENGTH})")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    write_wav(os.path.join(OUT_DIR, "plain.wav"), with_smpl=False)
    write_wav(os.path.join(OUT_DIR, "loop_smpl.wav"), with_smpl=True)
    patch_ogg(TARGET_OGG, os.path.join(OUT_DIR, "loop_tags.ogg"))


if __name__ == "__main__":
    sys.exit(main())
