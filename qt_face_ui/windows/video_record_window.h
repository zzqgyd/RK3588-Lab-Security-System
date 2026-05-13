#ifndef VIDEO_RECORD_WINDOW_H
#define VIDEO_RECORD_WINDOW_H

#include <QWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>

class VideoRecordWindow : public QWidget
{
    Q_OBJECT

public:
    explicit VideoRecordWindow(QWidget *parent = nullptr);
    ~VideoRecordWindow();

signals:
    void backToHome();

private slots:
    void onRefresh();
    void onBack();
    void onPlay(int row);

private:
    void setupUI();
    void loadData();

    QTableWidget* m_table;
    QPushButton* m_refreshBtn;
    QPushButton* m_backBtn;
};

#endif