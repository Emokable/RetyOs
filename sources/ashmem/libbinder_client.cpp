/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <unistd.h>
#include <binder/IServiceManager.h>
#include <binder/Parcel.h>
#include <private/binder/binder_module.h>
static_assert(BINDER_CURRENT_PROTOCOL_VERSION==8,"Binder ABI8 required");
static_assert(sizeof(binder_transaction_data)==64,"64-byte transaction");
static_assert(sizeof(binder_write_read)==48,"48-byte ioctl payload");
int main(){
    alarm(6);
    android::sp<android::IServiceManager> manager=android::defaultServiceManager();
    if(manager==nullptr)return 2;
    auto service=manager->checkService(android::String16("kindle.lab.echo"));
    if(service==nullptr)return 3;
    android::Parcel request,reply;
    if(request.writeInt32(2026)!=android::NO_ERROR)return 4;
    if(service->transact(42,request,&reply)!=android::NO_ERROR)return 5;
    int value=reply.readInt32();
    printf("LIBBINDER_V8 tx=64 ioctl=48 reply=%d\n",value);
    return value==2027?0:6;
}
