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

#include "cpu/features.h"
#include "cpu/registers.h"
#include "cpu/misc.h"

static void mrs(uint8_t rd, uint8_t sysm) {
	uint32_t rd_val = 0;

	switch ((sysm >> 3) & 0x1f) {
		case 0x0:
			if (sysm & 0x1) {
				union ipsr_t ipsr = CORE_ipsr_read();
				rd_val |= ipsr.bits.exception;
			}
			if (sysm & 0x2) {
				// Clear rd [26:24], [15:10]
				rd_val &= ~0x0700fc00;
			}
			if (!(sysm & 0x4)) {
				union apsr_t apsr = CORE_apsr_read();
				rd_val |= (apsr.storage & 0xf8000000);
				if (HaveDSPExt()) {
					rd_val |= (apsr.storage & 0xf0000);
				}
			}
			break;

		case 0x1:
			CORE_ERR_not_implemented("priviledge separation\n");
			//break;

		case 0x2:
			CORE_ERR_not_implemented("priviledge separation & FP\n");
			//break;

		default:
			CORE_ERR_unpredictable("bad sysm val\n");
	}

	CORE_reg_write(rd, rd_val);
}

static void mrs_t1(uint32_t inst) {
	uint8_t sysm = inst & 0xff;
	uint8_t rd = (inst >> 8) & 0xf;

	// if d IN {13,15} || !(Uint(SYSm) IN {0..3,5.9.16..20})
	if (BadReg(rd) || false /* XXX lazy */)
		CORE_ERR_unpredictable("bad reg\n");

	return mrs(rd, sysm);
}

static void msr(uint8_t rn, uint8_t mask, uint8_t sysm) {
	union apsr_t apsr = CORE_apsr_read();
	uint32_t rn_val = CORE_reg_read(rn);

	switch ((sysm >> 3) & 0x1f) {
		case 0x0:
			if (!(sysm & 0x4)) {
				if (mask & 0x1) {
					if (!(HaveDSPExt())) {
						CORE_ERR_unpredictable("dsp?\n");
					} else {
						apsr.storage &= ~0xf0000;
						apsr.storage |= (rn_val & 0xf0000);
					}
				}
				if (mask & 0x2) {
					apsr.storage &= ~0xf0000000;
					apsr.storage |= (rn_val & 0xf0000000);
				}
			}

			break;

		case 0x1:
			CORE_ERR_not_implemented("priviledge\n");
			//break;

		case 0x2:
			CORE_ERR_not_implemented("priviledge & others\n");
			//break;

		default:
			CORE_ERR_unpredictable("bad sysm\n");
	}

	CORE_apsr_write(apsr);
}

static void msr_t1(uint32_t inst) {
	uint8_t sysm = inst & 0xff;
	uint8_t mask = (inst >> 10) & 0x3;
	uint8_t rn = (inst >> 16) & 0xf;

	if ((mask == 0x0) || ((mask != 0x2) && !(sysm <= 3)))
		CORE_ERR_unpredictable("bad mask\n");

	// if n IN {13,15} || !(Uint(SYSm) IN {0..3,5.9.16..20})
	if (BadReg(rn) || false /* XXX lazy */)
		CORE_ERR_unpredictable("bad reg\n");

	return msr(rn, mask, sysm);
}

static void mov_imm(union apsr_t apsr, uint8_t setflags, uint32_t imm32, uint8_t rd, uint8_t carry){
	uint32_t result = imm32;

	if (rd == 15) {
		CORE_ERR_not_implemented("A1 case of mov_imm\n");
		//ALUWritePC(result);
	} else {
		CORE_reg_write(rd, result);
		if (setflags) {
			apsr.bits.N = HIGH_BIT(result);
			apsr.bits.Z = result == 0;
			apsr.bits.C = carry;
			CORE_apsr_write(apsr);
		}
	}

	DBG2("mov_imm r%02d = 0x%08x\n", rd, result);
}

// arm-thumb
static void mov_imm_t1(uint16_t inst) {
	uint8_t imm8 = inst & 0xff;
	uint8_t rd = (inst >> 8) & 0x7;

	bool setflags = !in_ITblock();
	uint32_t imm32 = imm8;

	union apsr_t apsr = CORE_apsr_read();
	bool carry = apsr.bits.C;

	OP_DECOMPILE("MOV<IT> <Rd>,#<imm8>", rd, imm8);
	return mov_imm(apsr, setflags, imm32, rd, carry);
}

// arm-v7-m
static void mov_imm_t2(uint32_t inst) {
	union apsr_t apsr = CORE_apsr_read();

	int   imm8 =  (inst & 0x000000ff);
	uint8_t Rd =  (inst & 0x00000f00) >> 8;
	int   imm3 =  (inst & 0x00007000) >> 12;
	uint8_t S = !!(inst & 0x00100000);
	uint8_t i = !!(inst & 0x04000000);

	uint8_t setflags = (S == 0x1);

	// (imm32, carry) = ThumbExpandImm_C(i:imm3:imm8, APSR.C);
	uint32_t arg;
	arg = imm8;
	arg |= (imm3 << 8);
	arg |= (i << 11);
	uint32_t imm32;
	bool carry;
	ThumbExpandImm_C(arg, apsr.bits.C, &imm32, &carry);

	if ((Rd == 13) || (Rd == 15)) {
		CORE_ERR_unpredictable("mov to SP or PC\n");
	}

	OP_DECOMPILE("MOV{S}<c>.W <Rd>,#<const>", setflags, Rd, imm32);
	return mov_imm(apsr, setflags, imm32, Rd, carry);
}

