#include <stdio.h>

#include "../Log.h"

#define BUILD_TIMESTAMP "1.3.1"
#define CLIENT_VERSION  "@CLIENT_VERSION@"

typedef struct
{
    const char* name; /**< name string */
    const char* value; /**< value string */
} MQTTAsync_nameValue;

MQTTAsync_nameValue* MQTTAsync_getVersionInfo(void)
{
    #define MAX_INFO_STRINGS 8
    static MQTTAsync_nameValue libinfo[MAX_INFO_STRINGS + 1];
    int i = 0;

    libinfo[i].name = "Product name";
    libinfo[i++].value = "Eclipse Paho Asynchronous MQTT C Client Library";

    libinfo[i].name = "Version";
    libinfo[i++].value = CLIENT_VERSION;
    libinfo[i].name = NULL;
    libinfo[i].value = NULL;
    return libinfo;
}

int main(int argc, char *argv[])
{
    printf("Hello World!\n");
    Log_initialize((Log_nameValue*)MQTTAsync_getVersionInfo());
    Log(TRACE_MEDIUM, -1, "addSocket: exceeded FD_SETSIZE %d", 101);
    Log(TRACE_MEDIUM, -1, "addSocket: exceeded FD_SETSIZE %d", 102);
    return 0;
}
