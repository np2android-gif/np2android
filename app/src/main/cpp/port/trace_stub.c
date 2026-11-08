/**
 * @file	trace_stub.c
 * @brief	TRACE 無効時の trace_fmt 代替
 *
 * cbus/atapicmd.c は TRACE の有無に関係なく TRACEOUT を trace_fmt に
 * 再定義して呼ぶため、TRACE 無効ビルドでもシンボルが必要になる。
 */

#include "compiler.h"

#if !defined(TRACE)
void trace_fmt(const char *str, ...)
{
	(void)str;
}
#endif
