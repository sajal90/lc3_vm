//#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
//#include <string.h>
//#include <termios.h>
#include <unistd.h> //unix api
#include <fcntl.h> //file control options

#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h> //terminal I/O
#include <sys/mman.h> //memory management declarations


enum
{
	R_R0=0,
	R_R1,
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
	FL_POS=1 << 0,
	FL_ZRO=1 << 1,
	FL_NEG=1 << 2,
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

enum
{
	MR_KBSR = 0xFE00, // keyboard status
	MR_KBDR = 0xFE02, // keyboard data

};

#define MAX_MEM (1 << 16)
uint16_t memory[MAX_MEM];
uint16_t regs[R_COUNT];
int running=1;

struct termios original_tio;

void disable_input_buffering()
{
	tcgetattr(STDIN_FILENO,&original_tio);
	struct termios new_tio = original_tio;
	new_tio.c_lflag &= ~ICANON & ~ECHO;
	tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

}

void restore_input_buffering()
{
	tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

uint16_t check_key()
{
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO,&readfds);

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	return select(1,&readfds, NULL, NULL, &timeout) !=0;

}

void handle_interrupt(int signal)
{
	restore_input_buffering();
	printf("\n");
	exit(-2);

}

uint16_t sign_extend(uint16_t x,int bit_count)
{
	if((x>>(bit_count-1)) & 1) //checking if last bit is 1 or 0
	{
		x |= (0xFFFF << bit_count);
	}
	return x;
}

uint16_t swap16(uint16_t val)
{
	return (val << 8) | (val >> 8);
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

void read_image_file(FILE *file)
{
	uint16_t origin;
	fread(&origin,sizeof(origin),1,file);
	origin = swap16(origin);

	uint16_t max_read = MAX_MEM - origin;
	uint16_t *p = memory + origin;
	size_t read = fread(p,sizeof(uint16_t), max_read,file);
	
	while(read-- > 0)
	{
		*p = swap16(*p);
		++p;
	}
}

int read_image(const char *image)
{
	FILE *fp = fopen(image,"rb");
	if(!fp)
		return 0;
	read_image_file(fp);
	fclose(fp);
	return 1;
}

void mem_write(uint16_t address, uint16_t val)
{
	memory[address] = val;
}

uint16_t mem_read(uint16_t address)
{
	if(address == MR_KBSR)
	{
		if(check_key())
		{
			memory[MR_KBSR] = (1 << 15);
			memory[MR_KBDR] = getchar();
		}
		else 
		{
			memory[MR_KBSR] = 0;
		}
	}
	return memory[address];
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
		uint16_t offset = sign_extend((instr & 0x7FF),11);
 
		regs[R_PC] += offset;
	}
	else 
	{
		uint16_t r1 = (instr >> 6) & 0x7;
		regs[R_PC] = regs[r1];
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

	mem_write(mem_read(regs[R_PC] + offset),regs[r0]);
}

void str_op(uint16_t instr)
{
	uint16_t r0 = (instr >> 9) & 0x7;
	uint16_t r1 = (instr >> 6) & 0x7;
	uint16_t offset = sign_extend((instr & 0x3F), 6);

	mem_write(regs[r1] + offset, regs[r0]);
}

void puts_trap()
{
	uint16_t *c = memory + regs[R_R0];

	while(*c)
	{
		putc((char)*c,stdout);
		++c;
	}
	fflush(stdout);

}

void getc_trap()
{

	regs[R_R0] = (uint16_t) getchar();
	update_flag(R_R0);

}

void out_trap()
{
	putc((char)regs[R_R0],stdout);
	fflush(stdout);
}

void in_trap()
{
	printf("Enter a character: ");
	char c = getchar();

	putc(c,stdout);
	fflush(stdout);
	regs[R_R0] = (uint16_t) c;

	update_flag(R_R0);
}

void putsp_trap()
{
	uint16_t *c = memory + regs[R_R0];

	while(*c)
	{
		char first = (*c) & 0xFF;
		char second = ((*c) >> 8);

		putc(first,stdout);
		if(second)
			putc(second,stdout);

		++c;
	}
	fflush(stdout);
}

void halt_trap()
{
	puts("HALT!");
	fflush(stdout);
	running = 0;
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

	signal(SIGINT, handle_interrupt);
	disable_input_buffering();

	
	regs[R_COND]=FL_ZRO;

	enum { PC_START = 0x3000 };
	regs[R_PC]= PC_START;

	while(running)
	{
		uint16_t instr = mem_read(regs[R_PC]++);
		uint16_t op = instr >> 12;
		
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
					case TRAP_OUT:
						out_trap();
						break;
					case TRAP_IN:
						in_trap();
						break;
					case TRAP_PUTSP:
						putsp_trap();
						break;
					case TRAP_HALT:
						halt_trap();
						break;
				}
				break;
			case OP_RES:
			case OP_RTI:
			default:
				abort();
				break;
		}
	}
	restore_input_buffering();
	return 0;
}
