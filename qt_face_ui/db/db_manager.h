#ifndef DB_MANAGER_H
#define DB_MANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVector>
#include <QStringList>
#include <QDate>
#include <QMap>

/**
 * @brief 人脸记录结构体
 */
struct FaceRecord {
    int featureId;
    QString personName;
    int roomId;        // 所属房间号
};

/**
 * @brief 考勤记录结构体
 */
struct AttendanceRecord {
    int id;
    QString personName;
    QString type;      // "签到" 或 "签退"
    int roomId;        // 所属房间号
    QString time;
};

/**
 * @brief 设备使用记录结构体
 */
struct DeviceUsageRecord {
    int id;
    QString personName;
    int roomId;
    int deviceId;
    int durationMinutes;
    QString time;
};

/**
 * @brief 录像记录结构体
 */
struct VideoRecord {
    int id;
    int streamId;
    QString startTime;
    QString endTime;
    QString filePath;
    QString recordType;  // "registered" 或 "unregistered"
};

/**
 * @brief 数据库管理器（单例模式）
 * 
 * 职责：
 * - 管理数据库连接（只读）
 * - 提供统一的查询接口给 QT 界面
 * - 所有写入操作由人脸进程负责，QT 只读
 */
class DbManager : public QObject
{
    Q_OBJECT

public:
    static DbManager& instance();
    
    /**
     * @brief 初始化数据库连接（只读模式）
     * @return true 成功, false 失败
     */
    bool init();
    
    /**
     * @brief 关闭数据库连接
     */
    void close();
    
    /**
     * @brief 检查数据库是否已打开
     */
    bool isOpen() const { return m_db.isOpen(); }
    
    // ================================================================
    // 人脸库查询（只读）
    // ================================================================
    
    /**
     * @brief 获取所有人脸记录
     * @return 人脸记录列表
     */
    QVector<FaceRecord> getAllFaces();
    
    // ================================================================
    // 考勤记录查询（只读）
    // ================================================================
    
    /**
     * @brief 获取所有考勤记录
     * @return 考勤记录列表（按时间倒序）
     */
    QVector<AttendanceRecord> getAllAttendance();
    
    // ================================================================
    // 设备使用记录查询（只读）
    // ================================================================
    
    /**
     * @brief 获取所有设备使用记录
     * @return 设备使用记录列表（按时间倒序）
     */
    QVector<DeviceUsageRecord> getAllDeviceUsage();
    
    // ================================================================
    // 录像记录查询（只读）
    // ================================================================
    
    /**
     * @brief 获取所有录像记录
     * @return 录像记录列表（按开始时间倒序）
     */
    QVector<VideoRecord> getAllVideoRecords();

    // ================================================================
    // 统计专用查询（供 StatisticsWindow 使用，UI 层不再直接写 SQL）
    // roomId 约定：-1 表示全部房间，否则按 room_id 筛选
    // ================================================================

    /**
     * @brief 统计已录入人员数量
     * @param roomId 房间号，-1 表示全部
     */
    int countRoomMembers(int roomId = -1);

    /**
     * @brief 查询指定日期范围、房间的考勤记录
     * @return 考勤记录列表（按时间正序，便于分组统计）
     */
    QVector<AttendanceRecord> queryAttendance(const QDate& start, const QDate& end, int roomId = -1);

    /**
     * @brief 查询设备使用时长汇总
     * @return QMap: device_id → 累计分钟数
     */
    QMap<int, int> queryDeviceUsageSummary(const QDate& start, const QDate& end, int roomId = -1);

private:
    DbManager() = default;
    ~DbManager() = default;
    DbManager(const DbManager&) = delete;
    DbManager& operator=(const DbManager&) = delete;
    
    QSqlDatabase m_db;
};

#endif