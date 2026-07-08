// ================================================================
// 数据库操作：打开/关闭/写入四张表
// 使用 SQLite 预编译语句，防止 SQL 注入，保证类型安全
// ================================================================

#include "ipc/db.h"
#include <sqlite3.h>      // SQLite 的 C 语言 API 头文件
#include <stdio.h>
#include <string.h>

// ================================================================
// 建表 SQL（四张表，用 IF NOT EXISTS 保证重复执行不会报错）
// ================================================================
static const char* SQL_CREATE_TABLES =
    // -------- 考勤表（签到/签退）--------
    "CREATE TABLE IF NOT EXISTS attendance ("             // 如果表已存在就跳过
    "  id          INTEGER PRIMARY KEY,"                  // 主键：自增ID，唯一标识每一行
    "  person_name TEXT    NOT NULL,"                     // 人名：文本，不能为空
    "  type        TEXT    NOT NULL,"                     // 类型："签到"或"签退"
    "  room_id     INTEGER DEFAULT 0,"                    // 房间号（与 face_mapping 对齐）
    "  time        DATETIME DEFAULT (datetime('now', 'localtime'))"    // 时间：不填就自动用本地当前时间
    ");"
    // -------- 设备使用表 --------
    "CREATE TABLE IF NOT EXISTS device_usage ("
    "  id               INTEGER PRIMARY KEY,"               // 主键自增
    "  person_name      TEXT    NOT NULL,"                  // 谁用的
    "  room_id          INTEGER NOT NULL,"                  // 哪个房间（对应哪路监控）
    "  device_id        INTEGER NOT NULL,"                  // 哪个设备（房间内的设备编号）
    "  duration_minutes INTEGER NOT NULL,"                  // 用了多少分钟
    "  time             DATETIME DEFAULT (datetime('now', 'localtime'))"  // 自动记录登记时间
    ");"
    // -------- 录像记录表 --------
    "CREATE TABLE IF NOT EXISTS video_records ("
    "  id         INTEGER PRIMARY KEY,"                     // 主键自增
    "  stream_id  INTEGER  NOT NULL,"                       // 哪路监控流
    "  start_time DATETIME NOT NULL,"                       // 录像开始时间
    "  end_time   DATETIME NOT NULL,"                       // 录像结束时间
    "  file_path  TEXT     NOT NULL,"                        // 录像文件路径
    "  record_type TEXT    DEFAULT 'unregistered'"          // 录像是否有人登记
    ");"
    // -------- 人脸特征ID → 人名映射表  --------
    "CREATE TABLE IF NOT EXISTS face_mapping ("
    "  feature_id  INTEGER PRIMARY KEY,"                    // InspireFace 分配的 feature_id（主键，唯一）
    "  person_name TEXT    NOT NULL,"                       // 对应的人名
    "  room_id     INTEGER DEFAULT 0"                       // 所属房间号
    ");"
    // -------- 智能插座配置表（device_process 读写，QT 只读） --------
    "CREATE TABLE IF NOT EXISTS device_plugs ("
    "  id         INTEGER PRIMARY KEY,"                     // 主键自增
    "  room_id    INTEGER NOT NULL,"                        // 房间号（0-7，对应监控流）
    "  device_id  INTEGER NOT NULL,"                        // 设备编号（与主进程 ROI 设备号对应）
    "  name       TEXT    NOT NULL,"                        // 使用插座的设备名称（如"示波器"）
    "  ip         TEXT    NOT NULL,"                        // 插座 IP
    "  token      TEXT    NOT NULL,"                        // 32 位 hex token
    "  enabled    INTEGER DEFAULT 1,"                       // 是否启用（0/1）
    "  UNIQUE(room_id, device_id)"                          // 同房间同设备号唯一
    ");"
    // -------- ESP32 设备表（room_esp32）--------
    // 每个房间绑定一个 ESP32（room_id 唯一），用于被动人脸识别推流
    "CREATE TABLE IF NOT EXISTS room_esp32 ("
    "  id         INTEGER PRIMARY KEY,"                     // 主键自增
    "  room_id    INTEGER NOT NULL UNIQUE,"                 // 房间号（唯一，一间一个 ESP32）
    "  name       TEXT    NOT NULL,"                        // 备注名（如"实验室ESP32"）
    "  esp32_ip   TEXT    NOT NULL,"                        // ESP32 IP（用于 RTSP 拉流）
    "  rtsp_url   TEXT    NOT NULL,"                        // RTSP 流地址
    "  online     INTEGER DEFAULT 0,"                       // 在线状态（0离线 1在线）
    "  last_seen  INTEGER DEFAULT 0"                        // 最后心跳时间戳（秒）
    ");";

