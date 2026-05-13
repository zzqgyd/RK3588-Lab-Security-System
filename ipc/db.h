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

#ifdef __cplusplus
}
#endif

#endif // DB_H
