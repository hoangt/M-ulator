#ifndef OPCODES_H
#define OPCODES_H

#include "../../common.h"

// Use this function to register your opcode handler
// if (
//       (ones_mask  == ones_mask  &  inst) &&
//       (zeros_mask == zeros_mask & ~inst)
//    )
// the instruction is considered a match
int		register_opcode_mask(uint32_t ones_mask,
		uint32_t zeros_mask, int (*fn) (uint32_t));
int		register_opcode_mask_ex(uint32_t ones_mask,
		uint32_t zeros_mask, int (*fn) (uint32_t), ...);
/* TTTA: Why do you need to specify two masks?
 *
 *  _Hint:_ When could we consider hardware to be trinary instead of binary?
 *
 */

void register_opcodes_add(void);
void register_opcodes_branch(void);
void register_opcodes_cb(void);
void register_opcodes_cmp(void);
void register_opcodes_div(void);
void register_opcodes_extend(void);
void register_opcodes_it(void);
void register_opcodes_ldm(void);
void register_opcodes_ld(void);
void register_opcodes_logical(void);
void register_opcodes_mov(void);
void register_opcodes_mul(void);
void register_opcodes_nop(void);
void register_opcodes_pop(void);
void register_opcodes_push(void);
void register_opcodes_shift(void);
void register_opcodes_str(void);
void register_opcodes_strm(void);
void register_opcodes_sub(void);

//////////////////
// MACRO TRICKS //
//////////////////

#define register_opcode_mask(_o, _z, _f)\
	register_opcode_mask_real((_o), (_z), (_f), __FILE__":"VAL2STR(_f))
int		register_opcode_mask_real(uint32_t, uint32_t,
		int (*fn) (uint32_t), const char *);
#define register_opcode_mask_ex(_o, _z, _f, ...)\
	register_opcode_mask_ex_real((_o), (_z),(_f),\
			__FILE__":"VAL2STR(_f), __VA_ARGS__)
int		register_opcode_mask_ex_real(uint32_t, uint32_t,
		int (*fn) (uint32_t), const char*, ...);

#endif // OPCODES_H
