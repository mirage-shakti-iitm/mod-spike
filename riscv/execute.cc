// See LICENSE for license details.

#include "processor.h"
#include "mmu.h"
#include "sim.h"
#include <cassert>

reg_t lowest_checkcomsp = 0x90000000;

static void commit_log_stash_privilege(processor_t* p)
{
//#ifdef RISCV_ENABLE_COMMITLOG
  state_t* state = p->get_state();
  state->last_inst_priv = state->prv;
  state->last_inst_xlen = p->get_xlen();
  state->last_inst_flen = p->get_flen();
//#endif
}

static void commit_log_print_value(int width, uint64_t hi, uint64_t lo, FILE *fptr)
{
  switch (width) {
    case 16:
      fprintf(fptr,"0x%08" PRIx32 ,(uint32_t)lo & 0xFFFF);
//      fprintf(fptr,"0x%16" PRIx64 ,lo);
      break;
    case 32:
      fprintf(fptr, "0x%08" PRIx32, (uint32_t)lo);
      break;
    case 64:
      fprintf(fptr, "0x%016" PRIx64, lo);
      break;
    case 128:
      fprintf(fptr, "0x%016" PRIx64 "%016" PRIx64, hi, lo);

      break;
    default:
      abort();
  }
}

#define LAST(k,n) ((k) & ((1<<(n))-1))
#define MID(k,m,n) LAST((k)>>(m),((n)-(m)))

static void commit_log_print_insn(state_t* state, reg_t pc, insn_t insn)
{
//#ifdef RISCV_ENABLE_COMMITLOG
if(commitlog_flag){
  FILE *fptr;
  fptr=fopen("spike.dump","a");

  auto& reg = state->log_reg_write;
  int priv = state->last_inst_priv;
  int xlen = state->last_inst_xlen;
  int flen = state->last_inst_flen;
  bool fp = reg.addr & 1;
  int rd = reg.addr >> 1;
  int size = fp ? flen : xlen;
  if(MID(insn.bits(),0,7)==0x63 || MID(insn.bits(),0,7)==0x23
    || MID(insn.bits(),0,7)==0xf || MID(insn.bits(),0,7)==0x27  ||MID(insn.bits(),0,12)==0x06f || 
    MID(insn.bits(),0,12)==0x067 || MID(insn.bits(),0,12)==0x073||MID(insn.bits(),0,12)==0x033 ||
    MID(insn.bits(),0,12)==0x03b || MID(insn.bits(),0,12)==0x037||MID(insn.bits(),0,12)==0x01b || 
    (rd==0 && !fp)){
    reg.data.v[0]=0;
    reg.data.v[1]=0;
    } 
  uint32_t lo = insn.bits();
  uint16_t comp_op =(lo & 0x0000E003);
  if(comp_op==0xA001||comp_op==0xC001||comp_op==0xE001||comp_op==0xA000||comp_op==0xC000||comp_op==0xE000||comp_op==0xA002||comp_op==0xC002||comp_op==0xE002){
    reg.data.v[0]=0;
    reg.data.v[1]=0;
    } 
  if(insn.bits()!=0x0000006f && lo!=0xffffa001){
    fprintf(fptr, "%1d ",priv);
    commit_log_print_value(xlen, 0, pc, fptr);
    fprintf(fptr, " (");
    commit_log_print_value(insn.length()*8,0,insn.bits(),fptr);
    fprintf(fptr, ") %c%2d ", fp ? 'f' : 'x', rd);
    commit_log_print_value(size, reg.data.v[1], reg.data.v[0],fptr);
    fprintf(fptr,"\n");
    reg.addr = 0;
    fclose(fptr);
    }
  }
//#endif
}

inline void processor_t::update_histogram(reg_t pc)
{
#ifdef RISCV_ENABLE_HISTOGRAM
  pc_histogram[pc]++;
#endif
}

static bool is_checkcap(uint64_t insn_bit_rep)
{
  if((insn_bit_rep & MASK_CHECKCAP) == MATCH_CHECKCAP)
    return 1;
  else
    return 0;
}

static bool is_disable_cap(uint64_t insn_bit_rep, uint64_t csr_val, uint64_t rs1, uint64_t rs2)
{
  //if a csrrw instruction and writing into mcomctl register, and the LSB is 0 (i.e. disable cap)
  if( ((insn_bit_rep & MASK_CSRRW) == MATCH_CSRRW) && (csr_val==CSR_COMCTL) && (rs1==0) && rs2==0 )
    return 1;
  else
    return 0;
}