// ================================================================
// 数据库安全配置（每个进程打开数据库时执行一次）
// ================================================================
static const char* SQL_PRAGMA =
    "PRAGMA journal_mode=WAL;"        // WAL模式：写不阻塞读，读不阻塞写（多进程安全）
    "PRAGMA busy_timeout=3000;"       // 写锁等待3秒：别的进程在写就排队等，不直接报错
    "PRAGMA synchronous=NORMAL;";     // 安全级别：正常模式，平衡安全和性能

// ================================================================
// 表结构升级（旧库迁移：给 attendance / face_mapping 补 room_id 列）
// ALTER TABLE ADD COLUMN 在列已存在时会报错，用 sqlite3_exec 忽略即可
// ================================================================
static const char* SQL_MIGRATE_ROOM_ID[] = {
    "ALTER TABLE attendance  ADD COLUMN room_id INTEGER DEFAULT 0;",
    "ALTER TABLE face_mapping ADD COLUMN room_id INTEGER DEFAULT 0;",
    NULL
};

// ================================================================
// 打开数据库
// 参数：
//   db_path：数据库文件路径，如 "/data/records.db"
// 返回：
//   成功返回 sqlite3* 指针，失败返回 NULL
// ================================================================
void* db_open(const char* db_path)
{
    sqlite3* db = NULL;                                    // 声明数据库句柄，先置空

    // -------- 打开文件，不存在会自动创建 --------
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {        // 返回值不等于 SQLITE_OK 就是失败
        fprintf(stderr, "[DB] 打开失败: %s\n", sqlite3_errmsg(db)); // 打印具体错误信息
        return NULL;
    }

    // -------- 执行初始化 SQL：设置 WAL + 建表 --------
    char* err = NULL;
    if (sqlite3_exec(db, SQL_PRAGMA, NULL, NULL, &err) != SQLITE_OK ||     // 设置 WAL 等参数
        sqlite3_exec(db, SQL_CREATE_TABLES, NULL, NULL, &err) != SQLITE_OK) { // 建四张表
        fprintf(stderr, "[DB] 初始化失败: %s\n", err); // 打印 SQL 执行错误
        sqlite3_free(err);                              // 错误信息需要手动释放
        sqlite3_close(db);                              // 关掉数据库
        return NULL;
    }

    // -------- 旧库迁移：给 attendance / face_mapping 补 room_id 列 --------
    // 列已存在时 sqlite3_exec 返回错误，忽略即可（多进程同时执行也安全，最多重复添加失败）
    for (int i = 0; SQL_MIGRATE_ROOM_ID[i] != NULL; i++) {
        char* merr = NULL;
        sqlite3_exec(db, SQL_MIGRATE_ROOM_ID[i], NULL, NULL, &merr);
        if (merr) sqlite3_free(merr);   // 列已存在时错误信息丢弃
    }

    printf("[DB] 已打开: %s (WAL)\n", db_path);         // 打开成功
    return db;                                           // 返回数据库句柄
}

// ================================================================
// 关闭数据库
// ================================================================
void db_close(void* db)
{
    if (db) {                                            // 非空才关
        sqlite3_close((sqlite3*)db);                     // 关闭数据库，释放资源
        printf("[DB] 已关闭\n");
    }
}

