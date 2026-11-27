// tail_read.h - ticket 0758 (route A of research/05-graphics-composition/frame-cost-exe-side.md section 7), written
// 2026-11-25 for this repository (GPL-3.0-or-later). Own code.
//
// The copy at Unmap reads the "tail" of a cached map (unit_filter.cpp OnUnmap: the pages of [0, need) the EXE did not write
// in this map) from the driver's pointer of a DYNAMIC vertex or instance buffer. That memory is write-combined (uncached):
// ordinary loads, memcpy's among them, read it at 130-630 MB/s, one bus read per load. A streaming load (SSE4.1 MOVNTDQA,
// AVX2 VMOVNTDQA) fills a 64-byte streaming-load buffer per line and serves the rest of the line from it: 2.4-11.6 GB/s on
// the same memory in 0757's bench, with the same bytes. A load is a load: what lands in the destination is what memcpy
// would have copied. On cached memory streaming loads are ordinary loads (and slower than memcpy), so only the tail uses this.
//
// TailReadLevel() reads the CPU once (CPUID, XGETBV); TailRead(level, dst, src, n) copies n bytes:
//   TAILREAD_COPY   memcpy (a CPU without SSE4.1, or the test key's 0)
//   TAILREAD_SSE41  MFENCE, the bytes up to src's 16-byte alignment by memcpy, 16-byte streaming loads (four lines in flight,
//                   the four loads of a line one after the other in the stream), unaligned 16-byte stores, the rest by memcpy
//   TAILREAD_AVX2   the same with 32-byte loads (two per line) from src's 32-byte alignment, then VZEROUPPER
// The destination may have any alignment (the shadow is realloc'd: 8 bytes on x86). The fence orders the loads after the
// forward's write-combined stores to other pages of the same buffer (the design's choice: under 1 us; 0757 7.5 lists it as
// not proven necessary). The source must stay readable for [src, src + n) only: no load reaches outside it.
// Used by unit_filter.cpp and by shell/tests/unit_filter_host.cpp --tailread (shell/tests/test_unit_filter_tailread.py).
#pragma once
#include <immintrin.h>
#include <intrin.h>
#include <stdint.h>
#include <string.h>

enum { TAILREAD_COPY = 0, TAILREAD_SSE41 = 1, TAILREAD_AVX2 = 2 };
static const char* const kTailReadNames[] = { "memcpy", "sse4.1", "avx2" };

// the best level this CPU and OS give (AVX2 needs the OS to save the YMM state: OSXSAVE and XCR0 bits 1-2)
static inline int TailReadLevel()
{
	int r0[4] = {}, r1[4] = {}, r7[4] = {};
	__cpuid(r0, 0);
	if (r0[0] < 1) return TAILREAD_COPY;
	__cpuid(r1, 1);
	const bool sse41 = ((r1[2] >> 19) & 1) != 0;
	if (!sse41) return TAILREAD_COPY;
	const bool osxsave = ((r1[2] >> 27) & 1) != 0, avx = ((r1[2] >> 28) & 1) != 0;
	if (r0[0] >= 7) __cpuidex(r7, 7, 0);
	const bool avx2 = osxsave && avx && ((r7[1] >> 5) & 1) && ((_xgetbv(0) & 6) == 6);
	return avx2 ? TAILREAD_AVX2 : TAILREAD_SSE41;
}

