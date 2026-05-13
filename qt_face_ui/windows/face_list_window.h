#ifndef FACE_LIST_WINDOW_H
#define FACE_LIST_WINDOW_H

#include <QWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>

/**
 * @brief 人脸库查询窗口
 * 
 * 功能：
 * - 显示 face_mapping 表中所有已录入的人脸
 * - 每条记录带删除按钮
 */
class FaceListWindow : public QWidget
{
    Q_OBJECT

public:
    explicit FaceListWindow(QWidget *parent = nullptr);
    ~FaceListWindow();

signals:
    void backToHome();                      // 返回主页
    void faceDeleteRequested(int featureId, const QString& name);  // 删除人脸

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