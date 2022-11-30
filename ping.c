 #include "syshead.h"
 #include "ping.h"
 #include "logger.h"

int ping_main(char *address, int count){
    char buf[256] = {0,};

    sprintf(buf, "timeout %d ping -c %d %s > /dev/null", count + 1 ,count, address);

    int result = system(buf);

    logger(LOG_DEBUGGING ,"Pinging : %s Count : %d, Result :%s", address, count, result == 0 ? "success" : "fail" );

    return result;
}