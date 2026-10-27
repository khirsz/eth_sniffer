/*
 * Symboliczne nazwy rejestrów MIPS (konwencja o32), żeby w head.S można było
 * pisać "t0" zamiast "$8". Zastępuje <asm/regdef.h> z kernela.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef _LOADER_REGDEF_H
#define _LOADER_REGDEF_H

#define zero	$0	/* zawsze 0 */
#define AT	$1	/* zarezerwowany dla asemblera */
#define v0	$2	/* wartości zwracane */
#define v1	$3
#define a0	$4	/* argumenty funkcji */
#define a1	$5
#define a2	$6
#define a3	$7
#define t0	$8	/* tymczasowe, niezachowywane przez wywołania */
#define t1	$9
#define t2	$10
#define t3	$11
#define t4	$12
#define t5	$13
#define t6	$14
#define t7	$15
#define s0	$16	/* zachowywane przez wywołania */
#define s1	$17
#define s2	$18
#define s3	$19
#define s4	$20
#define s5	$21
#define s6	$22
#define s7	$23
#define t8	$24
#define t9	$25
#define k0	$26	/* zarezerwowane dla jądra / obsługi wyjątków */
#define k1	$27
#define gp	$28	/* global pointer */
#define sp	$29	/* stack pointer */
#define fp	$30	/* frame pointer (alias s8) */
#define s8	$30
#define ra	$31	/* adres powrotu */

#endif /* _LOADER_REGDEF_H */
