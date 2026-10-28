/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/system_properties.h>
int main(int argc,char **argv){
    alarm(5);if(argc!=2 || (strcmp(argv[1],"echo")&&strcmp(argv[1],"absent")))return 2;
    char value[PROP_VALUE_MAX];__system_property_get("sys.kindletest.started",value);
    if(strcmp(value,"1")){fprintf(stderr,"CHILD_PROPERTY_MISSING value=%s\n",value);return 3;}
    fprintf(stderr,"CHILD_SHARED_PROPERTY=%s\n",value);
    for(int i=0;i<12;i++){
        pid_t p=fork();if(p<0)return 4;
        if(!p){execl("/system/bin/sm-client-bionic","sm-client-bionic",argv[1],(char*)0);_exit(127);}
        int status;waitpid(p,&status,0);
        if(WIFEXITED(status)&&WEXITSTATUS(status)==0)return 0;
        usleep(100000);
    }
    return 5;
}
