#!/usr/bin/env python3
"""Build tiny fake MP3s with Japanese ID3 tags for file_scan decode testing."""
import os
import struct
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else '/tmp/jptest'
os.makedirs(OUT, exist_ok=True)
os.makedirs(os.path.join(OUT, 'MUSIC'), exist_ok=True)


def syncsafe(n):
    return bytes([(n >> 21) & 0x7F, (n >> 14) & 0x7F, (n >> 7) & 0x7F, n & 0x7F])


def mp3_frame():
    # MPEG1 Layer3, 128kbps, 44100Hz, stereo, no padding: 0xFFFB9000
    return b'\xFF\xFB\x90\x00' + b'\x00' * 400


def id3v23_frame(fid, encoding, payload):
    frame = fid + struct.pack('>I', len(payload) + 1) + b'\x00\x00' + bytes([encoding]) + payload
    return frame


def write_mp3(path, frames):
    tag = b'ID3\x03\x00\x00' + syncsafe(sum(len(f) for f in frames))
    data = tag + b''.join(frames) + mp3_frame() * 20
    with open(path, 'wb') as f:
        f.write(data)


# 1. UTF-8 tagged (encoding 3)
write_mp3(os.path.join(OUT, 'MUSIC', 'lemon_utf8.mp3'), [
    id3v23_frame(b'TIT2', 3, 'Lemon'.encode('utf-8')),
    id3v23_frame(b'TPE1', 3, '米津玄師'.encode('utf-8')),
])

# 2. Shift-JIS mislabeled as ISO-8859-1 (encoding 0) - classic Japanese MP3
write_mp3(os.path.join(OUT, 'MUSIC', 'suzume_sjis.mp3'), [
    id3v23_frame(b'TIT2', 0, 'すずめ'.encode('shift_jis')),
    id3v23_frame(b'TPE1', 0, 'RADWIMPS'.encode('ascii')),
])

# 3. UTF-16 with BOM (encoding 1)
title = '夜に駆ける'.encode('utf-16-le')
write_mp3(os.path.join(OUT, 'MUSIC', 'yoru_utf16.mp3'), [
    id3v23_frame(b'TIT2', 1, b'\xff\xfe' + title),
    id3v23_frame(b'TPE1', 1, b'\xff\xfe' + 'YOASOBI'.encode('utf-16-le')),
])

# 4. No tag, Japanese filename (raw UTF-8 on disk)
no_tag = mp3_frame() * 20
with open(os.path.join(OUT, 'MUSIC', '前前前世 - RADWIMPS.mp3'), 'wb') as f:
    f.write(no_tag)

print('wrote', OUT)
