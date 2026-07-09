#include "widget.h"
#include "ui_widget.h"
#include <QSerialPortInfo>
#include <QMessageBox>
#include <QFileDialog>
#include <QDateTime>
#include <QPainter>
#include <QDebug>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
    , m_isSerialConnected(false)
    , m_totalCommands(0)
    , m_isStrictWaiting(false)
    , m_threshold(128)
{
    ui->setupUi(this);
    applyQSS(); // 应用极简美观的UI样式

    m_serialPort = new QSerialPort(this);
    connect(m_serialPort, &QSerialPort::readyRead, this, &Widget::processSerialData);

    m_commandTimer = new QTimer(this);
    m_commandTimer->setInterval(50);
    connect(m_commandTimer, &QTimer::timeout, this, &Widget::onCommandTimerTimeout);

    on_btnRefresh_clicked();
    updatePreviews();
}

Widget::~Widget()
{
    if (m_serialPort->isOpen()) {
        m_serialPort->close();
    }
    delete ui;
}

// ---------------- UI 美化 ----------------
void Widget::applyQSS()
{
    QString qss = R"(
        QWidget {
            font-family: "Microsoft YaHei", "Segoe UI";
            font-size: 13px;
            color: #333333;
            background-color: #F7F7F9;
        }
        QGroupBox {
            background-color: #FFFFFF;
            border: 1px solid #E5E5EA;
            border-radius: 8px;
            margin-top: 15px;
            padding-top: 15px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 15px;
            padding: 0 5px;
            color: #555555;
            font-weight: bold;
        }
        QLineEdit, QComboBox, QTextEdit {
            border: 1px solid #D1D1D6;
            border-radius: 4px;
            padding: 4px 8px;
            background-color: #FFFFFF;
        }
        QLineEdit:focus, QComboBox:focus, QTextEdit:focus {
            border: 1px solid #007AFF;
        }
        QPushButton {
            background-color: #F2F2F7;
            border: 1px solid #D1D1D6;
            border-radius: 4px;
            padding: 6px 12px;
            color: #333333;
        }
        QPushButton:hover {
            background-color: #E5E5EA;
        }
        QPushButton#btnIssueCard {
            background-color: #007AFF;
            color: #FFFFFF;
            font-weight: bold;
            border: none;
            padding: 8px;
        }
        QPushButton#btnIssueCard:hover {
            background-color: #0066CC;
        }
        QPushButton#btnClearCard {
            background-color: #FF3B30;
            color: #FFFFFF;
            font-weight: bold;
            border: none;
            padding: 8px;
        }
        QPushButton#btnClearCard:hover {
            background-color: #CC2E26;
        }
        QLabel[alignment="AlignCenter"] {
            border: 1px solid #D1D1D6;
            background-color: #FFFFFF;
            border-radius: 4px;
        }
        QLabel#lblDispTitle {
            font-size: 16px;
            font-weight: bold;
            color: #007AFF;
            border: none;
            background-color: transparent;
        }
        QLabel#lblDispUid, QLabel#lblDispSid, QLabel#lblDispType {
            font-size: 15px;
            font-weight: bold;
            color: #1C1C1E;
        }
        QProgressBar {
            border: 1px solid #D1D1D6;
            border-radius: 4px;
            text-align: center;
            background-color: #F2F2F7;
        }
        QProgressBar::chunk {
            background-color: #34C759;
            border-radius: 3px;
        }
    )";
    this->setStyleSheet(qss);
}

// ---------------- 串口逻辑 ----------------
void Widget::on_btnRefresh_clicked()
{
    ui->cmbPorts->clear();
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        ui->cmbPorts->addItem(info.portName());
    }
}

