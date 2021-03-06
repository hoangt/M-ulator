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
#include "cpu/misc.h"

static inline void mla(uint8_t rd, uint8_t rn, uint8_t rm, uint8_t ra) {
	uint32_t rn_val = CORE_reg_read(rn);
	uint32_t rm_val = CORE_reg_read(rm);
	uint32_t ra_val = CORE_reg_read(ra);

	uint64_t operand1 = rn_val;
	uint64_t operand2 = rm_val;
	uint64_t addend = ra_val;
	uint64_t result = operand1 * operand2 + addend;

	CORE_reg_write(rd, result & 0xffffffff);
}

// arm-v7-m
static void mla_t1(uint32_t inst) {
	uint8_t rm = inst & 0xf;
	uint8_t rd = (inst >> 8) & 0xf;
	uint8_t ra = (inst >> 12) & 0xf;
	uint8_t rn = (inst >> 16) & 0xf;

	if (BadReg(rd) || BadReg(rn) || BadReg(rm) || BadReg(ra))
		CORE_ERR_unpredictable("bad reg\n");

	OP_DECOMPILE("MLA<c> <Rd>,<Rn>,<Rm>,<Ra>", rd, rn, rm, ra);
	return mla(rd, rn, rm, ra);
}

static inline void mls(uint8_t rd, uint8_t rn, uint8_t rm, uint8_t ra) {
	uint32_t rn_val = CORE_reg_read(rn);
	uint32_t rm_val = CORE_reg_read(rm);
	uint32_t ra_val = CORE_reg_read(ra);

	uint32_t operand1 = rn_val;
	uint32_t operand2 = rm_val;
	uint32_t addend = ra_val;
	uint32_t result = addend - operand1 * operand2;
	CORE_reg_write(rd, result);
}

// arm-v7-m
static void mls_t1(uint32_t inst) {
	uint8_t rm = inst & 0xf;
	uint8_t rd = (inst >> 8) & 0xf;
	uint8_t ra = (inst >> 12) & 0xf;
	uint8_t rn = (inst >> 16) & 0xf;

	if (BadReg(rd) || BadReg(rn) || BadReg(rm) || BadReg(ra))
		CORE_ERR_unpredictable("bad reg\n");

	OP_DECOMPILE("MLS<c> <Rd>,<Rn>,<Rm>,<Ra>", rd, rn, rm, ra);
	return mls(rd, rn, rm, ra);
}

static void mul(uint8_t setflags, uint8_t rd, uint8_t rn, uint8_t rm) {
	uint32_t result;

	result = CORE_reg_read(rn) * CORE_reg_read(rm);
	CORE_reg_write(rd, result);

	if (setflags) {
		union apsr_t apsr = CORE_apsr_read();
		apsr.bits.N = HIGH_BIT(result);
		apsr.bits.Z = result == 0;
		CORE_apsr_write(apsr);
	}
}

// arm-thumb
static void mul_t1(uint16_t inst) {
	uint8_t rdm = inst & 0x7;
	uint8_t rn = inst & 0x7;

	bool setflags = !in_ITblock();

	OP_DECOMPILE("MUL<IT> <Rdm>,<Rn>,<Rdm>", rdm, rn, rdm);
	return mul(setflags, rdm, rn, rdm);
}

// arm-v7-m
static void mul_t2(uint32_t inst) {
	uint8_t rm = (inst & 0xf);
	uint8_t rd = (inst & 0xf00) >> 8;
	uint8_t rn = (inst & 0xf0000) >> 16;

	if ((rd >= 13) || (rn >= 13) || (rm >= 13)) {
		CORE_ERR_unpredictable("mul_t2 bad reg\n");
	}

	OP_DECOMPILE("MUL<c> <Rd>,<Rn>,<Rm>", rd, rn, rm);
	return mul(false, rd, rn, rm);
}

static void smull(uint8_t rdlo, uint8_t rdhi, uint8_t rn, uint8_t rm,
		bool setflags __attribute__ ((unused))) {
	assert(setflags == false);

	int64_t rn_val = CORE_reg_read(rn);
	int64_t rm_val = CORE_reg_read(rm);
	int64_t result = rn_val * rm_val;

	CORE_reg_write(rdhi, (result >> 32) & 0xffffffff);
	CORE_reg_write(rdlo, result & 0xffffffff);
}

