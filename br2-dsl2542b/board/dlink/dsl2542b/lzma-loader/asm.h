/*
 * Minimalne makra asemblerowe dla head.S.
 *
 * Oryginalny loader z OpenWrt dołączał <asm/asm.h> z drzewa kernela.
 * Potrzebujemy z niego tylko LEAF/END, więc zamiast ciągnąć nagłówki kernela
 * (które zależą od jego konfiguracji) definiujemy je tutaj.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef _LOADER_ASM_H
#define _LOADER_ASM_H

/* Funkcja-liść: nie wywołuje innych funkcji, nie ma ramki stosu. */
#define LEAF(symbol)				\
	.globl	symbol;				\
	.align	2;				\
	.type	symbol, @function;		\
	.ent	symbol, 0;			\
symbol:	.frame	sp, 0, ra

#define END(function)				\
	.end	function;			\
	.size	function, .-function

#endif /* _LOADER_ASM_H */
