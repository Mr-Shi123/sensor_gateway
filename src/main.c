#include "config.h"
#include "database.h"
#include "log.h"
#include "server.h"
#include "queue.h"
#include "thread.h"

int main()
{
    load_config("./config/gateway.conf");
    open_logfile();
    int flag = ini_server();
    if(flag < 0)
        return 1;
    ini_queue();
    ini_database();
    start_thread();
    handle_events();
    close_logfile();
    
    
    return 0;
}