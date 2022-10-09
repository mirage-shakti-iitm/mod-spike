//WRITE_RD(RS1 & RS1);
require_rv64;

// sreg_t high = sext_xlen(READ_REG(insn.rd()));
sreg_t high = sext_xlen(RS2);
sreg_t low = sext_xlen(RS1);
//sreg_t stack_pointer = sext_xlen(READ_REG(X_SP));//reading value of stack pointer . 
//sreg_t s0 = sext_xlen(READ_REG(8));//reading value of s0 register. 

// printf("bound : base %016llx\n", high);
// printf("idhash : pointer %016llx\n\n", low);

unsigned long long *base = (unsigned long long *)(high & 0x00000000ffffffff);
unsigned long long *bound = (unsigned long long *)((high & 0xffffffff00000000) >> 32 );
unsigned long long *ptr = (unsigned long long *)(low & 0x00000000ffffffff);
unsigned int hash = ((low & 0xffffffff00000000) >> 32 );


//spatial check
if (ptr > bound || ptr < base){
	printf("ptr : %8x\n", ptr);
	printf("base : %8x\n", base);
	printf("bound : %8x\n", bound);
	printf("Pointer accesss out of range\n");
	exit(0);
}

/*reg_t pointer = (low & 0x00000000ffffffff);

printf("bound : base %016lx\n", high);
printf("s0 : %08lx\n",s0);
printf("stack_pointer : %08lx\n",stack_pointer);
printf("pointer : %08lx\n",pointer);*/
//20 because
// first store double of return address 8 bytes
// second store double of frame pointe 8 bytes
//then the pointer to SFC cookie comes which is 4bytes 
//printf("SFC : %08lx\n",MMU.load_uint32(s0 - 20 ));
/*
if(s0 > pointer && pointer > stack_pointer ){ // pointer pointing to stack
	printf("Stack detected\n");
	reg_t SFC = MMU.load_uint32(s0 - 20 );
	unsigned long long rand = MMU.load_uint64(SFC);
	unsigned int ret = (unsigned int)rand;
	ret = ret ^ (unsigned int)(rand >> 32);
	printf("Expected : %x and got %x\n",hash,ret);
	if(hash != ret){
		printf("Validate Error for Hash !!\n");
		exit(0);
	}

}else{ 	*/						// pointer pointing to heap
	if(base == NULL){
		printf("Validate Error !! Got NULL pointer for base\n");
		exit(0);
	}
	reg_t base1 = (high & 0x00000000ffffffff);
	unsigned long long rand = MMU.load_uint64(base1);
	unsigned int ret = (unsigned int)rand;
	ret = ret ^ (unsigned int)(rand >> 32);

	if(hash != ret && hash != 0){
		printf("Validate Error for Hash !! %x\n", hash);
		exit(0);
	}
	// printf("Val passed");
//}