// This is expected to be inlined by the compiler so each use of execute_insn
// includes a duplicated body of the function to get separate fetch.func
// function calls.
static reg_t execute_insn(processor_t* p, reg_t pc, insn_fetch_t fetch)
{
  
  uint64_t insn_bits = fetch.insn.bits();
  uint64_t rs1 = fetch.insn.rs1();
  uint64_t rs2 = fetch.insn.rs2();
  uint64_t rd = fetch.insn.rd();
  bool is_jalr = (insn_bits & MASK_JALR)== MATCH_JALR;
  bool is_c_jalr = (insn_bits & MASK_C_JALR)== MATCH_C_JALR;
  bool is_c_jr = (insn_bits & MASK_C_JR)== MATCH_C_JR;
  // DECLARE_INSN(c_jalr, MATCH_C_JALR, MASK_C_JALR)
  bool is_ret= 0;

  // bool is_jal = (insn_bits & MASK_JAL) == MATCH_JAL;
  // bool rd_link= (rd==1 || rd==5);
  // bool rs1_link= (rs1==1 || rs1==5);
  // bool push= 0;
  // bool pop= 0;

  reg_t comctl = p->get_csr(CSR_COMCTL);
  reg_t curcompcstart = p->get_csr(CSR_CURCOMPCSTART);
  reg_t curcompcend = p->get_csr(CSR_CURCOMPCEND);
  reg_t parcompcstart = p->get_csr(CSR_PARCOMPCSTART);
  reg_t anycompcstart = p->get_csr(CSR_ANYCOMPCSTART);
  reg_t anycompcend = p->get_csr(CSR_ANYCOMPCEND);

  if(is_checkcap(insn_bits)){
    // Debug-Checkcap
    // fprintf(stdout,"\nDebug_Checkcap: %lu -> %lu / %d @ %#x \n", curr_cap, targetcom, no_cross_comp, pc);
    p->get_state()->checkcap_cycles += 1;
  }

  if(pc == 0x80000164){
    // fprintf(stdout,"\nmcause -> %x\n", p->get_csr(CSR_MCAUSE));
    // fprintf(stdout,"\nmepc -> %x\n", p->get_csr(CSR_MEPC));
    // fprintf(stdout,"\nmtval -> %x\n", p->get_csr(CSR_MTVAL));
    // fprintf(stdout, "\nRA1 = %lx\n", fetch.insn.rs1());
    // fprintf(stdout, "\nRA2 = %lx\n", fetch.insn.rs2());
    // fprintf(stdout, "\nRA3 = %lx\n", fetch.insn.rd());
    // fprintf(stdout, "\nSM cycles before shutdown => 0x%lx\n", p->get_state()->comp_cycles);
  fflush(stdout);
  }

  if(comctl == 1){
    if((pc >= curcompcstart) && (pc < curcompcend)){
      p->allow_cross_comp = 1;
    }
    else if((pc >= parcompcstart) && (pc < anycompcstart)){
      p->allow_cross_comp = 0;
    }
    else if((pc >= anycompcstart) && (pc < anycompcend)){
      p->allow_cross_comp = 1;
    }
    else if(p->is_prev_ret == 1 && is_checkcap(insn_bits)){
      p->set_csr(CSR_COMCTL, 0x0);
      // fprintf(stdout,"\nReturning from Compartment (%d) : Allowed (%d) (%x)", p->get_csr(CSR_CURRCOM), p->allow_cross_comp, p->get_csr(CSR_UCHECKCAPSP));
      // fprintf(stdout,"\nReturn from -> %d (%x)\n", p->get_csr(CSR_CURRCOM), pc);
      // fprintf(stdout, "Start SM cycle => 0x%lx\n", p->get_csr(CSR_MCYCLE));
      fflush(stdout);

      // printf("\n\n\n\n\n\n ********************************************************** \n\n\n\n\n\n");
      
      throw trap_ret_compartment(pc);
    }
    else if(p->allow_cross_comp == 0){
      p->set_csr(CSR_COMCTL, 0x0);
      fprintf(stdout,"\nCAPCTL : %d \n", p->get_csr(CSR_COMCTL));
      // exit(0);
      fprintf(stdout,"\nNOCROSS: %d %d %d %x %x", p->get_csr(CSR_COMCTL), p->is_prev_ret, p->allow_cross_comp, pc, p->get_csr(CSR_COMEPC));
      // fprintf(stdout, "Start SM cycle => 0x%lx\n", p->get_csr(CSR_MCYCLE));
      // fprintf(stdout,"\ncheckcomsp -> %x\n", p->get_csr(CSR_CHECKCOMSP));

      fflush(stdout);

      throw trap_pc_out_of_bounds(pc);
    }
    else if(is_checkcap(insn_bits)){
        p->set_csr(CSR_COMCTL, 0x0);
        reg_t targetcom = (unsigned int)fetch.insn.i_imm();
        p->set_csr(CSR_TARGETCOM, targetcom);
        // printf("\n\n\n\n\n\n ********************************************************** \n\n\n\n\n\n");

        // if((p->get_csr(CSR_TARGETCOM)) == 53){
          // fprintf(stdout, "\nGMP:%08x\n", pc);
        // }
        // fprintf(stdout, "\nIS_PREV_RET => %x\n", p->is_prev_ret);
        // fprintf(stdout,"\nCompartment transition %llu -> %llu (%x) (%x)", p->get_csr(CSR_CURRCOM), targetcom, pc, p->get_csr(CSR_CHECKCOMSP));
        // fprintf(stdout,"\nEntry -> %llu : %llu\n", p->get_csr(CSR_CURRCOM), targetcom);
        // fprintf(stdout, "Start SM cycle => 0x%lx @ 0x%lx\n", p->get_csr(CSR_MCYCLE), pc);
        // if(p->get_csr(CSR_CURRCOM) == 27 && (targetcom == 40)){
          // fprintf(stdout,"\nEntry27_40 : %x\n", pc);
          // fflush(stdout);
        // }

        if(lowest_checkcomsp > p->get_csr(CSR_CHECKCOMSP)){
          fprintf(stdout,"\ncheckcomsp -> %x\n", p->get_csr(CSR_CHECKCOMSP));
          lowest_checkcomsp = p->get_csr(CSR_CHECKCOMSP);
        }
        
        fflush(stdout);

        // state.pc = 0;
        throw trap_ent_compartment(pc);

      }
    else{
      fprintf(stdout,"\nBypass Capctl: %d %d %d %d %x %x", p->get_csr(CSR_CURRCOM), p->get_csr(CSR_COMCTL), p->is_prev_ret, p->allow_cross_comp, pc, p->get_csr(CSR_COMEPC));
      fflush(stdout);
      fprintf(stdout,"\nNOCRO: %d %x %x %d",  p->allow_cross_comp, pc, p->get_csr(CSR_COMEPC), is_checkcap(insn_bits));
      fflush(stdout);
      p->set_csr(CSR_COMCTL, 0x0);
      throw trap_illegal_comp_entry_point(pc);
    }
  }
  
  if(is_c_jalr){
    uint64_t imm_operand = (unsigned int)fetch.insn.i_imm();
    // fprintf(stdout, "\nc.jalr : %x %x %x %x %x", insn_bits, pc, rd, rs1, imm_operand);
  }

  if(is_c_jr){
    uint64_t imm_operand = (unsigned int)fetch.insn.i_imm();
    // fprintf(stdout, "\nc.jr : %x %lu %lu %lu %lu", insn_bits, rd, rs1, rs2, imm_operand);
  }

  if((is_jalr && rs1 == 1 && rd == 0) || (is_c_jr && rd == 1)){
      uint64_t imm_operand = (unsigned int)fetch.insn.i_imm();
      
      // fprintf(stdout, "\n%x %lu %lu %lu %lu, %lu, %lu", pc, rd, rs1, rs2, imm_operand, is_jalr, is_c_jr);
      is_ret = 1;
      if(imm_operand && (is_jalr && rs1 == 1))
        is_ret = 0;
      else{
        // fprintf(stdout, "\n%x %lu %lu %lu %lu", pc, rd, rs1, rs2, imm_operand);
      }
      // fprintf(stdout, "\nret : %x %d %d %d %d", pc, is_jalr, is_c_jalr, rd, rs1);
  }


  if(((is_jalr && rs1 == 7 && rd == 0) || (is_c_jr && rd == 7)) && (p->get_state()->comp_exception == 1 )){
    // printf("Compexception %d", p->get_state()->comp_exception);
    // printf("Setting COMCTL\n");
    p->set_csr(CSR_COMCTL, 0x1);
    p->get_state()->comp_exception = false;
    p->get_state()->end_sm_cycle = p->get_csr(CSR_MCYCLE);  
    // fprintf(stdout, "End SM cycle => 0x%lx\n", p->get_csr(CSR_MCYCLE));
    p->get_state()->comp_cycles += p->get_state()->end_sm_cycle - p->get_state()->start_sm_cycle;
    p->set_csr(CSR_MIE, p->get_state()->comp_mie);
  }
  

  
  

  p->is_prev_ret = is_ret;


commit_log_stash_privilege(p);
  reg_t npc = fetch.func(p, fetch.insn, pc);
  if (npc != PC_SERIALIZE_BEFORE) {
    commit_log_print_insn(p->get_state(), pc, fetch.insn);
    p->update_histogram(pc);
  }
  
  
  // if(p->is_prev_branch && (comctl & 0x3)==0x2) {

    // bool cond1= ( (pc>= pc_base) && (pc < pc_bound) );
    // bool dis_cap= is_disable_cap(insn_bits, fetch.insn.csr(), rs1, rs2);
    // bool cond2= is_checkcap(insn_bits) || dis_cap;
 //    if(p->is_prev_ret) {
 //      reg_t pc_base= MMU.load_int64(ushadowsp-16);
 //      reg_t cap= (pc_base & 0xfff0000000000000) >> 52;
 //      pc_base= pc_base & 0x000fffffffffffff;
  //    p->set_csr(CSR_UPCBASE, pc_base);
 //      p->set_csr(CSR_CURRCOM, cap);
  //    p->set_csr(CSR_UPCBOUND, MMU.load_int64(ushadowsp-8));
  //    p->set_csr(CSR_USHADOWSP,ushadowsp-16);
 //    }
 //    else if(is_checkcap(insn_bits)) {
 //      reg_t curr_cap = p->get_csr(CSR_CURRCOM);
 //      pc_base= pc_base | (curr_cap<<52);
  //    MMU.store_uint64(ushadowsp, pc_base);
  //    p->set_csr(CSR_USHADOWSP,ushadowsp+8);
 //    }
 //    else if(!cond1 && !cond2) {
  //    fprintf(stderr,"Arjun: PC Out-of-bounds and no Checkcap for pc: %lx pc_base: %lx pc_bound: %lx\n", pc, pc_base, pc_bound);
  //    p->is_prev_branch= 0;
  //    throw trap_tee_pc_bounds_exception(pc);
  //  }

  //  //if(cond1==0) {  //Target PC beyond current range
  //  //  //As stated in Page 22 of RISC-V spec 20190608, if push and pop are 1, pop should happen first, followed by push.
  //  //  if(p->is_prev_ret) {
  //  //    reg_t ushadowsp = p->get_csr(CSR_USHADOWSP);
  //  //    p->set_csr(CSR_UPCBOUND, MMU.load_int64(ushadowsp));
  //  //    p->set_csr(CSR_UPCBASE, MMU.load_int64(ushadowsp-8));
  //  //    p->set_csr(CSR_USHADOWSP,ushadowsp-16);
  //  //  }
 //    //  else if(p->is_prev_call) {
  //  //    reg_t ushadowsp = p->get_csr(CSR_USHADOWSP);
  //  //    MMU.store_uint64(ushadowsp, pc_base);
  //  //    MMU.store_uint64(ushadowsp+8, pc_bound);
  //  //    p->set_csr(CSR_USHADOWSP,ushadowsp+16);
  //  //  }
  //  //}

  // }
    // if(is_jal && rd_link) {
    //    push= 1;
    // }
    // else if(is_jalr) {
    //  if(!rd_link && rs1_link) {
    //    pop= 1;
  //       is_ret= 1;
  //     }
    //  else if(rd_link && !rs1_link)
    //    push= 1;
    //  else if(rd_link && rs1_link) {
    //    push= 1;
    //    if(rs1!=rd)
    //      pop= 1;
    //  }
    // }

    // p->is_prev_call= push & !pop;
    

   //  bool is_csrrw= (insn_bits & MASK_CSRRW) == MATCH_CSRRW;
   //  if(is_csrrw && (fetch.insn.i_imm()==0x812)) { //write pc_bound
      // MMU.store_uint64(ushadowsp, pc_bound);
      // p->set_csr(CSR_USHADOWSP,ushadowsp+8);
   //  }

  return npc;
}

