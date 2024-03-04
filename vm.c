#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h> //unix api
#include <fcntl.h> //file control options

#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h> //terminal I/O
#include <sys/mman.h> //memory management declarations


enum
{
	R_R0=0,
	R_R2,
	R_R3,
	R_R4,
	R_R5,
	R_R6,
	R_R7,
	R_PC,
	R_COND,
	R_COUNT,
};

enum
{
	FL_POS=1<<0,
	FL_ZRO=1<<1,
	FL_NEG=1<<2,
};

enum
{
	OP_BR=0,  //branch 
	OP_ADD,	  //add
	OP_LD,    //load
	OP_ST,    //store
	OP_JSR,	  //jump register
	OP_AND,   //bitwise and
	OP_LDR,   //load register
	OP_STR,   //store register
	OP_RTI,   //unused
	OP_NOT,   //bitwise not
	OP_LDI,   //load indirect
	OP_STI,   //store indirect
	OP_JMP,   //jump
	OP_RES,   //reserved(unused)
	OP_LEA,   //load effective address
	OP_TRAP,  //execute trap
};

enum
{
	TRAP_GETC  = 0x20, // get char from the keyboard, not echoed
	TRAP_OUT   = 0x21, // output a char
	TRAP_PUTS  = 0x22, // output a word string
	TRAP_IN    = 0x23, // get char from the keyboard, echoed on terminal
	TRAP_PUTSP = 0x24, // output a byte string
	TRAP_HALT  = 0x25, // halt the program
};

#define MAX_MEM (1<<16)
uint16_t memory[MAX_MEM];
uint16_t regs[R_COUNT];



uint16_t sign_extend(uint16_t x,int bit_count)
{
	if((x>>(bit_count-1)) & 1) //checking if last bit is 1 or 0
	{
		x |= (0xFFFF << bit_count);
	}
	return x;
}


void update_flag(uint16_t r)
{
	if(regs[r]==0)
		regs[R_COND]=FL_ZRO;
	else if(regs[r]>>15)
		regs[R_COND]=FL_NEG;
	else
		regs[R_COND]=FL_POS;
}


void add_op(uint16_t instr)
{	
	uint16_t r0 = (instr >> 9) & 0x7;
	uint16_t r1 = (instr >> 6) & 0x7;

	uint16_t imm_flag = (instr >> 5) & 0x1;

	if(imm_flag)
	{
		uint16_t imm5 = sign_extend((instr & 0x1F),5);
		regs[r0] = regs[r1] + imm5;
	}
	else 
	{
		uint16_t r2 = (instr & 0x7);
		regs[r0] = regs[r1] + regs[r2];
	}

	update_flag(r0);
}

void ldi_op(uint16_t instr)
{
	uint16_t r0 = (instr >> 9) & 0x7;
	uint16_t offset = sign_extend((instr & 0x1FF),9);;

	regs[r0] = mem_read(mem_read(regs[R_PC] + offset));
	update_flag(r0);
}

void and_op(uint16_t instr)
{
	uint16_t r0 = (instr >> 9) & 0x7;
	uint16_t r1 = (instr >> 6) & 0x7;

	uint16_t imm_flag = (instr >> 5) & 0x1;

	if(imm_flag)
	{
		uint16_t imm = sign_extend((instr & 0x1F),5);
		regs[r0] = regs[r1] & imm;
	}
	else
	{
		uint16_t r2 = (instr & 0x7);
		regs[r0] = regs[r1] & regs[r2];
	}
	update_flag(r0);
}

void not_op(uint16_t instr)
{
	uint16_t r0 = (instr >> 9) & 0x7;
	uint16_t r1 = (instr >> 6) & 0x7;

	regs[r0] = ~regs[r1];
	
	update_flag(r0);
}

void br_op(uint16_t instr)
{
	uint16_t cond = (instr >> 9) & 0x7;
	
	uint16_t offset = sign_extend((instr & 0x1FF),9);

	if(cond & regs[R_COND])
		regs[R_PC] += offset;
}

void jmp_op(uint16_t instr)
{
	uint16_t base_r = (instr >> 6) & 0x7;
	regs[R_PC] = regs[base_r];
}