// ================================================================
// 插入一条考勤记录
// 参数：
//   db：数据库句柄
//   person_name：人名，如 "张三"
//   type：类型，"签到" 或 "签退"
//   room_id：所属房间号
// 返回：0成功，-1失败，-2今日已操作
// ================================================================
int db_insert_attendance(void* db, const char* person_name, const char* type, int room_id)
{
    sqlite3_stmt* stmt = NULL;                           // 预编译语句句柄

    // 1. 查询今天是否已有同类型记录
    const char* check_sql =
        "SELECT COUNT(*) FROM attendance "
        "WHERE person_name = ? AND type = ? "
        "AND date(time) = date('now', 'localtime')";
    sqlite3_prepare_v2((sqlite3*)db, check_sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, person_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, type, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) > 0) {
        sqlite3_finalize(stmt);
        printf("[DB] %s 今日已%s，跳过\n", person_name, type);
        return -2;
    }
    sqlite3_finalize(stmt);

    // 2. 插入新记录
    const char* sql = "INSERT INTO attendance (person_name, type, room_id) VALUES (?, ?, ?)";

    sqlite3_prepare_v2((sqlite3*)db, sql, -1, &stmt, NULL);

    sqlite3_bind_text(stmt, 1, person_name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, type,       -1, SQLITE_STATIC);
    sqlite3_bind_int( stmt, 3, room_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[DB] attendance 写入失败\n");
        return -1;
    }
    printf("[DB] 考勤: %s %s (房间%d)\n", person_name, type, room_id);
    return 0;
}

// ================================================================
// 插入一条设备使用记录
// 参数：
//   db：数据库句柄
//   person_name：人名
//   room_id：哪个房间（对应 stream_id）
//   device_id：哪个设备
//   duration_minutes：使用了多少分钟
// 返回：0成功，-1失败
// ================================================================
int db_insert_device_usage(void* db, const char* person_name,
                           int room_id, int device_id, int duration_minutes)
{
    // SQL 模板：4 个占位符，分别对应人名、房间、设备、时长
    const char* sql =
        "INSERT INTO device_usage (person_name, room_id, device_id, duration_minutes) "
        "VALUES (?, ?, ?, ?)";

    sqlite3_stmt* stmt;                                  // 预编译语句句柄

    // 1. 准备
    sqlite3_prepare_v2((sqlite3*)db, sql, -1, &stmt, NULL);

    // 2. 逐个绑定（文本用 bind_text，整数用 bind_int）
    sqlite3_bind_text(stmt, 1, person_name,      -1, SQLITE_STATIC); // 第1个? = 人名
    sqlite3_bind_int( stmt, 2, room_id);                              // 第2个? = 房间号
    sqlite3_bind_int( stmt, 3, device_id);                            // 第3个? = 设备号
    sqlite3_bind_int( stmt, 4, duration_minutes);                     // 第4个? = 使用时长

    // 3. 执行
    int rc = sqlite3_step(stmt);

    // 4. 释放
    sqlite3_finalize(stmt);

    // 5. 检查结果
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[DB] device_usage 写入失败\n");
        return -1;
    }
    printf("[DB] 设备使用: %s 房间%d 设备%d %d分钟\n",
           person_name, room_id, device_id, duration_minutes);
    return 0;
}

// ================================================================
// 插入一条录像记录
// 参数：
//   db：数据库句柄
//   stream_id：哪路监控流
//   start_time：录像开始时间字符串，如 "20260501_100000"
//   end_time：录像结束时间字符串
//   file_path：录像文件路径
// 返回：0成功，-1失败
// ================================================================
int db_insert_video_record(void* db, int stream_id,
                           const char* start_time, const char* end_time,
                           const char* file_path, const char* record_type)
{
    // SQL 模板：5 个占位符
    const char* sql =
        "INSERT INTO video_records (stream_id, start_time, end_time, file_path, record_type) "
        "VALUES (?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt;

    // 1. 准备
    sqlite3_prepare_v2((sqlite3*)db, sql, -1, &stmt, NULL);

    // 2. 逐个绑定：第一个是整数，后面三个是文本
    sqlite3_bind_int( stmt, 1, stream_id);                             // 第1个? = 流号
    sqlite3_bind_text(stmt, 2, start_time, -1, SQLITE_STATIC);         // 第2个? = 开始时间
    sqlite3_bind_text(stmt, 3, end_time,   -1, SQLITE_STATIC);         // 第3个? = 结束时间
    sqlite3_bind_text(stmt, 4, file_path,  -1, SQLITE_STATIC);         // 第4个? = 文件路径
    sqlite3_bind_text(stmt, 5, record_type, -1, SQLITE_STATIC);        // 第5个? = 是否有人登记

    // 3. 执行
    int rc = sqlite3_step(stmt);

    // 4. 释放
    sqlite3_finalize(stmt);

    // 5. 检查结果
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[DB] video_record 写入失败\n");
        return -1;
    }
    printf("[DB] 录像: stream%d %s ~ %s\n", stream_id, start_time, end_time);
    return 0;
}

