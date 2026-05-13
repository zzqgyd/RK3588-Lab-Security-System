#include "db_manager.h"
#include <QDebug>
#include "app/constants.h"

DbManager& DbManager::instance()
{
    static DbManager instance;
    return instance;
}

bool DbManager::init()
{
    if (m_db.isOpen()) {
        return true;
    }
    
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(DB_PATH);
    
    if (!m_db.open()) {
        qWarning() << "[DbManager] Failed to open database:" << m_db.lastError().text();
        return false;
    }
    
    qDebug() << "[DbManager] Database opened (read-only):" << DB_PATH;
    return true;
}

void DbManager::close()
{
    if (m_db.isOpen()) {
        QString connectionName = m_db.connectionName();
        m_db.close();
        QSqlDatabase::removeDatabase(connectionName);
    }
}

// ================================================================
// 人脸库查询
// ================================================================

QVector<FaceRecord> DbManager::getAllFaces()
{
    QVector<FaceRecord> records;
    
    if (!m_db.isOpen()) {
        qWarning() << "[DbManager] Database not open";
        return records;
    }
    
    QSqlQuery query;
    if (!query.exec("SELECT feature_id, person_name, room_id FROM face_mapping ORDER BY feature_id")) {
        qWarning() << "[DbManager] Query failed:" << query.lastError().text();
        return records;
    }

    while (query.next()) {
        FaceRecord record;
        record.featureId = query.value(0).toInt();
        record.personName = query.value(1).toString();
        record.roomId = query.value(2).toInt();
        records.append(record);
    }

    return records;
}

// ================================================================
// 考勤记录查询
// ================================================================

QVector<AttendanceRecord> DbManager::getAllAttendance()
{
    QVector<AttendanceRecord> records;
    
    if (!m_db.isOpen()) {
        qWarning() << "[DbManager] Database not open";
        return records;
    }
    
    QSqlQuery query;
    if (!query.exec("SELECT id, person_name, type, room_id, time FROM attendance ORDER BY time DESC")) {
        qWarning() << "[DbManager] Query failed:" << query.lastError().text();
        return records;
    }

    while (query.next()) {
        AttendanceRecord record;
        record.id = query.value(0).toInt();
        record.personName = query.value(1).toString();
        record.type = query.value(2).toString();
        record.roomId = query.value(3).toInt();
        record.time = query.value(4).toString();
        records.append(record);
    }
    
    qDebug() << "[DbManager] Loaded" << records.size() << "attendance records";
    return records;
}

// ================================================================
// 设备使用记录查询
// ================================================================

QVector<DeviceUsageRecord> DbManager::getAllDeviceUsage()
{
    QVector<DeviceUsageRecord> records;
    
    if (!m_db.isOpen()) {
        qWarning() << "[DbManager] Database not open";
        return records;
    }
    
    QSqlQuery query;
    if (!query.exec("SELECT id, person_name, room_id, device_id, duration_minutes, time FROM device_usage ORDER BY time DESC")) {
        qWarning() << "[DbManager] Query failed:" << query.lastError().text();
        return records;
    }
    
    while (query.next()) {
        DeviceUsageRecord record;
        record.id = query.value(0).toInt();
        record.personName = query.value(1).toString();
        record.roomId = query.value(2).toInt();
        record.deviceId = query.value(3).toInt();
        record.durationMinutes = query.value(4).toInt();
        record.time = query.value(5).toString();
        records.append(record);
    }
    
    qDebug() << "[DbManager] Loaded" << records.size() << "device usage records";
    return records;
}

// ================================================================
// 录像记录查询
// ================================================================

QVector<VideoRecord> DbManager::getAllVideoRecords()
{
    QVector<VideoRecord> records;
    
    if (!m_db.isOpen()) {
        qWarning() << "[DbManager] Database not open";
        return records;
    }
    
    QSqlQuery query;
    if (!query.exec("SELECT id, stream_id, start_time, end_time, file_path, record_type FROM video_records ORDER BY start_time DESC")) {
        qWarning() << "[DbManager] Query failed:" << query.lastError().text();
        return records;
    }
    
    while (query.next()) {
        VideoRecord record;
        record.id = query.value(0).toInt();
        record.streamId = query.value(1).toInt();
        record.startTime = query.value(2).toString();
        record.endTime = query.value(3).toString();
        record.filePath = query.value(4).toString();
        record.recordType = query.value(5).toString();
        records.append(record);
    }
    
    qDebug() << "[DbManager] Loaded" << records.size() << "video records";
    return records;
}

// ================================================================
// 统计专用查询
// ================================================================

int DbManager::countRoomMembers(int roomId)
{
    if (!m_db.isOpen()) {
        qWarning() << "[DbManager] Database not open";
        return 0;
    }

    QSqlQuery query;
    if (roomId < 0) {
        query.exec("SELECT COUNT(*) FROM face_mapping");
    } else {
        query.prepare("SELECT COUNT(*) FROM face_mapping WHERE room_id = :rid");
        query.bindValue(":rid", roomId);
    }
    if (query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

QVector<AttendanceRecord> DbManager::queryAttendance(const QDate& start, const QDate& end, int roomId)
{
    QVector<AttendanceRecord> records;

    if (!m_db.isOpen()) {
        qWarning() << "[DbManager] Database not open";
        return records;
    }

    QString sql = "SELECT id, person_name, type, room_id, time FROM attendance "
                  "WHERE date(time) BETWEEN :start AND :end";
    if (roomId >= 0) {
        sql += " AND room_id = :rid";
    }
    sql += " ORDER BY time ASC";

    QSqlQuery query;
    query.prepare(sql);
    query.bindValue(":start", start.toString("yyyy-MM-dd"));
    query.bindValue(":end", end.toString("yyyy-MM-dd"));
    if (roomId >= 0) {
        query.bindValue(":rid", roomId);
    }

    if (!query.exec()) {
        qWarning() << "[DbManager] queryAttendance failed:" << query.lastError().text();
        return records;
    }

    while (query.next()) {
        AttendanceRecord r;
        r.id = query.value(0).toInt();
        r.personName = query.value(1).toString();
        r.type = query.value(2).toString();
        r.roomId = query.value(3).toInt();
        r.time = query.value(4).toString();
        records.append(r);
    }
    return records;
}

QMap<int, int> DbManager::queryDeviceUsageSummary(const QDate& start, const QDate& end, int roomId)
{
    QMap<int, int> result;

    if (!m_db.isOpen()) {
        qWarning() << "[DbManager] Database not open";
        return result;
    }

    QString sql = "SELECT device_id, SUM(duration_minutes) FROM device_usage "
                  "WHERE date(time) BETWEEN :start AND :end";
    if (roomId >= 0) {
        sql += " AND room_id = :rid";
    }
    sql += " GROUP BY device_id ORDER BY device_id";

    QSqlQuery query;
    query.prepare(sql);
    query.bindValue(":start", start.toString("yyyy-MM-dd"));
    query.bindValue(":end", end.toString("yyyy-MM-dd"));
    if (roomId >= 0) {
        query.bindValue(":rid", roomId);
    }

    if (!query.exec()) {
        qWarning() << "[DbManager] queryDeviceUsageSummary failed:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        int deviceId = query.value(0).toInt();
        int minutes = query.value(1).toInt();
        result[deviceId] = minutes;
    }
    return result;
}