// arm-v7-m
static void smull_t1(uint32_t inst) {
	uint8_t rm = inst & 0xf;
	uint8_t rdhi = (inst >> 8) & 0xf;
	uint8_t rdlo = (inst >> 12) & 0xf;
	uint8_t rn = (inst >> 16) & 0xf;

	bool setflags = false;

	if (BadReg(rdlo) || BadReg(rdhi) || BadReg(rn) || BadReg(rm))
		CORE_ERR_unpredictable("smull bad reg\n");

	if (rdhi == rdlo)
		CORE_ERR_unpredictable("smull same hi lo regs\n");

	OP_DECOMPILE("SMULL<c> <RdLo>,<RdHi>,<Rn>,<Rm>",
			rdlo, rdhi, rn, rm);
	return smull(rdlo, rdhi, rn, rm, setflags);
}

// arm-v7-m
static void umlal_t1(uint32_t inst) {
	uint8_t rm = inst & 0xf;
	uint8_t rdhi = (inst >> 8) & 0xf;
	uint8_t rdlo = (inst >> 12) & 0xf;
	uint8_t rn   = (inst >> 16) & 0xf;

	if (BadReg(rdlo) || BadReg(rdhi) || BadReg(rn) || BadReg(rm))
		CORE_ERR_unpredictable("umlal_t1 case 1\n");
	if (rdhi == rdlo)
		CORE_ERR_unpredictable("umlal_t1 case 2\n");

	OP_DECOMPILE("UMLAL<c> <RdLo>,<RdHi>,<Rn>,<Rm>",
			rdlo, rdhi, rn, rm);

	//
	uint64_t rn_val = CORE_reg_read(rn);
	uint64_t rm_val = CORE_reg_read(rm);
	uint64_t rdlo_val = CORE_reg_read(rdlo);
	uint64_t rdhi_val = CORE_reg_read(rdhi);
	uint64_t rd_val = rdlo_val | (rdhi_val << 32);
	uint64_t result = rn_val * rm_val + rd_val;

	CORE_reg_write(rdhi, result >> 32);
	CORE_reg_write(rdlo, result & 0xffffffff);
}

static void umull(uint8_t rdlo, uint8_t rdhi, uint8_t rn, uint8_t rm,
		bool setflags __attribute__ ((unused))) {
	assert(setflags == false);

	uint64_t rn_val = CORE_reg_read(rn);
	uint64_t rm_val = CORE_reg_read(rm);
	uint64_t result = rn_val * rm_val;

	CORE_reg_write(rdhi, (result >> 32) & 0xffffffff);
	CORE_reg_write(rdlo, result & 0xffffffff);
}

// arm-v7-m
static void umull_t1(uint32_t inst) {
	uint8_t rm = inst & 0xf;
	uint8_t rdhi = (inst >> 8) & 0xf;
	uint8_t rdlo = (inst >> 12) & 0xf;
	uint8_t rn = (inst >> 16) & 0xf;

	bool setflags = false;

	if (BadReg(rdlo) || BadReg(rdhi) || BadReg(rn) || BadReg(rm))
		CORE_ERR_unpredictable("umull bad reg\n");

	if (rdhi == rdlo)
		CORE_ERR_unpredictable("umull same hi lo regs\n");

	OP_DECOMPILE("UMULL<c> <RdLo>,<RdHi>,<Rn>,<Rm>",
			rdlo, rdhi, rn, rm);
	return umull(rdlo, rdhi, rn, rm, setflags);
}

__attribute__ ((constructor))
static void register_opcodes_mul(void) {
	// mla_t1: 1111 1011 0000 xxxx xxxx xxxx 0000 xxxx
	register_opcode_mask_32_ex(0xfb000000, 0x04f000f0, mla_t1,
			0xf000, 0x0,
			0, 0);

	// mls_t1: 1111 1011 0000 xxxx xxxx xxxx 0001 xxxx
	register_opcode_mask_32(0xfb000010, 0x04f000e0, mls_t1);

	// mul_t1: 0100 0011 01xx xxxx
	register_opcode_mask_16(0x4340, 0xbc80, mul_t1);

	// mul_t2: 1111 1011 0000 xxxx 1111 xxxx 0000 xxxx
	register_opcode_mask_32(0xfb00f000, 0x04f000f0, mul_t2);

	// smull_t1: 1111 1011 1000 xxxx xxxx xxxx 0000 xxxx
	register_opcode_mask_32(0xfb800000, 0x047000f0, smull_t1);

	// umlal_t1: 1111 1011 1110 xxxx xxxx xxxx 0000 xxxx
	register_opcode_mask_32(0xfbe00000, 0x041000f0, umlal_t1);

	// umull_t1: 1111 1011 1010 xxxx xxxx xxxx 0000 xxxx
	register_opcode_mask_32(0xfba00000, 0x045000f0, umull_t1);
}
