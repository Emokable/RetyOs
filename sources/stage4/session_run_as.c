#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <grp.h>
#include <stdio.h>
#include <sys/prctl.h>
int main(int argc,char **argv){
 if(argc<3)return 2;
 char *end;unsigned long uid=strtoul(argv[1],&end,10);
 if(*end||uid<10000||uid>65535)return 2;
 if(setgroups(0,NULL)||setgid(uid)||setuid(uid)||prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)){perror("drop uid");return 3;}
 execv(argv[2],argv+2);perror("exec");return 4;
}
