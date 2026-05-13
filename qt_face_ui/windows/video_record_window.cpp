#include "video_record_window.h"
#include "db/db_manager.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QDebug>
#include <QFileInfo>
#include <QProcess>

VideoRecordWindow::VideoRecordWindow(QWidget *parent)
    : QWidget(parent)
    , m_table(nullptr)
    , m_refreshBtn(nullptr)
    , m_backBtn(nullptr)
{
    setupUI();
    loadData();
}

VideoRecordWindow::~VideoRecordWindow()
{
}

void VideoRecordWindow::setupUI()
{
    setStyleSheet("background-color: #2c3e50;");
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);
    
    // 标题
    QLabel* title = new QLabel("录像记录");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size: 24px; font-weight: bold; color: #3498db;");
    mainLayout->addWidget(title);
    
    // 表格
    m_table = new QTableWidget(this);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setStyleSheet(
        "QTableWidget {"
        "    background-color: white;"
        "    alternate-background-color: #f5f5f5;"
        "}"
        "QHeaderView::section {"
        "    background-color: #3498db;"
        "    color: white;"
        "    padding: 5px;"
        "}"
    );
    mainLayout->addWidget(m_table);
    
    // 按钮布局
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(20);
    
    m_refreshBtn = new QPushButton("刷新");
    m_refreshBtn->setFixedSize(120, 50);
    m_refreshBtn->setStyleSheet(
        "QPushButton {"
        "    background-color: #27ae60;"
        "    color: white;"
        "    font-size: 16px;"
        "    border-radius: 8px;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #229954;"
        "}"
    );
    
    m_backBtn = new QPushButton("返回");
    m_backBtn->setFixedSize(120, 50);
    m_backBtn->setStyleSheet(
        "QPushButton {"
        "    background-color: #7f8c8d;"
        "    color: white;"
        "    font-size: 16px;"
        "    border-radius: 8px;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #6c7a7a;"
        "}"
    );
    
    btnLayout->addStretch();
    btnLayout->addWidget(m_refreshBtn);
    btnLayout->addWidget(m_backBtn);
    btnLayout->addStretch();
    mainLayout->addLayout(btnLayout);
    
    connect(m_refreshBtn, &QPushButton::clicked, this, &VideoRecordWindow::onRefresh);
    connect(m_backBtn, &QPushButton::clicked, this, &VideoRecordWindow::onBack);
}

void VideoRecordWindow::loadData()
{
    QVector<VideoRecord> records = DbManager::instance().getAllVideoRecords();
    
    m_table->setRowCount(records.size());
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(QStringList() << "ID" << "通道" << "开始时间" << "结束时间" << "录像文件" << "操作");
    
    for (int i = 0; i < records.size(); i++) {
        m_table->setItem(i, 0, new QTableWidgetItem(QString::number(records[i].id)));
        m_table->setItem(i, 1, new QTableWidgetItem(QString::number(records[i].streamId + 1)));
        m_table->setItem(i, 2, new QTableWidgetItem(records[i].startTime));
        m_table->setItem(i, 3, new QTableWidgetItem(records[i].endTime));
        
        // 文件路径只显示文件名
        QString fileName = records[i].filePath;
        int lastSlash = fileName.lastIndexOf('/');
        if (lastSlash >= 0) {
            fileName = fileName.mid(lastSlash + 1);
        }
        m_table->setItem(i, 4, new QTableWidgetItem(fileName));
        
        // 播放按钮
        QPushButton* playBtn = new QPushButton("播放");
        playBtn->setFixedSize(60, 30);
        playBtn->setStyleSheet(
            "QPushButton {"
            "    background-color: #3498db;"
            "    color: white;"
            "    font-size: 12px;"
            "    border-radius: 4px;"
            "}"
            "QPushButton:pressed {"
            "    background-color: #2980b9;"
            "}"
        );
        connect(playBtn, &QPushButton::clicked, [this, i]() { onPlay(i); });
        m_table->setCellWidget(i, 5, playBtn);
    }
    
    m_table->setColumnWidth(0, 50);
    m_table->setColumnWidth(1, 60);
    m_table->setColumnWidth(2, 140);
    m_table->setColumnWidth(3, 140);
    m_table->setColumnWidth(4, 200);
    m_table->setColumnWidth(5, 80);
    
    qDebug() << "[VideoRecordWindow] Loaded" << records.size() << "records";
}

void VideoRecordWindow::onPlay(int row)
{
    QVector<VideoRecord> records = DbManager::instance().getAllVideoRecords();
    if (row < 0 || row >= records.size()) return;
    
    QString filePath = records[row].filePath;
    
    if (!QFile::exists(filePath)) {
        QMessageBox::warning(this, "文件不存在", 
            QString("录像文件不存在:\n%1").arg(filePath));
        return;
    }
    
    // 使用 mpv 播放器（独立窗口）
    QProcess* player = new QProcess(this);
    QStringList args;
    args << "--no-border" << "--ontop" << "--keepaspect-window";
    args << filePath;
    
    player->start("mpv", args);
    
    connect(player, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            player, &QProcess::deleteLater);
    
    qDebug() << "[VideoRecordWindow] Playing:" << filePath;
}

void VideoRecordWindow::onRefresh()
{
    loadData();
}

void VideoRecordWindow::onBack()
{
    emit backToHome();
}