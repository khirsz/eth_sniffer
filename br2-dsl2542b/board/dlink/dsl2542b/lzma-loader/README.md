# LZMA loader dla CFE

Kod pochodzi z OpenWrt 18.06 (`target/linux/brcm63xx/image/lzma-loader/src`,
commit `9f2a40c72fdb`), autorzy: Gabor Juhos, Jonas Gorski i inni, licencja
GPL-2.0. Zmiany w tej kopii:

- `head.S` dołącza lokalne `asm.h` i `regdef.h` zamiast nagłówków kernela,
- nowy `Makefile` (budowanie do katalogu `O=`, bez `-fhonour-copts`),
- pozostałe pliki bez zmian.

Po co jest ten loader, opisuje główny `README.md` (sekcja o mapie pamięci).