void jsr_op(uint16_t instr)
{
	uint16_t flag = (instr >> 11) & 0x1;

	regs[R_R7] = regs[R_PC];

	if(flag)
	{
		regs[R_PC] += sign_extend((instr & 0x7FF),11);
	}
	else 
	{
		uint16_t r0 = (instr >> 6) & 0x7;
		regs[R_PC] = regs[r0];
	}
}

void ld_op(uint16_t instr)
{
	uint16_t r0 = (instr >> 9) & 0x7;
	uint16_t offset = sign_extend((instr & 0x1FF),9);

	regs[r0] = mem_read(regs[R_PC] + offset);

	update_flag(r0);
}

void ldr_op(uint16_t instr)
{
	uint16_t r0 = (instr >> 9) & 0x7;
	uint16_t r1 = (instr >> 6) & 0x7;
	uint16_t offset = sign_extend((instr & 0x3F),6);

	regs[r0] = mem_read(regs[r1] + offset);
	update_flag(r0);
}

void lea_op(uint16_t instr)
{
	uint16_t r0 = (instr >> 9) & 0x7;
	uint16_t offset = sign_extend((instr & 0x1FF), 9);

	regs[r0] = regs[R_PC] + offset;
	update_flag(r0);
}

void st_op(uint16_t instr)
{
	uint16_t r0 = (instr >> 9) & 0x7;
	uint16_t offset = sign_extend((instr & 0x1FF), 9);
	
	mem_write(regs[R_PC] + offset,regs[r0]);
}

void sti_op(uint16_t instr)
{
	uint16_t r0 = (instr >> 9) & 0x7;
	uint16_t offset = sign_extend((instr & 0x1FF), 9);

	mem_write(mem_read(regs[R_PC) + offset),regs[r0]);
}

void str_op(uint16_t instr)
{
	uint16_t r0 = (instr >> 9) & 0x7;
	uint16_t r1 = (instr >> 6) & 0x7;
	uint16_t offset = sign_extend((instr & 0x3F), 6);

	mem_write(regs[r1] + offset, r0);
}

void puts_trap()
{
	uint16_t *c = memory + regs[R_R0];

	while(*c)
	{
		putc((char)*c,stdout);
	}
	fflush(stdout);

}

void getc_trap()
{
	char c = getchar();

	regs[R_R0] = (int) c;
	regs[R_R0] &= 0xFF;

	update_flag(R_R0);

}

int main(int argc,const char *argv[])
{
	if(argc<2)
	{
		printf("Usage: %s [image-file]...\n",argv[argc-1]);
		exit(2);
	}
	
	
	for(int j=1;j<argc;++j)
	{
		if(!read_image(argv[j]))
		{
			printf("Failed to load image: %s\n",argv[j]);
			exit(1);
		}
	}

	
	regs[R_COND]=FL_ZRO;

	enum { PC_START=0x3000 };
	regs[R_PC]= PC_START;

	int running=1;
	while(running)
	{
		uint16_t instr = mem_read(regs[R_PC]++);
		uint16_t op = instr>>12;
		
		switch(op)
		{
			case OP_ADD:
				add_op(instr);
				break;
			case OP_LDI:
				ldi_op(instr);
				break;
			case OP_AND:
				and_op(instr);
				break;
			case OP_NOT:
				not_op(instr);
				break;
			case OP_BR:
				br_op(instr);
				break;
			case OP_JMP:
				jmp_op(instr);
				break;
			case OP_JSR:
				jsr_op(instr);
				break;
			case OP_LD:
				ld_op(instr);
				break;
			case OP_LDR:
				ldr_op(instr);
				break;
			case OP_LEA:
				lea_op(instr);
				break;
			case OP_ST:
				st_op(instr);
				break;
			case OP_STI:
				sti_op(instr);
				break;
			case OP_STR:
				str_op(instr);
				break;
			case OP_TRAP:
				regs[R_R7] = regs[R_PC];
				switch (instr & 0xFF) 
				{
					case TRAP_PUTS:
						puts_trap();
						break;
					case TRAP_GETC:
						getc_trap();
						break;
				}
		}
	}
	return 0;
}