void Widget::on_btnOpen_clicked()
{
    if (!m_isSerialConnected) {
        QString portName = ui->cmbPorts->currentText();
        if (portName.isEmpty()) return;

        m_serialPort->setPortName(portName);
        m_serialPort->setBaudRate(QSerialPort::Baud115200);
        m_serialPort->setDataBits(QSerialPort::Data8);
        m_serialPort->setParity(QSerialPort::NoParity);
        m_serialPort->setStopBits(QSerialPort::OneStop);
        m_serialPort->setFlowControl(QSerialPort::NoFlowControl);

        if (m_serialPort->open(QIODevice::ReadWrite)) {
            m_isSerialConnected = true;
            ui->btnOpen->setText("关闭串口");
            appendLog(QString("已打开串口: %1").arg(portName));
        } else {
            QMessageBox::critical(this, "错误", "无法打开串口！");
        }
    } else {
        m_serialPort->close();
        m_isSerialConnected = false;
        ui->btnOpen->setText("打开串口");
        appendLog("串口已关闭");
    }
}

void Widget::processSerialData()
{
    m_receiveBuffer.append(m_serialPort->readAll());
    while (m_receiveBuffer.contains('\n')) {
        int idx = m_receiveBuffer.indexOf('\n');
        QByteArray line = m_receiveBuffer.left(idx + 1);
        m_receiveBuffer.remove(0, idx + 1);
        processSerialLine(line);
    }
}

void Widget::processSerialLine(const QByteArray &lineData)
{
    bool isHex = ui->rbHex->isChecked();
    QString text = isHex ? lineData.toHex(' ').toUpper() : QString::fromLocal8Bit(lineData).trimmed();
    appendLog("接收: " + text, isHex);

    // ========== 严格模式下的应答逻辑 ==========
    if (m_isStrictWaiting) {
        if (text.contains("OK")) {
            sendNextCommand();
        } else if (text.contains("ERR")) {
            appendLog("--- 收到错误应答，中止下发序列 ---");
            m_commandQueue.clear();
            m_isStrictWaiting = false;
            ui->progressBar->setStyleSheet("QProgressBar::chunk { background-color: #FF3B30; }");
        }
    }

    // ========== 解析下位机主动上发的账户头 ==========
    // 新格式目标: "OK:UID=C350141B,SID=7772222,TYPE=0"
    if (text.contains("UID=") && text.contains("SID=") && text.contains("TYPE=")) {
        QString payload = text;

        // 如果包含冒号，提取冒号后面的负载部分（即 UID=...,SID=...,TYPE=...）
        if (text.contains(":")) {
            payload = text.mid(text.indexOf(":") + 1).trimmed();
        }

        QStringList parts = payload.split(",");
        QString uid, sid;
        int type = 0;

        // 遍历以逗号分割的各个键值对
        for (const QString &part : parts) {
            QString trimmedPart = part.trimmed();
            if (trimmedPart.startsWith("UID=")) {
                uid = trimmedPart.mid(4);  // 截取 "UID=" 后的内容
            } else if (trimmedPart.startsWith("SID=")) {
                sid = trimmedPart.mid(4);  // 截取 "SID=" 后的内容
            } else if (trimmedPart.startsWith("TYPE=")) {
                type = trimmedPart.mid(5).toInt(); // 截取 "TYPE=" 后的内容并转为整数
            }
        }

        // 解析成功后更新UI界面
        if (!uid.isEmpty()) {
            // 1. 自动同步到左侧发卡配置输入框
            ui->leUid->setText(uid);
            ui->leSid->setText(sid);
            if (type >= 0 && type < ui->cmbCardType->count()) {
                ui->cmbCardType->setCurrentIndex(type);
            }

            // 2. 自动同步到右侧专用的当前刷卡信息面板
            ui->lblDispUid->setText(uid);
            ui->lblDispSid->setText(sid);
            ui->lblDispType->setText(ui->cmbCardType->itemText(type));
        }
    }
}

void Widget::appendLog(const QString &msg, bool isHex)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    ui->txtLog->append(QString("[%1] %2").arg(timestamp).arg(msg));
}

void Widget::on_btnClearLog_clicked()
{
    ui->txtLog->clear();
}

// ---------------- 图像处理与预览 ----------------
void Widget::on_sliderThreshold_valueChanged(int value)
{
    m_threshold = value;
    ui->lblThresholdVal->setText(QString::number(value));
    updatePreviews();
}

void Widget::on_leName_textChanged(const QString &text) { Q_UNUSED(text); updatePreviews(); }
void Widget::on_leDept_textChanged(const QString &text) { Q_UNUSED(text); updatePreviews(); }