bool processor_t::slow_path()
{
  return debug || state.single_step != state.STEP_NONE || state.dcsr.cause;
}

// fetch/decode/execute loop
void processor_t::step(size_t n)
{
  if (state.dcsr.cause == DCSR_CAUSE_NONE) {
    if (halt_request) {
      enter_debug_mode(DCSR_CAUSE_DEBUGINT);
    } // !!!The halt bit in DCSR is deprecated.
    else if (state.dcsr.halt) {
      enter_debug_mode(DCSR_CAUSE_HALT);
    }
  }

  while (n > 0) {
    size_t instret = 0;
    reg_t pc = state.pc;
    mmu_t* _mmu = mmu;

    #define advance_pc() \
     if (unlikely(invalid_pc(pc))) { \
       switch (pc) { \
         case PC_SERIALIZE_BEFORE: state.serialized = true; break; \
         case PC_SERIALIZE_AFTER: ++instret; break; \
         case PC_SERIALIZE_WFI: n = ++instret; break; \
         default: abort(); \
       } \
       pc = state.pc; \
       break; \
     } else { \
       state.pc = pc; \
       instret++; \
     }

    try
    {
      take_pending_interrupt();

      if (unlikely(slow_path()))
      {
        while (instret < n)
        {
          if (unlikely(!state.serialized && state.single_step == state.STEP_STEPPED)) {
            state.single_step = state.STEP_NONE;
            if (state.dcsr.cause == DCSR_CAUSE_NONE) {
              enter_debug_mode(DCSR_CAUSE_STEP);
              // enter_debug_mode changed state.pc, so we can't just continue.
              break;
            }
          }

          if (unlikely(state.single_step == state.STEP_STEPPING)) {
            state.single_step = state.STEP_STEPPED;
          }

          insn_fetch_t fetch = mmu->load_insn(pc);
          if (debug && !state.serialized)
            disasm(fetch.insn);
          pc = execute_insn(this, pc, fetch);

          advance_pc();

          if (unlikely(state.pc >= DEBUG_ROM_ENTRY &&
                       state.pc < DEBUG_END)) {
            // We're waiting for the debugger to tell us something.
            return;
          }

        }
      }
      else while (instret < n)
      {
        // This code uses a modified Duff's Device to improve the performance
        // of executing instructions. While typical Duff's Devices are used
        // for software pipelining, the switch statement below primarily
        // benefits from separate call points for the fetch.func function call
        // found in each execute_insn. This function call is an indirect jump
        // that depends on the current instruction. By having an indirect jump
        // dedicated for each icache entry, you improve the performance of the
        // host's next address predictor. Each case in the switch statement
        // allows for the program flow to contine to the next case if it
        // corresponds to the next instruction in the program and instret is
        // still less than n.
        //
        // According to Andrew Waterman's recollection, this optimization
        // resulted in approximately a 2x performance increase.

        // This figures out where to jump to in the switch statement
        size_t idx = _mmu->icache_index(pc);

        // This gets the cached decoded instruction from the MMU. If the MMU
        // does not have the current pc cached, it will refill the MMU and
        // return the correct entry. ic_entry->data.func is the C++ function
        // corresponding to the instruction.
        auto ic_entry = _mmu->access_icache(pc);

        // This macro is included in "icache.h" included within the switch
        // statement below. The indirect jump corresponding to the instruction
        // is located within the execute_insn() function call.
        #define ICACHE_ACCESS(i) { \
          insn_fetch_t fetch = ic_entry->data; \
          pc = execute_insn(this, pc, fetch); \
          ic_entry = ic_entry->next; \
          if (i == mmu_t::ICACHE_ENTRIES-1) break; \
          if (unlikely(ic_entry->tag != pc)) break; \
          if (unlikely(instret+1 == n)) break; \
          instret++; \
          state.pc = pc; \
        }

        // This switch statement implements the modified Duff's device as
        // explained above.
        switch (idx) {
          // "icache.h" is generated by the gen_icache script
          #include "icache.h"
        }

        advance_pc();
      }
    }
    catch(trap_t& t)
    {
      
      take_trap(t, pc);
      n = instret;

      if (unlikely(state.single_step == state.STEP_STEPPED)) {
        state.single_step = state.STEP_NONE;
        enter_debug_mode(DCSR_CAUSE_STEP);
      }
    }
    catch (trigger_matched_t& t)
    {
      if (mmu->matched_trigger) {
        // This exception came from the MMU. That means the instruction hasn't
        // fully executed yet. We start it again, but this time it won't throw
        // an exception because matched_trigger is already set. (All memory
        // instructions are idempotent so restarting is safe.)

        insn_fetch_t fetch = mmu->load_insn(pc);
        pc = execute_insn(this, pc, fetch);
        advance_pc();

        delete mmu->matched_trigger;
        mmu->matched_trigger = NULL;
      }
      switch (state.mcontrol[t.index].action) {
        case ACTION_DEBUG_MODE:
          enter_debug_mode(DCSR_CAUSE_HWBP);
          break;
        case ACTION_DEBUG_EXCEPTION: {
          mem_trap_t trap(CAUSE_BREAKPOINT, t.address);
          take_trap(trap, pc);
          break;
        }
        default:
          abort();
      }
    }

    state.minstret += instret;
    n -= instret;
  }
}