// arm-v7-m
static void mov_imm_t3(uint32_t inst) {
	union apsr_t apsr = CORE_apsr_read();

	uint8_t imm8 = inst & 0xff;
	uint8_t rd = (inst & 0xf00) >> 8;
	uint8_t imm3 = (inst & 0x7000) >> 12;
	uint8_t imm4 = (inst & 0xf0000) >> 16;
	uint8_t i = !!(inst & 0x04000000);

	// d = uint(rd);
	uint8_t setflags = false;

	uint32_t imm32 = (imm4 << 12) | (i << 11) | (imm3 << 8) | imm8;

	if (BadReg(rd))
		CORE_ERR_unpredictable("BadReg in mov_imm_t3\n");

	OP_DECOMPILE("MOVW<c> <Rd>,#<imm16>", rd, imm32);
	// carry set to 0 irrelevant since setflags is false
	return mov_imm(apsr, setflags, imm32, rd, 0);
}

static void mov_reg(uint8_t rd, uint8_t rm, bool setflags) {
	uint32_t rm_val = CORE_reg_read(rm);

	if (rd == 15) {
		CORE_ERR_not_implemented("mov_reg --> alu\n");
		//ALUWritePC(rm_val);
	} else {
		CORE_reg_write(rd, rm_val);
		if (setflags) {
			union apsr_t apsr = CORE_apsr_read();
			apsr.bits.N = HIGH_BIT(rm_val);
			apsr.bits.Z = rm_val == 0;
			CORE_apsr_write(apsr);
		}
	}

	DBG2("mov_reg r%02d = r%02d (val: %08x)\n", rd, rm, rm_val);
}

/* If <rd> and <rm> both in R0-R7 then all thumb */
// arm-v6-m, arm-v7-m, arm-thumb*
static void mov_reg_t1(uint16_t inst) {
	uint8_t rd =  (inst & 0x7);
	uint8_t rm =  (inst & 0x78) >> 3;
	uint8_t D = !!(inst & 0x80);

	// d = UInt(D:rd)
	rd |= (D << 3);

	// m = UInt(rm); unecessary

	uint8_t setflags = false;

	if ((rd == 15) && in_ITblock() && !last_in_ITblock())
		CORE_ERR_unpredictable("mov_reg_t1 unpredictable\n");

	OP_DECOMPILE("MOV<c> <Rd>,<Rm>", rd, rm);
	return mov_reg(rd, rm, setflags);
}

// arm-thumb
static void mov_reg_t2(uint16_t inst) {
	uint8_t rd = inst & 0x7;
	uint8_t rm = (inst >> 3) & 0x7;

	bool setflags = true;

	if (in_ITblock())
		CORE_ERR_unpredictable("illegal in it block\n");

	OP_DECOMPILE("MOVS <Rd>,<Rm>", rd, rm);
	return mov_reg(rd, rm, setflags);
}

// arm-v7-m
static void mov_reg_t3(uint32_t inst) {
	uint8_t rm = inst & 0xf;
	uint8_t rd = (inst >> 8) & 0xf;
	bool    S  = (inst >> 16) & 0x1;

	bool setflags = S==1;

	if (setflags && ((rd > 13) || (rm > 13)))
		CORE_ERR_unpredictable("mov_reg_t3 case 1\n");
	if (!setflags && ((rd == 15) || (rm == 15) || ((rd == 13) && (rm == 13))))
		CORE_ERR_unpredictable("mov_reg_t3 case 2\n");

	OP_DECOMPILE("MOV{S}<c>.W <Rd>,<Rm>", setflags, rd, rm);
	return mov_reg(rd, rm, setflags);
}

__attribute__ ((constructor))
static void register_opcodes_mov(void) {
	// mrs_t1: 1111 0011 1110 1111 1000 xxxx xxxx xxxx
	register_opcode_mask_32(0xf3ef8000, 0x0c107000, mrs_t1);

	// msr_t1: 1111 0011 1000 xxxx 1000 xx00 xxxx xxxx
	register_opcode_mask_32(0xf3808000, 0x0c707300, msr_t1);

	// mov1: 0010 0xxx <x's>
	register_opcode_mask_16(0x2000, 0xd800, mov_imm_t1);

	// mov_imm_t2: 1111 0x00 010x 1111 0<x's>
	register_opcode_mask_32(0xf04f0000, 0x0ba08000, mov_imm_t2);

	// mov_imm_t3: 1111 0x10 0100 xxxx 0<x's>
	register_opcode_mask_32(0xf2400000, 0x09b08000, mov_imm_t3);

	// mov_reg_t1: 0100 0110 <x's>
	register_opcode_mask_16(0x4600, 0xb900, mov_reg_t1);

	// mov_reg_t2: 0000 0000 00xx xxxx
	register_opcode_mask_16(0x0, 0xffc0, mov_reg_t2);

	// mov_reg_t3: 1110 1010 010x 1111 0000 xxxx 0000 xxxx
	register_opcode_mask_32(0xea4f0000, 0x15a0f0f0, mov_reg_t3);
}
