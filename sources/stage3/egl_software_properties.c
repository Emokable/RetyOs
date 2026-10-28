/* SPDX-License-Identifier: Apache-2.0 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <string.h>
#include <unistd.h>
#include <sys/system_properties.h>

/* Only EGL's direct property requests select the existing software driver.
 * Other libraries and the shared Android property area retain their values. */
int property_get(const char *key,char *value,const char *fallback){
    Dl_info caller;
    if(dladdr(__builtin_return_address(0),&caller) && caller.dli_fname &&
       strstr(caller.dli_fname,"libEGL.so")){
        const char *selected=NULL;
        if(!strcmp(key,"ro.kernel.qemu"))selected=access("/system/lib/egl/libEGL_swiftshader.so",R_OK)==0?"0":"1";
        else if(!strcmp(key,"qemu.gles") || !strcmp(key,"ro.kernel.qemu.gles"))selected="0";
        if(selected){
            static int logged;
            if(!logged){const char marker[]="EGL_SOFTWARE_LOADER_OVERRIDE caller=libEGL.so\n";logged=1;write(2,marker,sizeof(marker)-1);}
            strcpy(value,selected);return 1;
        }
    }
    int length=__system_property_get(key,value);
    if(length>0)return length;
    if(!fallback){value[0]=0;return 0;}
    size_t count=strlen(fallback);if(count>=PROP_VALUE_MAX)count=PROP_VALUE_MAX-1;
    memcpy(value,fallback,count);value[count]=0;return (int)count;
}
