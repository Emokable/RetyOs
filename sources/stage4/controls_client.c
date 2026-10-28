#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/un.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc,char **argv){
    if(argc<2)return 2;
    int fd=socket(AF_UNIX,SOCK_STREAM,0);if(fd<0){perror("socket");return 3;}
    struct sockaddr_un address={.sun_family=AF_UNIX};
    memcpy(address.sun_path+1,"kindle-controls",15);
    if(connect(fd,(struct sockaddr*)&address,offsetof(struct sockaddr_un,sun_path)+16)){perror("connect");close(fd);return 4;}
    char message[160]={0};size_t used=0;
    for(int i=1;i<argc;i++){size_t len=strlen(argv[i]);if(used+len+2>=sizeof(message)){close(fd);return 2;}
        if(i>1)message[used++]=' ';memcpy(message+used,argv[i],len);used+=len;}
    message[used++]='\n';
    if(write(fd,message,used)!=(ssize_t)used){perror("write");close(fd);return 5;}
    char response[64];ssize_t n=read(fd,response,sizeof(response));if(n>0)write(1,response,n);
    close(fd);return n>0?0:6;
}
