# SPDX-License-Identifier: GPL-3.0-only
"""Scalar SH dispatch for a long original byte-clear loop.

No clear implementation or owner bytes are substituted: every original
opcode, branch, delay slot and byte store executes. The small instruction
set fails closed; the linked wrapper and ownership predicates use the
independent Python interpreter. A complete state/RAM differential on short
loops binds this acceleration to that interpreter.
"""
import ctypes as ct
import os
from pathlib import Path
import shlex
import subprocess

SOURCE=r'''
#include <stdint.h>
struct state { uint32_t r[16],pc,pr,sr; uint64_t steps,writes; uint32_t first,last; };
static int one(uint8_t *memory,struct state *s,uint16_t op) {
    unsigned n=(op>>8)&15,m=(op>>4)&15;uint32_t at;
    s->steps++;
    if(op==9) return 0;
    if(op>>12==14) {s->r[n]=(uint32_t)(int32_t)(int8_t)op;return 0;}
    if(op>>12==7) {s->r[n]+=(uint32_t)(int32_t)(int8_t)op;return 0;}
    if((op&0xf00f)==0x6003) {s->r[n]=s->r[m];return 0;}
    if((op&0xf00f)==0x3002) {s->sr=(s->sr&~1u)|(s->r[n]>=s->r[m]);return 0;}
    if((op&0xf00f)==0x2000) {
        uint32_t area=s->r[n]&0xe0000000u;at=(s->r[n]&0x1fffffffu)-0x0c000000u;
        if((area!=0u && area!=0x80000000u && area!=0xa0000000u)||at>=0x1000000u) return 2;
        memory[at]=(uint8_t)s->r[m];if(!s->writes)s->first=s->r[n];s->last=s->r[n];s->writes++;return 0;
    }
    return 3;
}
int execute(uint8_t *memory,struct state *s,uint32_t begin,uint32_t end,uint32_t stop) {
    s->steps=s->writes=0;s->first=s->last=0;
    while(s->pc!=stop) {
        if(s->steps>100000000u || s->pc<begin || s->pc>end-2u || (s->pc&1u))return 1;
        uint32_t offset=(s->pc&0x1fffffffu)-0x0c000000u;
        if(offset>=0x1000000u-1u)return 2;
        uint16_t op=(uint16_t)(memory[offset]|((uint16_t)memory[offset+1]<<8));
        uint32_t target=0;int delayed=0,error;
        if(op==0x000b) {target=s->pr;delayed=1;}
        else if(op>>8==0x8d || op>>8==0x8f) {
            int taken=(int)(s->sr&1u)==(op>>8==0x8d);
            target=taken?s->pc+4u+2u*(int32_t)(int8_t)op:s->pc+4u;delayed=1;
        }
        if(delayed) {
            if(s->pc>end-4u)return 1;
            uint16_t slot=(uint16_t)(memory[offset+2]|((uint16_t)memory[offset+3]<<8));
            s->steps++;if((error=one(memory,s,slot)))return error;s->pc=target;
        } else {if((error=one(memory,s,op)))return error;s->pc+=2u;}
    }
    return 0;
}
'''

class State(ct.Structure):
    _fields_=[('r',ct.c_uint32*16),('pc',ct.c_uint32),('pr',ct.c_uint32),('sr',ct.c_uint32),
        ('steps',ct.c_uint64),('writes',ct.c_uint64),('first',ct.c_uint32),('last',ct.c_uint32)]

class OriginalClear:
    def __init__(self,directory):
        source=Path(directory)/'original-clear.c';library=Path(directory)/'original-clear.so'
        source.write_text(SOURCE)
        subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O2','-shared','-fPIC',
            '-Wall','-Wextra','-Werror',str(source),'-o',str(library)],check=True,capture_output=True)
        self.library=ct.CDLL(str(library));self.execute=self.library.execute
        self.execute.argtypes=[ct.c_void_p,ct.POINTER(State),ct.c_uint32,ct.c_uint32,ct.c_uint32]
        self.execute.restype=ct.c_int

    def run(self,cpu,entry):
        state=State();state.r[:]=cpu.r;state.pc=entry;state.pr=cpu.pr;state.sr=cpu.sr
        view=(ct.c_uint8*len(cpu.memory)).from_buffer(cpu.memory)
        result=self.execute(view,ct.byref(state),entry,entry+24,cpu.pr)
        assert result==0,('Original clear scalar SH dispatch refused',result,hex(state.pc))
        cpu.r[:]=state.r;cpu.pc=state.pc;cpu.pr=state.pr;cpu.sr=state.sr
        cpu.instructions+=state.steps
        return dict(instructions=state.steps,byte_writes=state.writes,first=state.first,last=state.last)
