#include "face_list_window.h"
#include "db/db_manager.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QDebug>

FaceListWindow::FaceListWindow(QWidget *parent)
    : QWidget(parent)
    , m_table(nullptr)
    , m_refreshBtn(nullptr)
    , m_backBtn(nullptr)
{
    setupUI();
    loadData();
}

FaceListWindow::~FaceListWindow()
{
}

void FaceListWindow::setupUI()
{
    setStyleSheet("background-color: #2c3e50;");
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);
    
    // 标题
    QLabel* title = new QLabel("人脸库管理");
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
    
    connect(m_refreshBtn, &QPushButton::clicked, this, &FaceListWindow::onRefresh);
    connect(m_backBtn, &QPushButton::clicked, this, &FaceListWindow::onBack);
}

void FaceListWindow::loadData()
{
    QVector<FaceRecord> faces = DbManager::instance().getAllFaces();

    m_table->setRowCount(faces.size());
    m_table->setColumnCount(4);  // 特征ID、姓名、所属房间、操作
    m_table->setHorizontalHeaderLabels(QStringList() << "特征ID" << "姓名" << "所属房间" << "操作");

    for (int i = 0; i < faces.size(); i++) {
        // 特征ID
        m_table->setItem(i, 0, new QTableWidgetItem(QString::number(faces[i].featureId)));

        // 姓名
        m_table->setItem(i, 1, new QTableWidgetItem(faces[i].personName));

        // 所属房间
        m_table->setItem(i, 2, new QTableWidgetItem(QString("房间%1").arg(faces[i].roomId + 1)));

        // ============================================================
        // 删除按钮
        // ============================================================
        QPushButton* deleteBtn = new QPushButton("删除");
        deleteBtn->setFixedSize(60, 30);
        deleteBtn->setStyleSheet(
            "QPushButton {"
            "    background-color: #e74c3c;"
            "    color: white;"
            "    font-size: 12px;"
            "    border-radius: 4px;"
            "}"
            "QPushButton:pressed {"
            "    background-color: #c0392b;"
            "}"
        );

        int featureId = faces[i].featureId;
        QString name = faces[i].personName;

        connect(deleteBtn, &QPushButton::clicked, [this, featureId, name]() {
            qDebug() << "[FaceListWindow] Delete button clicked for ID:" << featureId;

            QMessageBox::StandardButton reply = QMessageBox::question(
                this, "确认删除",
                QString("确定要删除「%1」(ID=%2) 吗？").arg(name).arg(featureId),
                QMessageBox::Yes | QMessageBox::No
            );

            if (reply == QMessageBox::Yes) {
                qDebug() << "[FaceListWindow] Emitting faceDeleteRequested signal";
                emit faceDeleteRequested(featureId, name);
            }
        });

        m_table->setCellWidget(i, 3, deleteBtn);
    }

    m_table->setColumnWidth(0, 100);
    m_table->setColumnWidth(1, 150);
    m_table->setColumnWidth(2, 110);
    m_table->setColumnWidth(3, 80);

    qDebug() << "[FaceListWindow] Loaded" << faces.size() << "faces";
}

void FaceListWindow::onRefresh()
{
    loadData();
}

void FaceListWindow::onBack()
{
    emit backToHome();
}