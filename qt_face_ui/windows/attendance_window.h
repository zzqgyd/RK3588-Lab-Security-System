#ifndef ATTENDANCE_WINDOW_H
#define ATTENDANCE_WINDOW_H

#include <QWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>

/**
 * @brief 考勤记录查询窗口
 * 
 * 功能：
 * - 显示 attendance 表中所有签到/签退记录
 * - 只读查询
 */
class AttendanceWindow : public QWidget
{
    Q_OBJECT

public:
    explicit AttendanceWindow(QWidget *parent = nullptr);
    ~AttendanceWindow();

signals:
    void backToHome();  // 返回主页信号

private slots:
    void onRefresh();   // 刷新列表
    void onBack();      // 返回主页

private:
    void setupUI();     // 创建界面
    void loadData();    // 加载数据

    QTableWidget* m_table;
    QPushButton* m_refreshBtn;
    QPushButton* m_backBtn;
};

#endif