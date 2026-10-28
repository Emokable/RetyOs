/* Kindle lab client/provider. SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../build/binder.h"
static const char *name="kindle.lab.echo";
static unsigned token;
static void header(struct binder_io *msg,unsigned *buf,size_t size){
 bio_init(msg,buf,size,4);bio_put_uint32(msg,0);
 bio_put_string16_x(msg,SVC_MGR_NAME);bio_put_string16_x(msg,name);
}
static int echo(struct binder_state *bs,struct binder_transaction_data *tx,struct binder_io *msg,struct binder_io *reply){
 if(tx->code!=42 || msg->data_avail<4)return -1;
 bio_put_uint32(reply,bio_get_uint32(msg)+1);return 0;
}
int main(int argc,char **argv){
 alarm(8);if(argc!=2)return 2;
 struct binder_state *bs=binder_open(128*1024);if(!bs)return 3;
 unsigned buf[256];struct binder_io msg,reply;header(&msg,buf,sizeof(buf));
 if(!strcmp(argv[1],"publish")){
  bio_put_obj(&msg,&token);bio_put_uint32(&msg,0);
  if(binder_call(bs,&msg,&reply,0,SVC_MGR_ADD_SERVICE))return 4;
  if(reply.data_avail<4 || bio_get_uint32(&reply)!=0)return 5;
  binder_done(bs,&msg,&reply);puts("PUBLISH status=0");fflush(stdout);
  binder_loop(bs,echo);return 6;
 }
 if(binder_call(bs,&msg,&reply,0,SVC_MGR_CHECK_SERVICE))return 7;
 uint32_t h=0;
 if(reply.data_avail==4){if(bio_get_uint32(&reply)!=0)return 8;}
 else {if(reply.data_avail<sizeof(struct flat_binder_object))return 8;h=bio_get_ref(&reply);}if(h)binder_acquire(bs,h);
 binder_done(bs,&msg,&reply);printf("LOOKUP handle=%u\n",h);
 if(!strcmp(argv[1],"absent"))return h?9:0;
 if(strcmp(argv[1],"echo") || !h)return 10;
 bio_init(&msg,buf,sizeof(buf),0);bio_put_uint32(&msg,2026);
 if(binder_call(bs,&msg,&reply,h,42))return 11;
 if(reply.data_avail<4)return 12;
 uint32_t value=bio_get_uint32(&reply);printf("ECHO reply=%u\n",value);
 binder_done(bs,&msg,&reply);binder_release(bs,h);binder_close(bs);
 return value==2027?0:13;
}
