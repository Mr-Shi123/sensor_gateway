#include "database.h"

//数据库查询结构体
typedef struct {
    char **data;
    int cnt;
    int size;
} select_data_t;

sqlite3 *g_db;

void ini_database()
{
    char *filename = g_config.database_config.db_file;
    int rc = sqlite3_open(filename, &g_db);
    if (rc != SQLITE_OK) {
        const char *error = sqlite3_errmsg(g_db);
        fprintf(stderr, "无法打开数据库: %s\n", error);
        log_msg("ERROR", "数据库 %s 打开失败", filename);
        sqlite3_close(g_db);
        g_db = NULL;
        return;
    }
    printf("数据库打开成功\n");
    log_msg("INFO", "数据库 %s 打开成功", filename);

    //创建一张传感器数据表
    //字段: id, 时间time, 温度temp, 湿度hum 
    char *sql = "CREATE TABLE IF NOT EXISTS sensor_data ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "time TEXT NOT NULL, "
        "temp REAL, "
        "hum REAL);";
    char *err_msg = NULL;
    rc = sqlite3_exec(g_db, sql, NULL, NULL, &err_msg);
    if(rc != SQLITE_OK)
    {
        fprintf(stderr, "传感器数据表创建|打开失败: %s\n", err_msg);
        log_msg("ERROR", "传感器数据表创建|打开失败: %s", err_msg);
        sqlite3_free(err_msg);
    }
    printf("数据表创建|打开成功\n");
    printf("数据库初始化完成\n");
}

void insert_into_db(msg_node_t **batch, int cnt)
{
    if(g_db == NULL)
    {
        printf("数据库无法打开, 写入数据失败");
        log_msg("ERROR", "数据库无法打开, 写入数据失败");
        return;
    }

    sqlite3_exec(g_db, "BEGIN;", NULL, NULL, NULL);
    for(int i = 0; i < cnt; i++)
    {
        double val1, val2;
        sscanf(batch[i]->msg, "Temp:%lf, Hum:%lf", &val1, &val2);
        char sql[256];
        snprintf(sql, sizeof(sql), "INSERT INTO sensor_data (time, temp, hum) "
            "VALUES (datetime('now', 'localtime'), %lf, %lf);" ,
            val1, val2
        );
        char *err_msg = NULL;
        int rc = sqlite3_exec(g_db, sql, NULL, NULL, &err_msg);
        if(rc != SQLITE_OK)
        {
            fprintf(stderr, "数据[%s]插入失败: %s\n", batch[i]->msg, err_msg);
            log_msg("ERROR", "数据[%s]插入失败: %s", batch[i]->msg, err_msg);
        }
        free(batch[i]);
    }
    sqlite3_exec(g_db, "COMMIT;", NULL, NULL, NULL);
}

//  数据库查询回调函数
static int select_func(void* data, int args, char** argv, char** col_name)
{
    //变量转换
    select_data_t *sd = (select_data_t*)data;
    
    //检测扩容
    if(sd->cnt >= sd->size)
    {
        sd->size += 10;
        sd->data = realloc(sd->data, sd->size * sizeof(char*));
    }

    char *res = malloc(128);
    snprintf(res, 128, "%s | %s | %s | %s\n", argv[0], argv[1], argv[2], argv[3]);
    sd->data[sd->cnt++] = res;
    return 0;
}

char** get_sensor_data(int *cnt)
{
    if(g_db == NULL)
        return NULL;
    
    char *err_msg = NULL;
    char *sql = "SELECT * FROM sensor_data;";
    select_data_t sd = {NULL, 0, 10};
    sd.data = malloc(sd.size * sizeof(char*));
    if(sd.data == NULL)
    {
        fprintf(stderr, "查询数据,内存分配失败: %s\n", strerror(errno));
        log_msg("ERROR", "查询数据内存分配失败: %s", strerror(errno));
        return NULL;
    }
    int rc = sqlite3_exec(g_db, sql, select_func, &sd, &err_msg);
    if(rc != SQLITE_OK)
    {
        fprintf(stderr, "查询传感器数据失败: %s\n", err_msg);
        log_msg("ERROR", "查询传感器数据失败: %s", err_msg);
        sqlite3_free(err_msg);
        free(sd.data);
        return NULL;
    }

    *cnt = sd.cnt;
    return sd.data;
}