static inline void TailReadSse41(uint8_t* d, const uint8_t* s, size_t n)
{
	_mm_mfence();
	size_t h = (size_t)((16u - ((uintptr_t)s & 15u)) & 15u);
	if (h > n) h = n;
	if (h) { memcpy(d, s, h); d += h; s += h; n -= h; }
	__m128i* p = (__m128i*)s;   // (the intrinsic takes a non-const pointer in older headers)
	size_t i = 0;
	const size_t v = n / 16;
	for (; i + 16 <= v; i += 16)
	{
		const __m128i r0 = _mm_stream_load_si128(p + i + 0), r1 = _mm_stream_load_si128(p + i + 4), r2 = _mm_stream_load_si128(p + i + 8), r3 = _mm_stream_load_si128(p + i + 12);
		const __m128i r4 = _mm_stream_load_si128(p + i + 1), r5 = _mm_stream_load_si128(p + i + 5), r6 = _mm_stream_load_si128(p + i + 9), r7 = _mm_stream_load_si128(p + i + 13);
		const __m128i r8 = _mm_stream_load_si128(p + i + 2), r9 = _mm_stream_load_si128(p + i + 6), ra = _mm_stream_load_si128(p + i + 10), rb = _mm_stream_load_si128(p + i + 14);
		const __m128i rc = _mm_stream_load_si128(p + i + 3), rd = _mm_stream_load_si128(p + i + 7), re = _mm_stream_load_si128(p + i + 11), rf = _mm_stream_load_si128(p + i + 15);
		__m128i* q = (__m128i*)(d + i * 16);
		_mm_storeu_si128(q + 0, r0); _mm_storeu_si128(q + 4, r1); _mm_storeu_si128(q + 8, r2); _mm_storeu_si128(q + 12, r3);
		_mm_storeu_si128(q + 1, r4); _mm_storeu_si128(q + 5, r5); _mm_storeu_si128(q + 9, r6); _mm_storeu_si128(q + 13, r7);
		_mm_storeu_si128(q + 2, r8); _mm_storeu_si128(q + 6, r9); _mm_storeu_si128(q + 10, ra); _mm_storeu_si128(q + 14, rb);
		_mm_storeu_si128(q + 3, rc); _mm_storeu_si128(q + 7, rd); _mm_storeu_si128(q + 11, re); _mm_storeu_si128(q + 15, rf);
	}
	for (; i < v; ++i) _mm_storeu_si128((__m128i*)(d + i * 16), _mm_stream_load_si128(p + i));
	const size_t done = v * 16;
	if (n > done) memcpy(d + done, s + done, n - done);
}

static inline void TailReadAvx2(uint8_t* d, const uint8_t* s, size_t n)
{
	_mm_mfence();
	size_t h = (size_t)((32u - ((uintptr_t)s & 31u)) & 31u);
	if (h > n) h = n;
	if (h) { memcpy(d, s, h); d += h; s += h; n -= h; }
	__m256i* p = (__m256i*)s;
	size_t i = 0;
	const size_t v = n / 32;
	for (; i + 8 <= v; i += 8)
	{
		const __m256i r0 = _mm256_stream_load_si256(p + i + 0), r1 = _mm256_stream_load_si256(p + i + 2), r2 = _mm256_stream_load_si256(p + i + 4), r3 = _mm256_stream_load_si256(p + i + 6);
		const __m256i r4 = _mm256_stream_load_si256(p + i + 1), r5 = _mm256_stream_load_si256(p + i + 3), r6 = _mm256_stream_load_si256(p + i + 5), r7 = _mm256_stream_load_si256(p + i + 7);
		__m256i* q = (__m256i*)(d + i * 32);
		_mm256_storeu_si256(q + 0, r0); _mm256_storeu_si256(q + 2, r1); _mm256_storeu_si256(q + 4, r2); _mm256_storeu_si256(q + 6, r3);
		_mm256_storeu_si256(q + 1, r4); _mm256_storeu_si256(q + 3, r5); _mm256_storeu_si256(q + 5, r6); _mm256_storeu_si256(q + 7, r7);
	}
	for (; i < v; ++i) _mm256_storeu_si256((__m256i*)(d + i * 32), _mm256_stream_load_si256(p + i));
	_mm256_zeroupper();
	const size_t done = v * 32;
	if (n > done) memcpy(d + done, s + done, n - done);
}

static inline void TailRead(int level, void* dst, const void* src, size_t n)
{
	if (!n) return;
	if (level == TAILREAD_AVX2) TailReadAvx2((uint8_t*)dst, (const uint8_t*)src, n);
	else if (level == TAILREAD_SSE41) TailReadSse41((uint8_t*)dst, (const uint8_t*)src, n);
	else memcpy(dst, src, n);
}