void Widget::on_btnSelectAvatar_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "选择头像", "", "Images (*.png *.jpg *.bmp)");
    if (!filePath.isEmpty()) {
        m_avatarImage.load(filePath);
        ui->lblAvatarPath->setText(filePath);
        updatePreviews();
    }
}

// 生成白底黑字的文本图像
QImage Widget::generateTextImage(const QString& text, int width, int height)
{
    QImage img(width, height, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setPen(Qt::black);
    p.setFont(QFont("Microsoft YaHei", 10, QFont::Bold));
    p.drawText(img.rect(), Qt::AlignCenter, text);
    p.end();
    return img;
}

// 执行二值化操作返回预览图像 (超过阈值为白，否则为黑)
QImage Widget::binarizeImage(const QImage& img, int threshold)
{
    QImage result = img.convertToFormat(QImage::Format_Grayscale8);
    for (int y = 0; y < result.height(); ++y) {
        uchar *line = result.scanLine(y);
        for (int x = 0; x < result.width(); ++x) {
            // 背景（白色）保持，文字（深色）若低于阈值则变黑
            line[x] = (line[x] > threshold) ? 255 : 0;
        }
    }
    return result;
}

// 统一更新UI上的预览框
void Widget::updatePreviews()
{
    // 头像预览
    if (!m_avatarImage.isNull()) {
        QImage scaled = m_avatarImage.scaled(48, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QImage bw = binarizeImage(scaled, m_threshold);
        ui->lblAvatarPreview->setPixmap(QPixmap::fromImage(bw));
    }

    // 姓名预览
    QString name = ui->leName->text();
    if (!name.isEmpty()) {
        QImage nameImg = generateTextImage(name, 80, 16);
        ui->lblNamePreview->setPixmap(QPixmap::fromImage(binarizeImage(nameImg, m_threshold)));
    } else {
        ui->lblNamePreview->clear();
        ui->lblNamePreview->setText("姓名预览");
    }

    // 部门预览
    QString dept = ui->leDept->text();
    if (!dept.isEmpty()) {
        QImage deptImg = generateTextImage(dept, 80, 16);
        ui->lblDeptPreview->setPixmap(QPixmap::fromImage(binarizeImage(deptImg, m_threshold)));
    } else {
        ui->lblDeptPreview->clear();
        ui->lblDeptPreview->setText("部门预览");
    }
}

// 将图像转为1BPP格式下发，使用动态阈值
QByteArray Widget::imageTo1Bpp(const QImage& img, int width, int height, int threshold)
{
    QImage scaled = img.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QByteArray outData;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; x += 8) {
            unsigned char byte = 0;
            for (int b = 0; b < 8; ++b) {
                if (x + b < width) {
                    int gray = qGray(scaled.pixel(x + b, y));
                    // OLED 亮像素通常为1，暗像素为0。高于阈值为背景(不亮)，低于阈值为文字(亮)
                    if (gray <= threshold) byte |= (1 << (7 - b));
                }
            }
            outData.append(byte);
        }
    }
    return outData;
}