// ================================================================
// ★ 插入人脸映射：feature_id ↔ 人名 ↔ 房间号
//    如果 feature_id 已存在则覆盖（INSERT OR REPLACE）
//    用于：新录入人脸时写入，或更新已录入人员的信息
// 参数：
//   db：数据库句柄
//   feature_id：InspireFace SDK 返回的数字ID
//   person_name：人名
//   room_id：所属房间号
// 返回：0成功，-1失败
// ================================================================
int db_insert_face_mapping(void* db, int feature_id, const char* person_name, int room_id)
{
    // INSERT OR REPLACE：如果 feature_id 已存在，先删旧记录再插新记录（即改名/改房间）
    const char* sql = "INSERT OR REPLACE INTO face_mapping (feature_id, person_name, room_id) VALUES (?, ?, ?)";

    sqlite3_stmt* stmt;

    // 1. 准备
    sqlite3_prepare_v2((sqlite3*)db, sql, -1, &stmt, NULL);

    // 2. 绑定参数
    sqlite3_bind_int( stmt, 1, feature_id);                              // 第1个? = 特征ID
    sqlite3_bind_text(stmt, 2, person_name, -1, SQLITE_STATIC);          // 第2个? = 人名
    sqlite3_bind_int( stmt, 3, room_id);                                 // 第3个? = 房间号

    // 3. 执行
    int rc = sqlite3_step(stmt);

    // 4. 释放
    sqlite3_finalize(stmt);

    // 5. 检查结果
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[DB] face_mapping 写入失败\n");
        return -1;
    }
    printf("[DB] 人脸映射: feature_id=%d -> %s (房间%d)\n", feature_id, person_name, room_id);
    return 0;
}

// ================================================================
// ★ 删除人脸映射
//    用于：QT手动删除某个人的人脸信息
// 参数：
//   db：数据库句柄
//   feature_id：要删除的特征ID
// 返回：0成功，-1失败
// ================================================================
int db_delete_face_mapping(void* db, int feature_id)
{
    const char* sql = "DELETE FROM face_mapping WHERE feature_id = ?";

    sqlite3_stmt* stmt;

    // 1. 准备
    sqlite3_prepare_v2((sqlite3*)db, sql, -1, &stmt, NULL);

    // 2. 绑定要删的feature_id
    sqlite3_bind_int(stmt, 1, feature_id);

    // 3. 执行
    int rc = sqlite3_step(stmt);

    // 4. 释放
    sqlite3_finalize(stmt);

    // 5. 检查结果
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[DB] face_mapping 删除失败\n");
        return -1;
    }
    printf("[DB] 人脸映射删除: feature_id=%d\n", feature_id);
    return 0;
}

// ================================================================
// ★ 查询人名：feature_id → person_name
//    人脸识别后，拿SDK返回的feature_id查出人名
// 参数：
//   db：数据库句柄
//   feature_id：InspireFace 返回的特征ID
//   out_name：[OUT] 查到的名字写到这里
//   max_len：out_name 缓冲区大小
// 返回：0查到，-1未录入
// ================================================================
int db_query_person_name(void* db, int feature_id, char* out_name, int max_len)
{
    const char* sql = "SELECT person_name FROM face_mapping WHERE feature_id = ?";

    sqlite3_stmt* stmt;

    // 1. 准备
    sqlite3_prepare_v2((sqlite3*)db, sql, -1, &stmt, NULL);

    // 2. 绑定查询条件
    sqlite3_bind_int(stmt, 1, feature_id);

    // 3. 执行查询
    int rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {                                          // 查到一行数据
        const char* name = (const char*)sqlite3_column_text(stmt, 0); // 读第0列：person_name
        strncpy(out_name, name ? name : "未知", max_len - 1);       // 拷贝到输出缓冲区
        out_name[max_len - 1] = '\0';                                // 确保字符串结尾
        sqlite3_finalize(stmt);                                      // 释放语句
        return 0;
    }

    // -------- 查不到 --------
    strncpy(out_name, "未录入", max_len - 1);
    out_name[max_len - 1] = '\0';
    sqlite3_finalize(stmt);
    return -1;
}

