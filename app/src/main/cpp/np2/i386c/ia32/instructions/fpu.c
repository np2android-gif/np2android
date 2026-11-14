/*
 * Copyright (c) 2012 NONAKA Kimihiro
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "compiler.h"
#include "ia32/cpu.h"
#include "ia32/ia32.mcr"

#include "ia32/instructions/fpu/fp.h"

#if defined(USE_FPU) && defined(SUPPORT_FPU_SOFTFLOAT3)
/*
 * ANDROID-FPU: NP2kai (wx_alpha 5939e0c) の i386c/ia32/instructions/fpu.c から、
 * SoftFloat 3e 版で使う部分だけを取り込んだもの。DOSBox 版・SoftFloat 2c 版・
 * FPU の種類の選択・SSE (0F AE の FXSAVE/FXRSTOR) は外した。
 */
#include "ia32/inst_table.h"
#include "ia32/instructions/fpu/fpumem.h"

/* 命令表の D8h〜DFh を SoftFloat 版に差し替える。initreg なら FPU も初期化する */
void CPUCALL
fpu_initialize(int initreg)
{

	insttable_1byte[0][0xd8] = insttable_1byte[1][0xd8] = SF_ESC0;
	insttable_1byte[0][0xd9] = insttable_1byte[1][0xd9] = SF_ESC1;
	insttable_1byte[0][0xda] = insttable_1byte[1][0xda] = SF_ESC2;
	insttable_1byte[0][0xdb] = insttable_1byte[1][0xdb] = SF_ESC3;
	insttable_1byte[0][0xdc] = insttable_1byte[1][0xdc] = SF_ESC4;
	insttable_1byte[0][0xdd] = insttable_1byte[1][0xdd] = SF_ESC5;
	insttable_1byte[0][0xde] = insttable_1byte[1][0xde] = SF_ESC6;
	insttable_1byte[0][0xdf] = insttable_1byte[1][0xdf] = SF_ESC7;
	if (initreg) {
		SF_FPU_FINIT();
	}
}

/* ステートのロードの後、SoftFloat の大域変数 (例外フラグ・丸め) を FPU の状態に合わせる */
void
fpu_statesave_load(void)
{
	const UINT16 statusword = FPU_STATUSWORD;
	UINT8 result = 0;

	if (statusword & (1 << 0)) result |= softfloat_flag_invalid;
	if (statusword & (1 << 2)) result |= softfloat_flag_infinite;
	if (statusword & (1 << 3)) result |= softfloat_flag_overflow;
	if (statusword & (1 << 4)) result |= softfloat_flag_underflow;
	if (statusword & (1 << 5)) result |= softfloat_flag_inexact;
	softfloat_exceptionFlags = result;
	switch (FPU_STAT.round) {
	case ROUND_Nearest:
		softfloat_roundingMode = softfloat_round_near_even;
		break;
	case ROUND_Down:
		softfloat_roundingMode = softfloat_round_min;
		break;
	case ROUND_Up:
		softfloat_roundingMode = softfloat_round_max;
		break;
	case ROUND_Chop:
		softfloat_roundingMode = softfloat_round_minMag;
		break;
	default:
		break;
	}
}

char *
fpu_reg2str(void)
{
	static char empty[] = "";

	return empty;
}

/*
 * FPU memory access function
 */
static INLINE void
fpu_set_dataptr(UINT16 seg, UINT32 address)
{
	FPU_DATAPTR_SEG = CPU_REGS_SREG(seg);
	FPU_DATAPTR_OFFSET = address;
}

UINT8 MEMCALL
fpu_memoryread_b(UINT32 address)
{
	UINT16 seg;

	seg = CPU_INST_SEGREG_INDEX;
	fpu_set_dataptr(seg, address);
	return cpu_vmemoryread_b(seg, address);
}

UINT16 MEMCALL
fpu_memoryread_w(UINT32 address)
{
	UINT16 seg;

	seg = CPU_INST_SEGREG_INDEX;
	fpu_set_dataptr(seg, address);
	return cpu_vmemoryread_w(seg, address);
}

UINT32 MEMCALL
fpu_memoryread_d(UINT32 address)
{
	UINT16 seg;

	seg = CPU_INST_SEGREG_INDEX;
	fpu_set_dataptr(seg, address);
	return cpu_vmemoryread_d(seg, address);
}

