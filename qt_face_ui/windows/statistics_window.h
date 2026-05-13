#ifndef STATISTICS_WINDOW_H
#define STATISTICS_WINDOW_H

#include <QWidget>
#include <QTabWidget>
#include <QDateEdit>
#include <QPushButton>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QShowEvent>
#include <QtCharts/QChartView>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>
#include <QtCharts/QValueAxis>
#include <QtCharts/QChart>

QT_CHARTS_USE_NAMESPACE

/**
 * @brief 统计图表窗口
 * 
 * 功能：
 * 1. 考勤统计：签到/签退人数柱状图，支持按年/月/周分组，支持按房间号筛选
 * 2. 设备使用统计：各设备使用时长饼图，支持按房间号筛选
 * 3. 支持日期范围筛选
 */
class StatisticsWindow : public QWidget
{
    Q_OBJECT

public:
    explicit StatisticsWindow(QWidget *parent = nullptr);
    ~StatisticsWindow();

signals:
    void backToHome();  // 返回主页信号

protected:
    void showEvent(QShowEvent *event) override;  // 窗口显示时自动刷新

private slots:
    void onRefresh();               // 刷新数据
    void onDateRangeChanged();      // 日期范围改变
    void onGroupByChanged(int index);   // 分组方式改变
    void onRoomFilterChanged(int index); // 房间号筛选改变
    void onTabChanged(int index);       // 选项卡切换

private:
    void setupUI();                 // 创建界面
    void loadAttendanceStats();     // 加载考勤统计
    void loadDeviceUsageStats();    // 加载设备使用统计
    void updateAttendanceChart();   // 更新考勤柱状图
    void updateDeviceUsageChart();  // 更新设备使用饼图
    void updateRoomTotalLabel(int roomFilter);  // 更新侧面房间人数
    QString getGroupKey(const QDate& date, int groupBy);  // 分组键值

    // UI 控件
    QTabWidget* m_tabWidget;

    // 考勤统计页面
    QWidget* m_attendancePage;
    QDateEdit* m_startDateEdit;
    QDateEdit* m_endDateEdit;
    QComboBox* m_groupByCombo;
    QComboBox* m_roomFilterCombo;
    QChartView* m_attendanceChartView;
    QLabel* m_roomTotalLabel;       // 侧面显示房间人数
    QPushButton* m_refreshBtn;
    QPushButton* m_backBtn;

    // 设备使用统计页面
    QWidget* m_devicePage;
    QDateEdit* m_deviceStartDate;
    QDateEdit* m_deviceEndDate;
    QComboBox* m_deviceRoomFilterCombo;
    QChartView* m_deviceChartView;

    // 数据缓存：考勤三类（按时间分组）
    // 签到且签退 / 签到没签退 / 没签到
    QMap<QString, int> m_signInAndOutData;
    QMap<QString, int> m_signInOnlyData;
    QMap<QString, int> m_notSignInData;
    int m_roomTotal;                // 当前筛选房间的人员总数

    QMap<int, int> m_deviceData;

    // 饼图颜色
    QList<QColor> m_colors;
};

#endif