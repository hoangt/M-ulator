/* Mulator - An extensible {ARM} {e,si}mulator
 * Copyright 2011-2012  Pat Pannuto <pat.pannuto@gmail.com>
 *
 * This file is part of Mulator.
 *
 * Mulator is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Mulator is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Mulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "opcodes.h"
#include "helpers.h"

#include "cpu/registers.h"

/* From Bit Twiddling Hacks:
 * http://graphics.stanford.edu/~seander/bithacks.html#BitReverseTable */
static const unsigned char BitReverseTable256[256] = {
#   define R2(n)     n,     n + 2*64,     n + 1*64,     n + 3*64
#   define R4(n) R2(n), R2(n + 2*16), R2(n + 1*16), R2(n + 3*16)
#   define R6(n) R4(n), R4(n + 2*4 ), R4(n + 1*4 ), R4(n + 3*4 )
	R6(0), R6(2), R6(1), R6(3)
};

uint32_t reverse_bits(uint32_t v) {
	// reverse 32-bit value, 8 bits at time
	uint32_t c; // c will get v reversed

	// Option 1:
	c = (BitReverseTable256[v & 0xff] << 24) |
		(BitReverseTable256[(v >> 8) & 0xff] << 16) |
		(BitReverseTable256[(v >> 16) & 0xff] << 8) |
		(BitReverseTable256[(v >> 24) & 0xff]);

	return c;
}

// Only supports N == 32 bits
void AddWithCarry(uint32_t x, uint32_t y, bool carry_in,
		uint32_t *result, bool *carry_out, bool *overflow_out) {
	uint64_t unsigned_sum = x + y + carry_in;
	int64_t signed_sum = ((int32_t) x) + ((int32_t) y) + carry_in;

	*result = (uint32_t) unsigned_sum; // 64->32 truncation

	DBG2("x %08x, y %08x, carry %d\n", x, y, carry_in);
	DBG2("usum %09"PRIx64", ssum %09"PRIx64"\n", unsigned_sum, signed_sum);
	*carry_out    = !((uint32_t) *result == (uint32_t) unsigned_sum);
	*overflow_out = !( (int32_t) *result ==  (int32_t) signed_sum);
}

uint32_t SignExtend(uint32_t val, uint8_t bits) {
	assert((bits > 0) && (bits < 32));

	if ((val & (1 << (bits-1))) > 0) {
		val |= (0xffffffff << bits);
	}

	return val;
}

void LoadWritePC(uint32_t addr) {
	BXWritePC(addr);
}

void BXWritePC(uint32_t addr) {
	// XXX: Mode handler / Exception stuff

	SET_THUMB_BIT(addr & 0x1);

	BranchTo(addr & 0xfffffffe);
}

void BLXWritePC(uint32_t addr __attribute__ ((unused))) {
	CORE_ERR_not_implemented("BLXWritePC\n");
}

void BranchTo(uint32_t addr) {
	CORE_reg_write(PC_REG, addr);
}

void BranchWritePC(uint32_t addr) {
	if (get_isetstate() == INST_SET_ARM) {
#ifdef M_PROFILE
		assert(false && "Arm state in M profile?");
#endif
		BranchTo(addr & 0xfffffffc);
	} else if (get_isetstate() == INST_SET_JAZELLE) {
#ifdef M_PROFILE
		assert(false && "Jazelle state in M profile?");
#endif
		CORE_ERR_not_implemented("Jazelle\n");
	} else {
		BranchTo(addr & 0xfffffffe);
	}
}

void DecodeImmShift(uint8_t type, uint8_t imm5, enum SRType *shift_t, uint8_t *shift_n) {
	assert(SRType_RRX == SRType_ROR);

	switch (type) {
		case SRType_LSL:
			*shift_t = LSL;
			*shift_n = imm5;
			break;
		case SRType_LSR:
			*shift_t = LSR;
			*shift_n = (imm5 == 0) ? 32 : imm5;
			break;
		case SRType_ASR:
			*shift_t = ASR;
			*shift_n = (imm5 == 0) ? 32 : imm5;
			break;
		case SRType_RRX:
		//case SRType_ROR: // same
			*shift_t = (imm5 == 0) ? RRX : ROR;
			*shift_n = (imm5 == 0) ? 1 : imm5;
			break;
		default:
			CORE_ERR_unpredictable("Bad type DecodeImmShift\n");
	}
}

