#include "attendance_window.h"
#include "db/db_manager.h"
#include <QHeaderView>
#include <QDebug>

AttendanceWindow::AttendanceWindow(QWidget *parent)
    : QWidget(parent)
    , m_table(nullptr)
    , m_refreshBtn(nullptr)
    , m_backBtn(nullptr)
{
    setupUI();
    loadData();
}

AttendanceWindow::~AttendanceWindow()
{
}

void AttendanceWindow::setupUI()
{
    setStyleSheet("background-color: #2c3e50;");
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);
    
    // 标题
    QLabel* title = new QLabel("考勤记录");
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
    
    connect(m_refreshBtn, &QPushButton::clicked, this, &AttendanceWindow::onRefresh);
    connect(m_backBtn, &QPushButton::clicked, this, &AttendanceWindow::onBack);
}

void AttendanceWindow::loadData()
{
    QVector<AttendanceRecord> records = DbManager::instance().getAllAttendance();
    
    m_table->setRowCount(records.size());
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(QStringList() << "ID" << "姓名" << "类型" << "时间");
    
    for (int i = 0; i < records.size(); i++) {
        m_table->setItem(i, 0, new QTableWidgetItem(QString::number(records[i].id)));
        m_table->setItem(i, 1, new QTableWidgetItem(records[i].personName));
        
        // 根据类型设置颜色
        QTableWidgetItem* typeItem = new QTableWidgetItem(records[i].type);
        if (records[i].type == "签到") {
            typeItem->setForeground(Qt::green);
        } else if (records[i].type == "签退") {
            typeItem->setForeground(Qt::red);
        }
        m_table->setItem(i, 2, typeItem);
        
        m_table->setItem(i, 3, new QTableWidgetItem(records[i].time));
    }
    
    m_table->setColumnWidth(0, 60);
    m_table->setColumnWidth(1, 120);
    m_table->setColumnWidth(2, 80);
    m_table->setColumnWidth(3, 180);
    
    qDebug() << "[AttendanceWindow] Loaded" << records.size() << "records";
}

void AttendanceWindow::onRefresh()
{
    loadData();
}

void AttendanceWindow::onBack()
{
    emit backToHome();
}