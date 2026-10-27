#!/usr/bin/env python3
"""
Zrzut całego flasha DSL-2542B (BCM6338, 4 MB NOR) przez UART,
korzystając z `dumpmem` w shellu stockowego firmware'u.

Format linii dumpmem (bajty w kolejności pamięci, więc bez problemu z endianness):
  bfc00000: 10 00 02 79 00 00 ...    ...y............

Wymagania: pip install pyserial
Przed uruchomieniem: zamknij picocom/minicom i zostaw router w shellu (prompt "# ").

Użycie:
  python3 dump_flash.py /dev/ttyUSB0 flash.bin
  python3 dump_flash.py /dev/ttyUSB0 flash.bin --start-chunk 17   # wznowienie
"""
import argparse
import os
import re
import sys
import time

import serial

BASE = 0xBFC00000
SIZE = 4 * 1024 * 1024
CHUNK = 64 * 1024          # jednostka zapisu/wznawiania (~30 s przy 115200)
SUB = 1024                 # dumpmem przyjmuje length < 2048
RETRIES = 3
IDLE_TIMEOUT = 8.0         # s bez nowych danych = problem

LINE_RE = re.compile(rb'^([0-9a-fA-F]{8}):((?: [0-9a-fA-F]{2}){16})')
PROMPT_RE = re.compile(rb'[\r\n]# ?')   # prompt msh "# " na początku linii


class NoPrompt(Exception):
    def __init__(self, msg, buf):
        super().__init__(msg)
        self.buf = bytes(buf)


def drain(ser, quiet=0.5):
    """Wyrzuca wszystko, co jeszcze leci z portu, aż do `quiet` s ciszy."""
    last = time.monotonic()
    while time.monotonic() - last < quiet:
        if ser.read(4096):
            last = time.monotonic()


def read_until(ser, done):
    """Czyta, aż done(buf) zwróci True; przy ciszy > IDLE_TIMEOUT rzuca NoPrompt z odbiorem."""
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
            raise NoPrompt(f"brak promptu, odebrano {len(buf)} B", buf)


def sync(ser):
    drain(ser)
    ser.write(b'\r')
    read_until(ser, lambda b: PROMPT_RE.search(b) is not None)
    drain(ser, 0.3)


def dump_block(ser, addr, length):
    """Jedno wywołanie dumpmem. Koniec = wszystkie linie danych + prompt po ostatniej z nich."""
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
            raise ValueError(f"oczekiwano 0x{expected:08x}, jest 0x{addr:08x}")
        out += bytes(int(b, 16) for b in m.group(2).split())
        expected += 16
    if len(out) != length:
        raise ValueError(f"odebrano {len(out)} B zamiast {length}")
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('port')
    ap.add_argument('out')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--start-chunk', type=int, default=0)
    args = ap.parse_args()

    n_chunks = SIZE // CHUNK
    # plik prealokowany, każdy kawałek zapisywany od razu na swoje miejsce
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
                    print(f'  kawałek {i}: próba {attempt} nieudana ({e}), surowe dane: {dbg}')
                    time.sleep(1)
                    try:
                        sync(ser)
                    except NoPrompt as e2:
                        print(f'  synchronizacja nieudana ({e2})')
            else:
                sys.exit(f'Przerwano na kawałku {i}. Wznów: --start-chunk {i}')

            f.seek(i * CHUNK)
            f.write(data)
            f.flush()
            done = i + 1 - args.start_chunk
            eta = (time.monotonic() - t0) / done * (n_chunks - i - 1)
            print(f'[{i + 1}/{n_chunks}] 0x{addr:08x} OK, zostało ~{eta / 60:.1f} min')

    with open(args.out, 'rb') as f:
        head = f.read(4)
    status = 'OK (skok MIPS na wektorze resetu CFE)' if head == b'\x10\x00\x02\x79' \
        else f'UWAGA: nieoczekiwany początek {head.hex()}'
    print(f'Gotowe: {args.out}, początek: {status}')


if __name__ == '__main__':
    main()
