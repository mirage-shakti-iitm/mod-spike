//WRITE_RD(RS1 & RS1);
require_rv64;
//unsigned long long *address = (unsigned long long *)sext_xlen(RS1);
//printf("address of hash : %p \n",address);
unsigned long long rand = MMU.load_uint64(RS1);
//printf("random number : %llu \n",rand);
unsigned int ret = (unsigned int)rand;
ret = ret ^ (unsigned int)(rand >> 32);
//printf("%u\n",ret);
WRITE_RD(ret);