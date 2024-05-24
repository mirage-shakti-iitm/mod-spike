// p->get_state()->checkcap_cycles++;
// printf("-");
// reg_t capctl = p->get_csr(CSR_MCAPCTL);

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