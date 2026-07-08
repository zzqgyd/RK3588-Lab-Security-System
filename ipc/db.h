#ifndef DB_H
#define DB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * 数据库初始化
 * 内部执行：打开/创建、WAL模式、建表
 * 多进程安全：每个进程独立调用 db_open
 * ================================================================ */
void* db_open(const char* db_path);
void  db_close(void* db);

/*
 * 表结构：
 *
 * attendance（签到/签退）—— 人脸进程写入
 *   id          INTEGER PRIMARY KEY AUTOINCREMENT
 *   person_name TEXT NOT NULL
 *   type        TEXT NOT NULL              -- '签到' or '签退'
 *   room_id     INTEGER DEFAULT 0          -- 房间号（与 face_mapping 对齐）
 *   time        DATETIME DEFAULT CURRENT_TIMESTAMP
 *
 * device_usage（设备使用登记）—— 人脸进程写入
 *   id               INTEGER PRIMARY KEY AUTOINCREMENT
 *   person_name      TEXT NOT NULL
 *   room_id          INTEGER NOT NULL
 *   device_id        INTEGER NOT NULL
 *   duration_minutes INTEGER NOT NULL
 *   time             DATETIME DEFAULT CURRENT_TIMESTAMP
 *
 * video_records（录像记录）—— 主进程写入
 *   id         INTEGER PRIMARY KEY AUTOINCREMENT
 *   stream_id  INTEGER NOT NULL
 *   start_time DATETIME NOT NULL
 *   end_time   DATETIME NOT NULL
 *   file_path  TEXT NOT NULL
 *
 * face_mapping（人脸特征ID → 人名映射）—— 人脸进程写入
 *   feature_id  INTEGER PRIMARY KEY
 *   person_name TEXT NOT NULL
 *   room_id     INTEGER DEFAULT 0          -- 所属房间号
 */

/* ========== 考勤 ========== */
int db_insert_attendance(void* db, const char* person_name, const char* type, int room_id);

/* ========== 设备使用登记 ========== */
int db_insert_device_usage(void* db, const char* person_name,
                           int room_id, int device_id, int duration_minutes);

/* ========== 录像记录 ========== */
int db_insert_video_record(void* db, int stream_id,
                           const char* start_time, const char* end_time,
                           const char* file_path, const char* record_type);

/* ========== 人脸映射表 ========== */

/*
 * 录入人脸：保存 feature_id、人名、房间号的对应关系
 * 如果 feature_id 已存在，覆盖旧记录
 */
int db_insert_face_mapping(void* db, int feature_id, const char* person_name, int room_id);

/*
 * 删除人脸：删除 feature_id 对应的映射记录
 */
int db_delete_face_mapping(void* db, int feature_id);

// 根据路径删除录像记录
int db_delete_video_by_path(void* db, const char* file_path);


/*
 * 根据 feature_id 查询人名
 * 查到时写入 out_name 返回 0，查不到返回 -1
 */
int db_query_person_name(void* db, int feature_id, char* out_name, int max_len);

/*
 * 根据 feature_id 查询人名和房间号
 * 查到时写入 out_name、*out_room_id 返回 0，查不到返回 -1
 */
int db_query_person_name_ex(void* db, int feature_id, char* out_name, int max_len, int* out_room_id);

/* ================================================================
 * 智能插座配置表（device_plugs）
 * ================================================================ */

#define DB_MAX_PLUG_NAME  64
#define DB_MAX_PLUG_IP    64
#define DB_MAX_PLUG_TOKEN 33

typedef struct {
    int  id;
    int  room_id;
    int  device_id;
    char name[DB_MAX_PLUG_NAME];
    char ip[DB_MAX_PLUG_IP];
    char token[DB_MAX_PLUG_TOKEN];
    int  enabled;
} DevicePlugRow;

/* 插入或更新插座配置（按 room_id+device_id 唯一）*/
int db_insert_device_plug(void* db, int room_id, int device_id,
                          const char* name, const char* ip, const char* token, int enabled);

/* 删除插座配置（按 room_id + device_id）*/
int db_delete_device_plug(void* db, int room_id, int device_id);

/* 查询指定房间的所有插座配置，返回记录数（>=0），失败返回 -1 */
int db_query_plugs_by_room(void* db, int room_id,
                           DevicePlugRow* out, int max_count);

/* 查询指定房间+设备号的插座配置，0=查到，-1=未查到 */
int db_query_plug(void* db, int room_id, int device_id, DevicePlugRow* out);

/* ================================================================
 * ESP32 设备表（room_esp32）
 * ----------------------------------------------------------------
 * 每个房间绑定一个 ESP32（room_id 唯一），用于被动人脸识别推流
 * ================================================================ */

#define DB_MAX_ESP32_NAME    64
#define DB_MAX_ESP32_IP      64
#define DB_MAX_ESP32_RTSP    128

typedef struct {
    int  id;
    int  room_id;                    /* 房间号（唯一，一间一个 ESP32） */
    char name[DB_MAX_ESP32_NAME];    /* 备注名 */
    char esp32_ip[DB_MAX_ESP32_IP];  /* ESP32 IP（用于 RTSP 拉流） */
    char rtsp_url[DB_MAX_ESP32_RTSP];/* RTSP 流地址 */
    int  online;                     /* 0=离线, 1=在线（心跳维护） */
    int64_t last_seen;               /* 最后心跳时间戳（秒） */
} RoomEsp32Row;

/* 插入或更新 ESP32 配置（按 room_id 唯一）*/
int db_insert_room_esp32(void* db, int room_id,
                         const char* name, const char* esp32_ip, const char* rtsp_url);

/* 删除 ESP32 配置（按 room_id）*/
int db_delete_room_esp32(void* db, int room_id);

/* 查询指定房间的 ESP32 配置，0=查到，-1=未查到 */
int db_query_esp32_by_room(void* db, int room_id, RoomEsp32Row* out);

/* 查询所有 ESP32 配置，返回记录数（>=0），失败返回 -1 */
int db_query_all_esp32(void* db, RoomEsp32Row* out, int max_count);

/* 更新 ESP32 在线状态 + 最后心跳时间 */
int db_update_esp32_online(void* db, int room_id, int online, int64_t last_seen);

#ifdef __cplusplus
}
#endif

#endif // DB_H
