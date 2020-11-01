reg_t capctl = p->get_csr(CSR_MCAPCTL);


// if((capctl & 0x3)==0x2){
// 	throw trap_tee_comp_all_cond_fail(pc);
// }

// if((capctl != 0)){
	// p->set_csr(CSR_UTARGETCAP, (unsigned int)insn.i_imm());
	// if((unsigned int)insn.i_imm() == 255){
	// 	// go ahead
	// }
	// else if(((unsigned int)insn.i_imm() == 254) && (no_cross_comp == 1)){
	// 	// go ahead
	// }
	// else if(((unsigned int)insn.i_imm() == curr_cap) && (no_cross_comp == 0)){
	// 	// go ahead
	// }
	// else{
	// 	throw trap_tee_compartment_exception(pc);
	// }
// }


/*
MCAPCTL 12'h7ff
MCROSSCOMP_EXCEPTION 12'h7fe
MCROSSCOMP_RET_EXCEPTION 12'h7fd
MCAPMATRIXBASE 12'h7fc
MCAPPCBASEBOUND 12'h7fb

UCURRCAP 			12'h801
UTARGETCAP 			12'h802
UCHECKCAPSP 		12'h803
UCURRCAP_PCBASE 	12'h804
UCURRCAP_PCBOUND 	12'h805
UPARCAP_PCBASE 		12'h806
UANYCAP_PCBASE 		12'h807
UANYCAP_PCBOUND 	12'h808
*/