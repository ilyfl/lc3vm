#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>

#include <unistd.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

enum
{
    R_R0 = 0,
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
    OP_BR = 0,
    OP_ADD,
    OP_LD,
    OP_ST,
    OP_JSR,
    OP_AND,
    OP_LDR,
    OP_STR,
    OP_RTI,
    OP_NOT,
    OP_LDI,
    OP_STI,
    OP_JMP,
    OP_RES,
    OP_LEA,
    OP_TRAP,

};

enum
{
    FL_POS = 1 << 0,
    FL_ZRO = 1 << 1,
    FL_NEG = 1 << 2
};

enum
{
    TRAP_GETC  = 0x20,
    TRAP_OUT   = 0x21,
    TRAP_PUTS  = 0x22,
    TRAP_IN    = 0x23,
    TRAP_PUTSP = 0x24,
    TRAP_HALT  = 0x25
};

enum
{
    MR_KBSR = 0xFE00,
    MR_KBDR = 0xFE02
};

// 65536 locations
uint16_t memory[UINT16_MAX];
uint16_t reg[R_COUNT];

struct termios original_tio;

uint16_t sign_extend(uint16_t x, int bit_count)
{
    uint16_t negative = (x >> (bit_count - 1)) & 1;
    x |= ((0xFFFF << bit_count) * negative);
    return x;
}

void update_flags(uint16_t r)
{
    reg[R_COND] = FL_ZRO * !reg[r] | FL_NEG * (reg[r]>>15);
    reg[R_COND] |= FL_POS * !reg[R_COND];
}

uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

void read_image_file(FILE* file)
{
    uint16_t origin;
    fread(&origin, sizeof(origin), 1, file);
    origin = swap16(origin);

    uint16_t max_read = UINT16_MAX - origin;
    uint16_t* p = memory + origin;
    size_t read = fread(p, sizeof(uint16_t), max_read, file);

    while(read-- > 0)
    {
        *p = swap16(*p);
        ++p;
    }
}

int read_image(const char* image_path)
{
    FILE* file = fopen(image_path, "rb");
    if(!file)
        return 0;
    read_image_file(file);
    fclose(file);
    return 1;
}

void mem_write(uint16_t address, uint16_t val)
{
    memory[address] = val;
}

uint16_t check_key()
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

