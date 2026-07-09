#ifndef WIDGET_H
#define WIDGET_H

#include <QWidget>
#include <QSerialPort>
#include <QTimer>
#include <QQueue>
#include <QImage>

QT_BEGIN_NAMESPACE
namespace Ui { class Widget; }
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();

private slots:
    void on_btnRefresh_clicked();
    void on_btnOpen_clicked();
    void on_btnClearLog_clicked();
    void on_btnSelectAvatar_clicked();
    void on_btnIssueCard_clicked();
    void on_btnClearCard_clicked();

    void processSerialData();
    void onCommandTimerTimeout();

    // 新增：用于图像预览实时更新的槽函数
    void on_leName_textChanged(const QString &text);
    void on_leDept_textChanged(const QString &text);
    void on_sliderThreshold_valueChanged(int value);

private:
    Ui::Widget *ui;

    QSerialPort *m_serialPort;
    bool m_isSerialConnected;

    QByteArray m_receiveBuffer;
    QTimer *m_commandTimer;
    QQueue<QByteArray> m_commandQueue;
    int m_totalCommands;
    bool m_isStrictWaiting;

    QImage m_avatarImage;
    int m_threshold; // 保存当前的二值化阈值

    void appendLog(const QString &msg, bool isHex = false);
    void processSerialLine(const QByteArray &lineData);
    void startCommandQueue();
    void sendNextCommand();
    void sendCommand(const QByteArray &cmd);

    // 新增/修改：图像处理函数
    void applyQSS();
    void updatePreviews();
    QImage generateTextImage(const QString& text, int width, int height);
    QImage binarizeImage(const QImage& img, int threshold);
    QByteArray imageTo1Bpp(const QImage& img, int width, int height, int threshold);
};

#endif // WIDGET_H
