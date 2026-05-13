#ifndef SYSTEM_DASHBOARD_H
#define SYSTEM_DASHBOARD_H

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QVector>

/**
 * @brief 系统状态仪表盘
 * 
 * 显示内容：
 * 1. CPU 8个核心使用率
 * 2. 内存使用情况
 * 3. NPU 3个核心使用率 (需 sudo)
 * 4. RGA 3个核心使用率 (需 sudo)
 * 5. DDR 带宽
 * 6. GPU 使用率
 * 7. VPU 会话
 * 8. CPU 温度
 */
class SystemDashboard : public QWidget
{
    Q_OBJECT

public:
    explicit SystemDashboard(QWidget *parent = nullptr);
    ~SystemDashboard();

signals:
    void backToHome();

private slots:
    void updateStats();

private:
    // ============================================================
    // 数据读取函数
    // ============================================================
    
    QString readFile(const QString& path);              // 普通权限读取
    QString readFileWithSudo(const QString& path);      // sudo 权限读取
    
    // ============================================================
    // 各硬件更新函数
    // ============================================================
    
    void updateCpu();       // 1. CPU 8个核心
    void updateMemory();    // 2. 内存使用情况
    void updateNpu();       // 3. NPU 3个核心
    void updateRga();       // 4. RGA 3个核心
    void updateDdr();       // 5. DDR 带宽
    void updateGpu();       // 6. GPU 使用率
    void updateVpu();       // 7. VPU 会话
    void updateTemp();      // 8. CPU 温度

    // ============================================================
    // UI 组件
    // ============================================================
    
    // 1. CPU - 8个核心
    QVector<QProgressBar*> m_cpuBars;
    QVector<QLabel*> m_cpuLabels;

    // 2. 内存
    QProgressBar* m_memoryBar;
    QLabel* m_memoryLabel;
    QLabel* m_memoryDetailLabel;

    // 3. NPU - 3个核心
    QVector<QProgressBar*> m_npuBars;
    QVector<QLabel*> m_npuLabels;

    // 4. RGA - 3个核心
    QVector<QProgressBar*> m_rgaBars;
    QVector<QLabel*> m_rgaLabels;

    // 5. DDR
    QProgressBar* m_ddrBar;
    QLabel* m_ddrFreqLabel;
    QLabel* m_ddrLoadLabel;

    // 6. GPU
    QProgressBar* m_gpuBar;
    QLabel* m_gpuFreqLabel;
    QLabel* m_gpuLoadLabel;

    // 7. VPU
    QLabel* m_vpuDecLabel;
    QLabel* m_vpuEncLabel;

    // 8. 温度
    QLabel* m_tempLabel;

    QTimer* m_timer;

    // ============================================================
    // CPU 差值计算缓存
    // ============================================================
    QVector<long long> m_prevUser;
    QVector<long long> m_prevNice;
    QVector<long long> m_prevSystem;
    QVector<long long> m_prevIdle;
    bool m_firstCpuRead;
};

#endif