void Shift_C(
		uint32_t value, int Nbits,
		enum SRType type, uint8_t amount, bool carry_in,
		uint32_t *result, bool *carry_out) {
	assert (!((type == RRX) && (amount != 1)));

	if (amount == 0) {
		*result = value;
		*carry_out = carry_in;
	} else {
		switch (type) {
			case LSL:
				LSL_C(value, Nbits, amount, result, carry_out);
				break;
			case LSR:
				LSR_C(value, Nbits, amount, result, carry_out);
				break;
			case ASR:
				ASR_C(value, Nbits, amount, result, carry_out);
				break;
			case ROR:
				ROR_C(value, Nbits, amount, result, carry_out);
				break;
			case RRX:
				//RRX_C(value, Nbits, amount, result, carry_out);
				CORE_ERR_not_implemented("Shitf_C RRX\n");
				//break;
			default:
				CORE_ERR_unpredictable("Shift_C bad type\n");
		}
	}
}

uint32_t Shift(uint32_t value, int Nbits, enum SRType type, uint8_t amount, bool carry_in) {
	uint32_t result;
	bool carry_out;
	Shift_C(value, Nbits, type, amount, carry_in, &result, &carry_out);
	return result;
}

void LSL_C(uint32_t x, int Nbits, uint8_t shift,
		uint32_t *result, bool *carry_out) {
	assert((shift > 0) && (shift < 32));
	assert(Nbits <= 32);
	// use 64 bit type to catch overflow of shift to the 33rd bit
	uint64_t extended_x = x;
	extended_x <<= shift;
	*result = (uint32_t) (extended_x & ((1ULL << Nbits) - 1));
	*carry_out = !!(extended_x & (1ULL << Nbits));
}

void LSR_C(uint32_t x, int Nbits __attribute__ ((unused)), uint8_t shift,
		uint32_t *result, bool *carry_out) {
	assert((shift > 0) && (shift < 32));
	// extended_x = ZeroExtend(x, shift+Nbits)
	// ^^unneeded, C-language zero-fills left bits
	*result = x >> shift;
	*carry_out = (x >> (shift - 1)) & 0x1;
}

void ASR_C(uint32_t x, int Nbits, uint8_t shift,
		uint32_t *result, bool *carry_out) {
	assert((shift > 0) && (shift < 32));

	uint32_t extended_x;
	if (x & (1 << (shift - 1))) {
		// negative, need to extend
		uint32_t mask = 0xffffffff - ((1 << (shift - 1)) - 1);
		extended_x = x | mask;
	} else {
		extended_x = x;
	}

	*result = (extended_x >> shift) & ((1 << Nbits) - 1);
	*carry_out = !!(extended_x & (1 << (shift - 1)));
}

void ROR_C(uint32_t x, int Nbits, uint8_t shift,
		uint32_t *result, bool *carry_out) {
	assert((shift > 0) && (shift < 32));
	int m = shift % Nbits;
	*result = (x >> m) | (x << (Nbits - m));
	*carry_out = !!((*result) & (1 << (Nbits - 1)));
}

uint32_t ThumbExpandImm(uint32_t imm12) {
	union apsr_t apsr = CORE_apsr_read();

	uint32_t result;
	bool carry;

	ThumbExpandImm_C(imm12, apsr.bits.C, &result, &carry);

	return result;
}

void ThumbExpandImm_C(
		uint32_t imm12, bool carry_in,
		uint32_t *imm32, bool *carry_out
		) {

	if ((imm12 & 0xc00) == 0) {
		switch (imm12 & 0x300) {
			case (0x000):
				*imm32 = imm12 & 0xff;
				break;
			case (0x100):
				if ((imm12 & 0xff) == 0)
					CORE_ERR_unpredictable("ThumbExpandImm_C\n");
				*imm32 = (imm12 & 0xff);
				*imm32 |= ((imm12 & 0xff) << 16);
				break;
			case (0x200):
				if ((imm12 & 0xff) == 0)
					CORE_ERR_unpredictable("ThumbExpandImm_C\n");
				*imm32 = ((imm12 & 0xff) << 8);
				*imm32 |= ((imm12 & 0xff) << 24);
				break;
			case (0x300):
				if ((imm12 & 0xff) == 0)
					CORE_ERR_unpredictable("ThumbExpandImm_C\n");
				*imm32 = (imm12 & 0xff);
				*imm32 |= ((imm12 & 0xff) << 8);
				*imm32 |= ((imm12 & 0xff) << 16);
				*imm32 |= ((imm12 & 0xff) << 24);
				break;
			default:
				assert(false);
		}

		*carry_out = carry_in;
	} else {
		DBG2("ThumbExpandImm_C --> ROR_C\n");
		// unrotated_value is 32 bits
		uint32_t unrotated_value = (1 << 7) | (imm12 & 0x7f);
		uint8_t arg = (imm12 & 0xf80) >> 7;
		ROR_C(unrotated_value, 32, arg, imm32, carry_out);
	}

	DBG2("expanded imm12 %08x carry_in  %d\n", imm12, carry_in);
	DBG2("into     imm32 %08x carry_out %d\n", *imm32, *carry_out);
}
