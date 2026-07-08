#ifndef KEYBOARD_DRAG_HELPER_H
#define KEYBOARD_DRAG_HELPER_H

#include <QObject>
#include <QPointer>
#include <QPoint>

class QWidget;
class QTimer;

// ================================================================
// KeyboardDragHelper：给 tgtsml 输入法键盘加拖动支持
// ----------------------------------------------------------------
// 原理：
//   tgtsml 的 KeyboardForm 是一个 Qt::Tool | Qt::FramelessWindowHint
//   的顶层 QWidget。本类通过 QApplication::topLevelWidgets() 找到
//   它，安装事件过滤器拦截鼠标事件，实现拖动。
//
//   键盘顶部约 30px 的空白区域（addStretch）作为拖动把手。
//
// 用法：
//   KeyboardDragHelper::instance()->enable();  // 应用启动时调用一次
//   KeyboardDragHelper::instance()->disable();
//
// 实现：定时扫描顶层窗口（键盘可能尚未创建），找到后安装过滤器。
// ================================================================
class KeyboardDragHelper : public QObject
{
    Q_OBJECT
public:
    static KeyboardDragHelper* instance();

    // 启用拖动支持（开始定时扫描键盘窗口）
    void enable();
    // 禁用拖动支持
    void disable();

protected:
    // 事件过滤器：拦截键盘窗口的鼠标事件
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    explicit KeyboardDragHelper(QObject* parent = nullptr);

    // 定时扫描顶层窗口，找到键盘窗口并安装事件过滤器
    void scanKeyboardWindow();
    // 判断 widget 是否是 tgtsml 键盘窗口
    bool isKeyboardWidget(QWidget* w) const;
    // 判断点击位置是否在键盘顶部拖动区域
    bool isDragArea(QWidget* kb, const QPoint& pos) const;

private:
    QTimer*         m_timer;
    QPointer<QWidget> m_keyboard;  // 找到的键盘窗口（弱引用）
    bool            m_dragging;
    QPoint          m_dragOffset;
};

#endif // KEYBOARD_DRAG_HELPER_H
