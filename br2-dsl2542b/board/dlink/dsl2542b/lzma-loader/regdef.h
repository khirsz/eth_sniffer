/*
 * Symbolic MIPS register names (o32 convention), so head.S can say "t0"
 * instead of "$8". Replaces <asm/regdef.h> from the kernel.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef _LOADER_REGDEF_H
#define _LOADER_REGDEF_H

#define zero	$0	/* always 0 */
#define AT	$1	/* reserved for the assembler */
#define v0	$2	/* return values */
#define v1	$3
#define a0	$4	/* function arguments */
#define a1	$5
#define a2	$6
#define a3	$7
#define t0	$8	/* temporaries, not preserved across calls */
#define t1	$9
#define t2	$10
#define t3	$11
#define t4	$12
#define t5	$13
#define t6	$14
#define t7	$15
#define s0	$16	/* preserved across calls */
#define s1	$17
#define s2	$18
#define s3	$19
#define s4	$20
#define s5	$21
#define s6	$22
#define s7	$23
#define t8	$24
#define t9	$25
#define k0	$26	/* reserved for the kernel / exception handlers */
#define k1	$27
#define gp	$28	/* global pointer */
#define sp	$29	/* stack pointer */
#define fp	$30	/* frame pointer (alias s8) */
#define s8	$30
#define ra	$31	/* return address */

#endif /* _LOADER_REGDEF_H */
