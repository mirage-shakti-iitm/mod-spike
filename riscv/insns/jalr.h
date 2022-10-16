reg_t tmp = npc;
set_pc((RS1 + insn.i_imm()) & ~reg_t(1));
WRITE_RD(tmp);

// if(p->get_state()->comp_exception){
	// printf("True inside jalr\n");
// }