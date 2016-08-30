/*
 * enumerate.c	- use intel x[86/64] cpuid instruction to enumerate the list of
 * 		  cpu caches.
 *
 * Author: Sougata Santra (sougata.santra@gmail.com)
 *
 * Reference:
 *  64-ia-32-architectures-software-developer-instruction-set-\
 *  reference-manual-325383.pdf
 * 	
 *
 * Theory:
 * 	The ID flag (bit 21) in the EFLAGS register indicates support for the
 * 	CPUID instruction. If a software procedure can set and clear this flag,
 * 	the processor executing the procedure supports the CPUID instruction.
 * 	This instruction operates the same in non-64-bit modes and 64-bit mode.
 *
 * 	CPUID returns processor identification and feature information in the
 * 	EAX, EBX, ECX, and EDX registers.1 The instruction’s output is
 * 	dependent on the contents of the EAX register upon execution
 * 	(in some cases, ECX as well).
 *
 * 	Deterministic Cache Parameters Leaf
 * 	MOV EAX, 04H
 * 	CPUID
 *
 * 	NOTES:
 * 		Leaf 04H output depends on the initial value in ECX.*
 * 		See also: “INPUT EAX = 04H: Returns Deterministic Cache
 * 			Parameters for Each Level” on page 213.
 * 	EAX
 * 		Bits 04 - 00: Cache Type Field.
 * 		  0 = Null - No more caches.
 * 		  1 = Data Cache.
 * 		  2 = Instruction Cache.
 * 		  3 = Unified Cache.
 * 		  4-31 = Reserved.
 * 		Bits 07 - 05: Cache Level (starts at 1).
 * 		Bit  08: Self Initializing cache level (does not need SW
 * 			initialization).
 * 		Bit  09: Fully Associative cache.
 * 		Bits 13 - 10: Reserved.
 * 		Bits 25 - 14: Maximum number of addressable IDs for logical
 * 			processors sharing this cache**, ***.
 * 		Bits 31 - 26: Maximum number of addressable IDs for processor
 * 			cores in the physical package**, ****, *****.
 * 	EBX
 * 		Bits 11 - 00: L = System Coherency Line Size**.
 * 		Bits 21 - 12: P = Physical Line partitions**.
 * 		Bits 31 - 22: W = Ways of associativity**.
 * 	ECX
 * 		Bits 31-00: S = Number of Sets**.
 * 	EDX
 * 		Bit 00: Write-Back Invalidate/Invalidate.
 * 		  0 = WBINVD/INVD from threads sharing this cache acts upon lower
 * 			level caches for threads sharing this cache.
 * 		  1 = WBINVD/INVD is not guaranteed to act upon lower level
 * 			caches of non-originating threads sharing this cache.
 * 		Bit 01: Cache Inclusiveness.
 * 		  0 = Cache is not inclusive of lower cache levels.
 * 		  1 = Cache is inclusive of lower cache levels.
 * 		Bit 02: Complex Cache Indexing.
 * 		  0 = Direct mapped cache.
 * 		  1 = A complex function is used to index the cache, potentially
 * 			using all address bits.
 * 		Bits 31 - 03: Reserved = 0.
 *
 * 	NOTES:
 * 	* If ECX contains an invalid sub leaf index, EAX/EBX/ECX/EDX return 0.
 * 		Sub-leaf index n+1 is invalid if sub- leaf n returns EAX[4:0]
 * 		as 0.
 * 	** Add one to the return value to get the result.
 * 	***The nearest power-of-2 integer that is not smaller than
 * 		(1 + EAX[25:14]) is the number of unique initial APIC IDs
 * 		reserved for addressing different logical processors sharing
 * 		this cache.
 * 	**** The nearest power-of-2 integer that is not smaller than
 * 		(1 + EAX[31:26]) is the number of unique Core_IDs reserved for
 * 		addressing different processor cores in a physical package.
 * 		Core ID is a subset of bits of the initial APIC ID.
 * 	***** The returned value is constant for valid initial values in ECX.
 * 		Valid ECX values start from 0.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

static const char *header[] =
{
"[L]*  	  - Self Initialized",
"[Ways]** - Fully associative",
"*============================================================================*",
"| L |  Type  | No.Sets | LineSz | Line Part | Ways | Size | Invd | Incv.| Indx",
"*============================================================================*",
NULL
};

static inline void bytes_to_prefix(size_t *bytes, char **s)
{
	size_t b = *bytes;
	if (*s) {
		if (!(b & (1024 - 1))) {
			b >>= 10;
			*s = "k";
		}
		if (!(b & (1024 - 1))) {
			b >>= 10;
			*s = "M";
		}
		if (!(b & (1024 - 1))) {
			b >>= 10;
			*s = "G";
		}
	}
	*bytes = b;
}

static const char *cache_type[] =
{
	"Data", "Instn", "Unified", "Unkown", NULL
};

static void enumerate_cache(void)
{
	size_t total_size;
	uint32_t type, eax, ebx, ecx, edx;
	unsigned level, sets, line_size, line_partitions, ways;
	bool fully_associative, self_init, wbinvd, inclusive, indexing;
	char *prefix;
	int index = 0;

	while(header[index])
		fprintf (stdout, "%s\n", header[index++]);

	index = 0;
	do {

		eax = 0x04;
		ecx = index;

		asm("cpuid": "+a" (eax), "=b" (ebx), "+c" (ecx), "=d" (edx));
		/*
		 * Process contents of register EAX.
		 *
		 *  - Bits 04 - 00: Cache Type Field.
		 * 	0 = Null - No more caches.
		 * 	1 = Data Cache.
		 * 	2 = Instruction Cache.
		 * 	3 = Unified Cache.
		 * 	4-31 = Reserved.
		 *  - Bits 07 - 05: Cache Level (starts at 1).
		 *  - Bit  08: does not need SW initialization.
		 *  - 09: Fully Associative cache.
		 */
		if (!(type = eax & 0x00000001F))
			break;
		level = (eax >>= 5) & 0x00000007;
		self_init = ((eax >>= 3) & 0x00000001);
		fully_associative = ((eax >>= 1) & 0x00000001);
		/*
		 * Process contents of register EBX.
		 * - Bits 11 - 00: L = System Coherency Line Size**.
		 * - Bits 21 - 12: P = Physical Line partitions**.
		 * - Bits 31 - 22: W = Ways of associativity**.
		 */
		line_size = (ebx & 0x00000FFF) + 1;
		line_partitions = ((ebx >>= 12) & 0x000003FF) + 1;
		ways = ((ebx >>= 10) & 0x000003FF) + 1;
		/*
		 * Process contents of register ECX.
		 * - Bits 31-00: S = Number of Sets**.
		 */
		sets = ecx + 1;
		/*
		 * Process contents of register EDX.
		 * - Bit 00: Write-Back Invalidate/Invalidate.
		 * - Bit 01: Cache Inclusiveness.
		 * - Bit 02: Complex Cache Indexing.
		 */
		wbinvd = ((edx & 0x00000001));
		inclusive =((edx & 0x00000002));
		indexing =((edx & 0x00000004));

		total_size = ways * line_partitions * line_size * sets;
		prefix = " ";
		bytes_to_prefix(&total_size, &prefix);
		fprintf(stdout, "%2s%u%s %8s %6u %8u %8u %8u%s %8zu%s "
				"%4s %6s %6s\n",
				"L", level, self_init ? "*": "",
				cache_type[type - 1], sets, line_size,
				line_partitions, ways,
				fully_associative ? "*": "",
				total_size, prefix,
				wbinvd ? "N": "Y",
				inclusive ? "N": "Y",
				indexing ? "D": "C");
		index++;
	} while (1);
}

int main(void)
{
	/*
	 * First we need to check the ID flag (bit 21) in the EFLAGS register
	 * indicates support for the CPUID instruction. If we can set and clear
	 * this flag, the processor executing the procedure supports the CPUID
	 * instruction, else we bail out.
	 */
#if defined(__GNUC__)
#elif defined(__x86_64__)
	uint64_t rflags, tmp;
	/* Read the value of rflags register. */
	asm("pushf\npop %0" : "=r" (rflags));
	/* Try to set bit 21 of the rflags register and read it again. */
	asm("pushf\norl $0x200000,(%%rsp)\npop %0" : "=r" (tmp));
	if (rflags == tmp || !(tmp & 0x200000))
		goto err_out;
	/* Try to clear bit 21 of the rflags register and read it again. */
	asm("pushf\nandl $0xFFFFFFFFFFDFFFFF,(%%rsp)\npop %0" : "=r"(tmp));
	if ((tmp & 0x200000)) {
err_out:
		fprintf(stderr,"The the processor executing the procedure "
				"does not support the CPUID instruction. \n");
		return 0;
	}
#else
#error "Unsupported arch!"
#endif
	enumerate_cache();
	return 0;
}
