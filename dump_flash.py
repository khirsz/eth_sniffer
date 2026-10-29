#!/usr/bin/env python3
"""
Dump the whole DSL-2542B flash (BCM6338, 4 MB NOR) over the UART,
using `dumpmem` in the stock firmware shell.

dumpmem line format (bytes in memory order, so no endianness issues):
  bfc00000: 10 00 02 79 00 00 ...    ...y............

Requirements: pip install pyserial
Before running: close picocom/minicom and leave the router in the shell (prompt "# ").

Usage:
  python3 dump_flash.py /dev/ttyUSB0 flash.bin
  python3 dump_flash.py /dev/ttyUSB0 flash.bin --start-chunk 17   # resume
"""
import argparse
import os
import re
import sys
import time

import serial

BASE = 0xBFC00000
SIZE = 4 * 1024 * 1024
CHUNK = 64 * 1024          # unit of saving/resuming (~30 s at 115200)
SUB = 1024                 # dumpmem accepts length < 2048
RETRIES = 3
IDLE_TIMEOUT = 8.0         # s without new data = a problem

LINE_RE = re.compile(rb'^([0-9a-fA-F]{8}):((?: [0-9a-fA-F]{2}){16})')
PROMPT_RE = re.compile(rb'[\r\n]# ?')   # msh prompt "# " at the start of a line


class NoPrompt(Exception):
    def __init__(self, msg, buf):
        super().__init__(msg)
        self.buf = bytes(buf)


def drain(ser, quiet=0.5):
    """Discard everything still arriving on the port until `quiet` s of silence."""
    last = time.monotonic()
    while time.monotonic() - last < quiet:
        if ser.read(4096):
            last = time.monotonic()


def read_until(ser, done):
    """Read until done(buf) returns True; on silence > IDLE_TIMEOUT raise NoPrompt with what was received."""
    buf = bytearray()
    last = time.monotonic()
    while True:
        data = ser.read(4096)
        if data:
            buf += data
            last = time.monotonic()
            if done(buf):
                return bytes(buf)
        elif time.monotonic() - last > IDLE_TIMEOUT:
            raise NoPrompt(f"no prompt, received {len(buf)} B", buf)


def sync(ser):
    drain(ser)
    ser.write(b'\r')
    read_until(ser, lambda b: PROMPT_RE.search(b) is not None)
    drain(ser, 0.3)


def dump_block(ser, addr, length):
    """One dumpmem call. Done = all data lines + a prompt after the last of them."""
    last_line = b'%08x:' % (addr + length - 16)

    def done(buf):
        pos = buf.rfind(last_line)
        if pos < 0:
            return False
        nl = buf.find(b'\n', pos)
        return nl >= 0 and b'#' in buf[nl:]

    ser.write(f'dumpmem 0x{addr:08x} {length}\r'.encode())
    return read_until(ser, done)


def save_debug(raw, i, attempt):
    name = f'debug_chunk{i}_try{attempt}.txt'
    with open(name, 'wb') as f:
        f.write(raw)
    return name


def parse_chunk(raw, start, length):
    out = bytearray()
    expected = start
    for line in raw.split(b'\n'):
        m = LINE_RE.match(line.strip())
        if not m:
            continue
        addr = int(m.group(1), 16)
        if addr != expected:
            raise ValueError(f"expected 0x{expected:08x}, got 0x{addr:08x}")
        out += bytes(int(b, 16) for b in m.group(2).split())
        expected += 16
    if len(out) != length:
        raise ValueError(f"received {len(out)} B instead of {length}")
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('port')
    ap.add_argument('out')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--start-chunk', type=int, default=0)
    args = ap.parse_args()

    n_chunks = SIZE // CHUNK
    # preallocated file, each chunk is written straight to its place
    if not os.path.exists(args.out):
        with open(args.out, 'wb') as f:
            f.truncate(SIZE)

    with serial.Serial(args.port, args.baud, timeout=0.2) as ser, \
            open(args.out, 'r+b') as f:
        sync(ser)
        t0 = time.monotonic()
        for i in range(args.start_chunk, n_chunks):
            addr = BASE + i * CHUNK
            for attempt in range(1, RETRIES + 1):
                raw = b''
                try:
                    data = bytearray()
                    for sub in range(addr, addr + CHUNK, SUB):
                        raw = dump_block(ser, sub, SUB)
                        data += parse_chunk(raw, sub, SUB)
                    break
                except (ValueError, NoPrompt) as e:
                    if isinstance(e, NoPrompt):
                        raw = e.buf
                    dbg = save_debug(raw, i, attempt)
                    print(f'  chunk {i}: attempt {attempt} failed ({e}), raw data: {dbg}')
                    time.sleep(1)
                    try:
                        sync(ser)
                    except NoPrompt as e2:
                        print(f'  resync failed ({e2})')
            else:
                sys.exit(f'Aborted at chunk {i}. Resume with: --start-chunk {i}')

            f.seek(i * CHUNK)
            f.write(data)
            f.flush()
            done = i + 1 - args.start_chunk
            eta = (time.monotonic() - t0) / done * (n_chunks - i - 1)
            print(f'[{i + 1}/{n_chunks}] 0x{addr:08x} OK, ~{eta / 60:.1f} min left')

    with open(args.out, 'rb') as f:
        head = f.read(4)
    status = 'OK (MIPS branch at the CFE reset vector)' if head == b'\x10\x00\x02\x79' \
        else f'WARNING: unexpected start {head.hex()}'
    print(f'Done: {args.out}, start: {status}')


if __name__ == '__main__':
    main()
