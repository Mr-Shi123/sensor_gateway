#include "utils.h"

char* trim(char *str)
{
    if(str == NULL)
        return NULL;
    
    while(isspace((unsigned char)*str))
        str++;

    if(*str == '\0')
        return str;
    
    char *end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end))
        end--;
    end[1] = '\0';

    return str;
}

void get_time_str(char* buf, size_t size)
{
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(buf, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

int is_empty_str(char* str)
{
    if(str == NULL) return 1;
    while(*str) {
        if(!isspace((unsigned char)*str)) return 0;
        str++;
    }
    return 1;
}

int is_useful_data(char *data)
{
    char name1[5], name2[5];
    double temp, hum;
    int ret = sscanf(data, "%4[^:]:%lf, %4[^:]:%lf", name1, &temp, name2, &hum);
    if(ret != 4)
        return -1;
    if(strcmp(name1, "Temp") != 0 || strcmp(name2, "Hum") != 0)
        return -1;
    return 1;
}