#include "keyboard_drag_helper.h"
#include <QApplication>
#include <QWidget>
#include <QTimer>
#include <QMouseEvent>
#include <QDebug>

// ================================================================
// 单例
// ================================================================
KeyboardDragHelper* KeyboardDragHelper::instance()
{
    static KeyboardDragHelper* inst = nullptr;
    if (!inst) {
        inst = new KeyboardDragHelper(qApp);
    }
    return inst;
}

// ================================================================
// 构造
// ================================================================
KeyboardDragHelper::KeyboardDragHelper(QObject* parent)
    : QObject(parent), m_timer(nullptr), m_dragging(false)
{
}

// ================================================================
// enable：启动定时扫描
// ----------------------------------------------------------------
// 每 500ms 扫描一次顶层窗口，找到键盘后安装事件过滤器
// 找到后停止扫描；键盘销毁后（m_keyboard 变空）重新扫描
// ================================================================
void KeyboardDragHelper::enable()
{
    if (m_timer) return;  // 已启用

    m_timer = new QTimer(this);
    m_timer->setInterval(500);
    connect(m_timer, &QTimer::timeout, this, &KeyboardDragHelper::scanKeyboardWindow);
    m_timer->start();
    qDebug() << "[KbDrag] 启用键盘拖动支持";
}

void KeyboardDragHelper::disable()
{
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
    if (m_keyboard) {
        m_keyboard->removeEventFilter(this);
        m_keyboard = nullptr;
    }
}

// ================================================================
// scanKeyboardWindow：扫描顶层窗口找到键盘
// ================================================================
void KeyboardDragHelper::scanKeyboardWindow()
{
    // 已找到且仍有效
    if (m_keyboard) {
        return;
    }

    // 遍历所有顶层窗口
    const QWidgetList widgets = QApplication::topLevelWidgets();
    for (QWidget* w : widgets) {
        if (isKeyboardWidget(w)) {
            m_keyboard = w;
            w->installEventFilter(this);
            qDebug() << "[KbDrag] 找到键盘窗口，已安装事件过滤器:" << w;
            break;
        }
    }
}

// ================================================================
// isKeyboardWidget：判断是否是 tgtsml 键盘窗口
// ----------------------------------------------------------------
// tgtsml KeyboardForm 的特征：
//   - 顶层窗口
//   - WindowFlags 包含 Qt::Tool | Qt::FramelessWindowHint
//   - 固定大小 800x250
//   - 无父对象（独立窗口）
//   - 类名是 "KeyboardForm"
// ================================================================
bool KeyboardDragHelper::isKeyboardWidget(QWidget* w) const
{
    if (!w || w->parent()) return false;

    // 检查类名（最可靠）
    QString className = w->metaObject()->className();
    if (className == "KeyboardForm") return true;

    // 退化检查：窗口标志 + 固定大小
    Qt::WindowFlags flags = w->windowFlags();
    if ((flags & Qt::Tool) && (flags & Qt::FramelessWindowHint)) {
        QSize sz = w->size();
        // tgtsml 默认 800x250，容差一些
        if (sz.width() >= 700 && sz.width() <= 900 &&
            sz.height() >= 200 && sz.height() <= 300) {
            return true;
        }
    }
    return false;
}

// ================================================================
// isDragArea：判断点击位置是否在键盘顶部拖动区域
// ----------------------------------------------------------------
// tgtsml 键盘布局：顶部 addStretch(1) 约 30-50px 空白
// 这个空白区域作为拖动把手
// ================================================================
bool KeyboardDragHelper::isDragArea(QWidget* kb, const QPoint& pos) const
{
    // 顶部 50px 作为拖动区域
    return pos.y() < 50;
}

// ================================================================
// eventFilter：拦截键盘窗口的鼠标事件
// ----------------------------------------------------------------
// 在顶部拖动区域按下鼠标 → 拖动移动键盘
// ================================================================
bool KeyboardDragHelper::eventFilter(QObject* watched, QEvent* event)
{
    QWidget* kb = qobject_cast<QWidget*>(watched);
    if (!kb || kb != m_keyboard) {
        return QObject::eventFilter(watched, event);
    }

    switch (event->type()) {
        case QEvent::MouseButtonPress: {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && isDragArea(kb, me->pos())) {
                m_dragging = true;
                m_dragOffset = me->globalPos() - kb->frameGeometry().topLeft();
                return true;  // 事件已处理，不传递给键盘
            }
            break;
        }
        case QEvent::MouseMove: {
            if (m_dragging) {
                QMouseEvent* me = static_cast<QMouseEvent*>(event);
                if (me->buttons() & Qt::LeftButton) {
                    kb->move(me->globalPos() - m_dragOffset);
                    return true;
                }
            }
            break;
        }
        case QEvent::MouseButtonRelease: {
            if (m_dragging) {
                m_dragging = false;
                return true;
            }
            break;
        }
        default:
            break;
    }

    return QObject::eventFilter(watched, event);
}
