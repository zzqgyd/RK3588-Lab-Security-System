#ifndef COMMUNICATION_LAYER_HPP
#define COMMUNICATION_LAYER_HPP

#include <cstring>
#include <stdio.h>
#include <sqlite3.h>
#include "ipc/db.h"

/**
 * @brief 通信层（简化版）
 * 
 * 职责：
 *   1. 数据库操作：考勤、设备使用、人脸映射
 *   2. 不再负责 IPC 通信（改为 Socket 方式，独立线程处理）
 */
class CommunicationLayer {
public:
    CommunicationLayer(void* db)
        : db_(db) {}

    // ============================================================
    // 数据库操作
    // ============================================================

    // 签到/签退：写入 attendance，room_id 由人员所属房间决定
    int db_write_attendance(const char* name, const char* type, int room_id) {
        return db_insert_attendance(db_, name, type, room_id);
    }

    void db_write_device_usage(const char* name, int room, int dev, int dur) {
        db_insert_device_usage(db_, name, room, dev, dur);
    }

    bool db_check_name_exists(const char* name, int& existing_id) {
        sqlite3_stmt* stmt = NULL;
        const char* sql = "SELECT feature_id FROM face_mapping WHERE person_name = ?";
        sqlite3_prepare_v2((sqlite3*)db_, sql, -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
        if (exists) existing_id = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return exists;
    }

    // 录入：写入 face_mapping（含 room_id）
    void db_insert_mapping(int fid, const char* name, int room_id) {
        db_insert_face_mapping(db_, fid, name, room_id);
    }

    void db_delete_mapping(int fid) {
        db_delete_face_mapping(db_, fid);
    }

    int db_query_name(int fid, char* out, int max_len) {
        return db_query_person_name(db_, fid, out, max_len);
    }

    // 查询人名 + 房间号（签到/签退时用，把房间号写入 attendance）
    int db_query_name_ex(int fid, char* out, int max_len, int* out_room_id) {
        return db_query_person_name_ex(db_, fid, out, max_len, out_room_id);
    }

private:
    void* db_;
};

#endif // COMMUNICATION_LAYER_HPP