// ---------------- 发卡与销卡控制 ----------------
void Widget::on_btnIssueCard_clicked()
{
    if (!m_isSerialConnected) {
        QMessageBox::warning(this, "警告", "请先打开串口！");
        return;
    }

    m_commandQueue.clear();
    m_commandTimer->stop();
    m_isStrictWaiting = false;
    ui->progressBar->setStyleSheet("QProgressBar::chunk { background-color: #34C759; }");

    QString uidStr = ui->leUid->text().toUpper().rightJustified(8, '0');
    uint sidDec = ui->leSid->text().toUInt();
    QString sidStr = QString::number(sidDec);
    QString ctypeStr = QString::number(ui->cmbCardType->currentIndex());

    // 1. 发送账户头 ISSUE 命令
    QString issueCmd = QString("ISSUE:%1,%2,0,%3\n").arg(uidStr).arg(sidStr).arg(ctypeStr);
    m_commandQueue.enqueue(issueCmd.toUtf8());

    // 2. 仅当卡片类型为图像卡 (索引为 1) 时，才下发头像数据 (IMGA)
    if (ui->cmbCardType->currentIndex() == 1) {
        if (!m_avatarImage.isNull()) {
            QByteArray avatarBytes = imageTo1Bpp(m_avatarImage, 48, 64, m_threshold);
            for (int i = 0; i < 24; ++i) {
                QString cmd = QString("IMGA%1:%2\n").arg(i, 2, 10, QChar('0'))
                .arg(QString(avatarBytes.mid(i * 16, 16).toHex().toUpper()));
                m_commandQueue.enqueue(cmd.toUtf8());
            }
        } else {
            // 如果是图像卡但未选择头像，下发空数据占位
            for (int i = 0; i < 24; ++i) {
                QString cmd = QString("IMGA%1:00000000000000000000000000000000\n").arg(i, 2, 10, QChar('0'));
                m_commandQueue.enqueue(cmd.toUtf8());
            }
        }
    }

    // 3. 所有卡片（普通卡、图像卡、管理员卡）均需要下发姓名与部门图像数据
    // 下发姓名数据块 (IMGN)
    QImage nameImg = generateTextImage(ui->leName->text(), 80, 16);
    QByteArray nameBytes = imageTo1Bpp(nameImg, 80, 16, m_threshold);
    for (int i = 0; i < 10; ++i) {
        QString cmd = QString("IMGN%1:%2\n").arg(i, 2, 10, QChar('0'))
        .arg(QString(nameBytes.mid(i * 16, 16).toHex().toUpper()));
        m_commandQueue.enqueue(cmd.toUtf8());
    }

    // 下发部门数据块 (IMGD)
    QImage deptImg = generateTextImage(ui->leDept->text(), 80, 16);
    QByteArray deptBytes = imageTo1Bpp(deptImg, 80, 16, m_threshold);
    for (int i = 0; i < 10; ++i) {
        QString cmd = QString("IMGD%1:%2\n").arg(i, 2, 10, QChar('0'))
        .arg(QString(deptBytes.mid(i * 16, 16).toHex().toUpper()));
        m_commandQueue.enqueue(cmd.toUtf8());
    }

    // 4. 所有卡片在图像下发完成后，发送更新映射命令
    m_commandQueue.enqueue(QByteArray("UPDATEIMG\n"));

    // 5. 最后读取一遍以作验证
    m_commandQueue.enqueue(QByteArray("READ\n"));

    // 启动发送队列
    startCommandQueue();
}

void Widget::on_btnClearCard_clicked()
{
    if (!m_isSerialConnected) {
        QMessageBox::warning(this, "警告", "请先打开串口！");
        return;
    }
    QString uidStr = ui->leUid->text().toUpper().rightJustified(8, '0');
    QString clearCmd = QString("CLEAR:%1\n").arg(uidStr);

        m_commandQueue.clear();
    m_commandQueue.enqueue(clearCmd.toUtf8());
    startCommandQueue();
}

void Widget::startCommandQueue()
{
    m_totalCommands = m_commandQueue.size();
    ui->progressBar->setMaximum(m_totalCommands);
    ui->progressBar->setValue(0);

    if (m_totalCommands > 0) {
        if (ui->chkTestMode->isChecked()) {
            appendLog("--- 开启测试模式(连续发送) ---");
            m_commandTimer->start();
        } else {
            appendLog("--- 开启严格模式(等待应答) ---");
            m_isStrictWaiting = true;
            sendNextCommand();
        }
    }
}

void Widget::sendNextCommand()
{
    if (m_commandQueue.isEmpty()) {
        ui->progressBar->setValue(m_totalCommands);
        appendLog("--- 序列执行完毕 ---");
        m_isStrictWaiting = false;
        return;
    }

    QByteArray cmd = m_commandQueue.dequeue();
    sendCommand(cmd);
    ui->progressBar->setValue(m_totalCommands - m_commandQueue.size());
}

void Widget::onCommandTimerTimeout()
{
    sendNextCommand();
    if (m_commandQueue.isEmpty()) {
        m_commandTimer->stop();
    }
}

void Widget::sendCommand(const QByteArray &cmd)
{
    if (m_serialPort->isOpen()) {
        m_serialPort->write(cmd);
        appendLog("发送: " + QString::fromLocal8Bit(cmd).trimmed());
    }
}