// ================================================================
// ★ 查询人名和房间号：feature_id → person_name + room_id
//    签到/签退时由人脸进程调用，把人员的房间号一并写入 attendance
// 返回：0查到，-1未录入
// ================================================================
int db_query_person_name_ex(void* db, int feature_id, char* out_name, int max_len, int* out_room_id)
{
    const char* sql = "SELECT person_name, room_id FROM face_mapping WHERE feature_id = ?";

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2((sqlite3*)db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, feature_id);

    int rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        const char* name = (const char*)sqlite3_column_text(stmt, 0);
        strncpy(out_name, name ? name : "未知", max_len - 1);
        out_name[max_len - 1] = '\0';
        if (out_room_id) *out_room_id = sqlite3_column_int(stmt, 1);
        sqlite3_finalize(stmt);
        return 0;
    }

    strncpy(out_name, "未录入", max_len - 1);
    out_name[max_len - 1] = '\0';
    if (out_room_id) *out_room_id = 0;
    sqlite3_finalize(stmt);
    return -1;
}

/**
 * 根据路径删除录像记录
 */
int db_delete_video_by_path(void* db, const char* file_path) {
    sqlite3* s3 = (sqlite3*)db;
    const char* sql = "DELETE FROM video_records WHERE file_path = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(s3, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, file_path, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return 0;
}

// ================================================================
// ★ 智能插座配置表（device_plugs）CRUD 接口
// ================================================================

