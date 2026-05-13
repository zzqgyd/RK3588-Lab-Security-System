#ifndef DEVICE_USAGE_WINDOW_H
#define DEVICE_USAGE_WINDOW_H

#include <QWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>

/**
 * @brief 设备使用记录查询窗口
 * 
 * 功能：
 * - 显示 device_usage 表中所有设备使用记录
 * - 只读查询
 */
class DeviceUsageWindow : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceUsageWindow(QWidget *parent = nullptr);
    ~DeviceUsageWindow();

signals:
    void backToHome();

private slots:
    void onRefresh();
    void onBack();

private:
    void setupUI();
    void loadData();

    QTableWidget* m_table;
    QPushButton* m_refreshBtn;
    QPushButton* m_backBtn;
};

#endif