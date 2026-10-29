/*
 * Minimal assembler macros for head.S.
 *
 * The original OpenWrt loader included <asm/asm.h> from the kernel tree.
 * We only need LEAF/END from it, so instead of pulling in kernel headers
 * (which depend on the kernel configuration) we define them here.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef _LOADER_ASM_H
#define _LOADER_ASM_H

/* Leaf function: calls no other functions, has no stack frame. */
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