// 插入或更新插座配置（按 room_id+device_id 唯一）
int db_insert_device_plug(void* db, int room_id, int device_id,
                          const char* name, const char* ip, const char* token, int enabled)
{
    const char* sql =
        "INSERT OR REPLACE INTO device_plugs (room_id, device_id, name, ip, token, enabled) "
        "VALUES (?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int( stmt, 1, room_id);
    sqlite3_bind_int( stmt, 2, device_id);
    sqlite3_bind_text(stmt, 3, name,  -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, ip,    -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, token, -1, SQLITE_STATIC);
    sqlite3_bind_int( stmt, 6, enabled);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

// 删除插座配置（按 room_id + device_id）
int db_delete_device_plug(void* db, int room_id, int device_id)
{
    const char* sql = "DELETE FROM device_plugs WHERE room_id = ? AND device_id = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, room_id);
    sqlite3_bind_int(stmt, 2, device_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

// 查询指定房间的所有插座配置
// 返回记录数，结果写入 out 数组（最多 max_count 条），实际写入数返回
int db_query_plugs_by_room(void* db, int room_id,
                           DevicePlugRow* out, int max_count)
{
    const char* sql = "SELECT id, room_id, device_id, name, ip, token, enabled "
                      "FROM device_plugs WHERE room_id = ? ORDER BY device_id";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, room_id);

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_count) {
        out[count].id        = sqlite3_column_int(stmt, 0);
        out[count].room_id   = sqlite3_column_int(stmt, 1);
        out[count].device_id = sqlite3_column_int(stmt, 2);
        const char* name  = (const char*)sqlite3_column_text(stmt, 3);
        const char* ip    = (const char*)sqlite3_column_text(stmt, 4);
        const char* token = (const char*)sqlite3_column_text(stmt, 5);
        strncpy(out[count].name,  name  ? name  : "", sizeof(out[count].name) - 1);
        strncpy(out[count].ip,    ip    ? ip    : "", sizeof(out[count].ip) - 1);
        strncpy(out[count].token, token ? token : "", sizeof(out[count].token) - 1);
        out[count].enabled  = sqlite3_column_int(stmt, 6);
        count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

// 查询指定房间+设备号的插座配置
int db_query_plug(void* db, int room_id, int device_id, DevicePlugRow* out)
{
    const char* sql = "SELECT id, room_id, device_id, name, ip, token, enabled "
                      "FROM device_plugs WHERE room_id = ? AND device_id = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, room_id);
    sqlite3_bind_int(stmt, 2, device_id);

    int ret = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out->id        = sqlite3_column_int(stmt, 0);
        out->room_id   = sqlite3_column_int(stmt, 1);
        out->device_id = sqlite3_column_int(stmt, 2);
        const char* name  = (const char*)sqlite3_column_text(stmt, 3);
        const char* ip    = (const char*)sqlite3_column_text(stmt, 4);
        const char* token = (const char*)sqlite3_column_text(stmt, 5);
        strncpy(out->name,  name  ? name  : "", sizeof(out->name) - 1);
        strncpy(out->ip,    ip    ? ip    : "", sizeof(out->ip) - 1);
        strncpy(out->token, token ? token : "", sizeof(out->token) - 1);
        out->enabled  = sqlite3_column_int(stmt, 6);
        ret = 0;
    }
    sqlite3_finalize(stmt);
    return ret;
}

// ================================================================
// ★ ESP32 设备表（room_esp32）CRUD 接口
// ================================================================

// 插入或更新 ESP32 配置（按 room_id 唯一）
int db_insert_room_esp32(void* db, int room_id,
                         const char* name, const char* esp32_ip, const char* rtsp_url)
{
    const char* sql =
        "INSERT OR REPLACE INTO room_esp32 (room_id, name, esp32_ip, rtsp_url) "
        "VALUES (?, ?, ?, ?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int( stmt, 1, room_id);
    sqlite3_bind_text(stmt, 2, name,      -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, esp32_ip,  -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, rtsp_url,  -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

// 删除 ESP32 配置（按 room_id）
int db_delete_room_esp32(void* db, int room_id)
{
    const char* sql = "DELETE FROM room_esp32 WHERE room_id = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, room_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

// 查询指定房间的 ESP32 配置
int db_query_esp32_by_room(void* db, int room_id, RoomEsp32Row* out)
{
    const char* sql = "SELECT id, room_id, name, esp32_ip, rtsp_url, online, last_seen "
                      "FROM room_esp32 WHERE room_id = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, room_id);

    int ret = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out->id        = sqlite3_column_int(stmt, 0);
        out->room_id   = sqlite3_column_int(stmt, 1);
        const char* name    = (const char*)sqlite3_column_text(stmt, 2);
        const char* ip      = (const char*)sqlite3_column_text(stmt, 3);
        const char* rtsp    = (const char*)sqlite3_column_text(stmt, 4);
        strncpy(out->name,     name ? name : "", sizeof(out->name) - 1);
        strncpy(out->esp32_ip, ip   ? ip   : "", sizeof(out->esp32_ip) - 1);
        strncpy(out->rtsp_url, rtsp ? rtsp : "", sizeof(out->rtsp_url) - 1);
        out->online    = sqlite3_column_int(stmt, 5);
        out->last_seen = sqlite3_column_int64(stmt, 6);
        ret = 0;
    }
    sqlite3_finalize(stmt);
    return ret;
}

// 查询所有 ESP32 配置
int db_query_all_esp32(void* db, RoomEsp32Row* out, int max_count)
{
    const char* sql = "SELECT id, room_id, name, esp32_ip, rtsp_url, online, last_seen "
                      "FROM room_esp32 ORDER BY room_id";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_count) {
        out[count].id        = sqlite3_column_int(stmt, 0);
        out[count].room_id   = sqlite3_column_int(stmt, 1);
        const char* name    = (const char*)sqlite3_column_text(stmt, 2);
        const char* ip      = (const char*)sqlite3_column_text(stmt, 3);
        const char* rtsp    = (const char*)sqlite3_column_text(stmt, 4);
        strncpy(out[count].name,     name ? name : "", sizeof(out[count].name) - 1);
        strncpy(out[count].esp32_ip, ip   ? ip   : "", sizeof(out[count].esp32_ip) - 1);
        strncpy(out[count].rtsp_url, rtsp ? rtsp : "", sizeof(out[count].rtsp_url) - 1);
        out[count].online    = sqlite3_column_int(stmt, 5);
        out[count].last_seen = sqlite3_column_int64(stmt, 6);
        count++;
    }
    sqlite3_finalize(stmt);
    return count;
}

// 更新 ESP32 在线状态 + 最后心跳时间
int db_update_esp32_online(void* db, int room_id, int online, int64_t last_seen)
{
    const char* sql = "UPDATE room_esp32 SET online = ?, last_seen = ? WHERE room_id = ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2((sqlite3*)db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(stmt, 1, online);
    sqlite3_bind_int64(stmt, 2, last_seen);
    sqlite3_bind_int(stmt, 3, room_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}