uint16_t mem_read(uint16_t address)
{
    if (address == MR_KBSR)
    {
        if (check_key())
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

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}


int main(int argc, char **argv){
    //load args
    if (argc < 2)
    {
        printf("%s [image-file1] \n", argv[0]);
        exit(2);
    }

    for(int j = 1; j < argc; ++j)
    {
        if (!read_image(argv[j]))
        {
            printf("failed to load image %s\n", argv[j]);
            exit(1);
        }
    }
    //setup
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;


    int running = 1;
    while(running)
    {
        uint16_t r;
        uint16_t *c;
        uint16_t r0;
        uint16_t r1;
        uint16_t r2;
        uint16_t imm5;
        uint16_t flag;
        uint16_t offset;
        uint16_t imm_flag;
        uint16_t pc_offset;

        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12;

        switch(op)
        {
            case OP_ADD:
                {
                    r0 = (instr >> 9) & 0x7; // 0x7 = 0111
                    r1 = (instr >> 6) & 0x7; 
                    imm_flag = (instr >> 5) & 0x1;

                    imm5 = sign_extend(instr & 0x1F, 5);
                    r2 = instr & 0x7;

                    reg[r0] = reg[r1] + !imm_flag * reg[r2] + imm5 * imm_flag;
                    
                    update_flags(r0);


                }
                break;

            case OP_AND:
                r0 = (instr >> 9) & 0x7; // 0x7 = 0111
                r1 = (instr >> 6) & 0x7; 
                imm_flag = (instr >> 5) & 0x1;

                imm5 = sign_extend(instr & 0x1F, 5);
                r2 = instr & 0x7;

                reg[r0] = reg[r1] & (!imm_flag * reg[r2] + imm5 * imm_flag);
                
                update_flags(r0);

                break;
                
            case OP_NOT:
                r0 = (instr >> 9) & 0x7;
                r1 = (instr >> 6) & 0x7;
                reg[r0] = ~reg[r1];
                update_flags(r0);
                break;
                
            case OP_BR:
                flag = ((instr >> 9) & 0x7) & reg[R_COND];
                pc_offset = sign_extend(instr & 0x1FF, 9);
                reg[R_PC] = reg[R_PC] + (pc_offset * !!flag);

                break;
                
            case OP_JMP:
                r = (instr >> 6) & 0x7;
                reg[R_PC] = reg[r];
                break;
                
            case OP_JSR:
                reg[R_R7] = reg[R_PC];
                flag = (instr >> 11) & 0x1;
                pc_offset = sign_extend(instr & 0x7FF, 11);
                r = (instr >> 6) & 0x7;
                reg[R_PC] =reg[R_PC] * flag + reg[r] * !flag + pc_offset * flag ;

                break;
                
            case OP_LD:
                r0 = (instr >> 9) & 0x7;
                pc_offset = sign_extend(instr & 0x1FF, 9);
                reg[r0] = mem_read(reg[R_PC] + pc_offset);
                update_flags(r0);
                break;
                
            case OP_LDI:
                r0 = (instr >> 9) & 0x7;
                pc_offset = sign_extend(instr & 0x1FF, 9);
                reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
                update_flags(r0);
                break;
                
            case OP_LDR:
                r0 = (instr >> 9) & 0x7;
                r1 = (instr >> 6) & 0x7;
                offset = sign_extend(instr & 0x03F, 6);
                reg[r0] = mem_read(reg[r1] + offset);
                update_flags(r0);
                break;
                
            case OP_LEA:
                r0 = (instr >> 9) & 0x7;
                pc_offset = sign_extend(instr & 0x1FF, 9);
                reg[r0] = reg[R_PC] + pc_offset;
                update_flags(r0);
                break;
                
            case OP_ST:
                r0 = (instr >> 9) & 0x7;
                pc_offset = sign_extend(instr & 0x1FF, 9);
                mem_write(reg[R_PC] + pc_offset, reg[r0]);
                
                break;
                
            case OP_STI:
                r0 = (instr >> 9) & 0x7;
                pc_offset = sign_extend(instr & 0x1FF, 9);
                mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
                break;
                
            case OP_STR:
                r0 = (instr >> 9) & 0x7;
                r1 = (instr >> 6) & 0x7;
                offset = sign_extend(instr & 0x03F, 6);
                mem_write(reg[r1] + offset, reg[r0]);

                break;
                
            case OP_TRAP:
                switch (instr & 0xFF)
                {
                    case TRAP_GETC:
                        reg[R_R0] = (uint16_t)getc(stdin);
                        break;

                    case TRAP_OUT:
                        putc((char)reg[R_R0], stdout);
                        fflush(stdout);
                        break;

                    case TRAP_PUTS:
                        c = memory + reg[R_R0];
                        while (*c)
                            putc((char)*c++, stdout);
                        fflush(stdout);
                        break;

                    case TRAP_IN:
                        printf("Enter a character: ");
                        reg[R_R0] = (uint16_t)getc(stdin);
                        putc((char)reg[R_R0], stdout);
                        break;

                    case TRAP_PUTSP:
                        c = memory + reg[R_R0];
                        while (*c)
                        {
                            char c1 = (*c) & 0xFF;
                            putc(c1, stdout);
                            char c2 = (*c++) >> 8;
                            if (c2) 
                                putc(c2, stdout);
                        }
                        fflush(stdout);
                        break;

                    case TRAP_HALT:
                        puts("HALT");
                        fflush(stdout);
                        running = 0;
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
}