UINT64 MEMCALL
fpu_memoryread_q(UINT32 address)
{
	UINT16 seg;

	seg = CPU_INST_SEGREG_INDEX;
	fpu_set_dataptr(seg, address);
	return cpu_vmemoryread_q(seg, address);
}

REG80 MEMCALL
fpu_memoryread_f(UINT32 address)
{
	UINT16 seg;

	seg = CPU_INST_SEGREG_INDEX;
	fpu_set_dataptr(seg, address);
	return cpu_vmemoryread_f(seg, address);
}

float MEMCALL
fpu_memoryread_f32(UINT32 address)
{
	UINT16 seg;
	union {
		float f;
		UINT32 l;
	} val;

	seg = CPU_INST_SEGREG_INDEX;
	fpu_set_dataptr(seg, address);
	val.l = cpu_vmemoryread_d(seg, address);
	return val.f;
}

double MEMCALL
fpu_memoryread_f64(UINT32 address)
{
	UINT16 seg;
	union {
		double f;
		UINT64 q;
	} val;

	seg = CPU_INST_SEGREG_INDEX;
	fpu_set_dataptr(seg, address);
	val.q = cpu_vmemoryread_q(seg, address);
	return val.f;
}

void MEMCALL
fpu_memorywrite_b(UINT32 address, UINT8 value)
{
	UINT16 seg;

	seg = CPU_INST_SEGREG_INDEX;
	fpu_set_dataptr(seg, address);
	cpu_vmemorywrite_b(seg, address, value);
}

void MEMCALL
fpu_memorywrite_w(UINT32 address, UINT16 value)
{
	UINT16 seg;

	seg = CPU_INST_SEGREG_INDEX;
	fpu_set_dataptr(seg, address);
	cpu_vmemorywrite_w(seg, address, value);
}

void MEMCALL
fpu_memorywrite_d(UINT32 address, UINT32 value)
{
	UINT16 seg;

	seg = CPU_INST_SEGREG_INDEX;
	fpu_set_dataptr(seg, address);
	cpu_vmemorywrite_d(seg, address, value);
}

void MEMCALL
fpu_memorywrite_q(UINT32 address, UINT64 value)
{
	UINT16 seg;

	seg = CPU_INST_SEGREG_INDEX;
	fpu_set_dataptr(seg, address);
	cpu_vmemorywrite_q(seg, address, value);
}

void MEMCALL
fpu_memorywrite_f(UINT32 address, REG80 *value)
{
	UINT16 seg;

	seg = CPU_INST_SEGREG_INDEX;
	fpu_set_dataptr(seg, address);
	cpu_vmemorywrite_f(seg, address, value);
}

void MEMCALL
fpu_memorywrite_f32(UINT32 address, float value)
{
	UINT16 seg;
	union {
		float f;
		UINT32 l;
	} val;

	val.f = value;

	seg = CPU_INST_SEGREG_INDEX;
	fpu_set_dataptr(seg, address);
	cpu_vmemorywrite_d(seg, address, val.l);
}

void MEMCALL
fpu_memorywrite_f64(UINT32 address, double value)
{
	UINT16 seg;
	union {
		double f;
		UINT64 q;
	} val;

	val.f = value;

	seg = CPU_INST_SEGREG_INDEX;
	fpu_set_dataptr(seg, address);
	cpu_vmemorywrite_q(seg, address, val.q);
}

/*
 * ANDROID-FPU: NP2kai の FPU_FWAIT と同じ。WAIT/FWAIT が #NM になるのは、Intel の仕様どおり
 * CR0 の MP と TS が両方立っているときだけ (xnp2 の元の FWAIT はどちらかで #NM にしていた)。
 * マスクされていない FPU の例外が残っていれば #MF にする。
 */
void
FWAIT(void)
{

	if ((CPU_CR0 & (CPU_CR0_MP|CPU_CR0_TS)) == (CPU_CR0_MP|CPU_CR0_TS)) {
		EXCEPTION(NM_EXCEPTION, 0);
	}
	if ((FPU_STATUSWORD & ~FPU_CTRLWORD) & 0x3f) {
		EXCEPTION(MF_EXCEPTION, 0);
	}
}

#else	/* !(USE_FPU && SUPPORT_FPU_SOFTFLOAT3) */

void
FWAIT(void)
{
#if defined(USE_FPU)
	if (CPU_CR0 & (CPU_CR0_MP|CPU_CR0_TS)) {
		EXCEPTION(NM_EXCEPTION, 0);
	}

	fpu_fwait();
#endif
}

#endif
