## NFC 考勤系统设计

2026专业实践综合设计II ·课程设计

## CONTENTS

课程导览

## 01

## 课程概述与目标

课程定位、设计目标与硬件平台介绍

## 02

## 功能要求与阶段划分

三阶段递进式开发：从最小可用闭环到联网通信

## 03

## 验收标准与评分

必做内容验收标准、扩展加分项与评分说明

## 04

## 技术方案与资源

系统软件架构、推荐技术栈与开发资源

## 01

## 课程概述与目标

课程定位、设计目标与硬件平台介绍

## 课程设计目标

本次课程设计以"NFC 考勤系统"为任务载体，在 STM32 + FreeRTOS 平台上完成完整的嵌入式应用系统。通过本项目，学生将实践以下五项核心能力：

<!-- image-->

## 硬件驱动集成

在BSP驱动基础上整合多外设模块，理解初始化顺序与资源冲突处理

## 通信协议设计

完成上下位机文本命令协议设计，理解帧格式、校验、超时重传

<!-- image-->

## 实时操作系统应用

在FreeRTOS多任务框架下划分任务职责，使用任务通知与队列通信

<!-- image-->

<!-- image-->

## 嵌入式存储管理

设计循环日志结构，处理Flash扇区擦除与预擦除策略

<!-- image-->

## 系统联调与测试

从最小可用闭环开始，逐步叠加功能，掌握分阶段集成与 验证

<!-- image-->

核心任务：在给定的STM32硬件平台上，完成一个具备刷卡识别、信息显示、脱机存储与上位机发卡功能的完整嵌入式应用系统。

## 硬件平台配置

<table><tr><td rowspan=1 colspan=1>模块</td><td rowspan=1 colspan=1>规格</td><td rowspan=1 colspan=1>在本项目中的用途</td></tr><tr><td rowspan=1 colspan=1>主控芯片</td><td rowspan=1 colspan=1>STM32F407VGT6, Cortex-M4F,168 MHz</td><td rowspan=1 colspan=1>主控处理器，运行 FreeRTOS</td></tr><tr><td rowspan=1 colspan=1>调试接口</td><td rowspan=1 colspan=1>SWD + USB 串口</td><td rowspan=1 colspan=1>程序下载与调试输出</td></tr><tr><td rowspan=1 colspan=1>实时时钟</td><td rowspan=1 colspan=1>32.768 kHz LSE 晶振+VBAT备份电池</td><td rowspan=1 colspan=1>为考勤记录提供时间戳</td></tr><tr><td rowspan=1 colspan=1>显示模块</td><td rowspan=1 colspan=1>0.96 英寸OLED,128x64, I2C</td><td rowspan=1 colspan=1>显示时钟、刷卡结果、人员信息</td></tr><tr><td rowspan=1 colspan=1>读卡模块</td><td rowspan=1 colspan=1>RC522,SPI,支持 Mifare S50</td><td rowspan=1 colspan=1>读卡、写卡、发卡</td></tr><tr><td rowspan=1 colspan=1>存储模块</td><td rowspan=1 colspan=1>W25Q128, SPI NOR Flash,16 MB</td><td rowspan=1 colspan=1>设备配置与考勤记录持久化</td></tr><tr><td rowspan=1 colspan=1>无线模块</td><td rowspan=1 colspan=1>ESPO1S,WiFi透传,USART</td><td rowspan=1 colspan=1>联网校时、记录上传 (扩展)</td></tr><tr><td rowspan=1 colspan=1>人机交互</td><td rowspan=1 colspan=1>6个按键+7个LED+蜂鸣器</td><td rowspan=1 colspan=1>界面切换、状态指示、声光反馈</td></tr></table>

注：实验板已通过出厂测试，所有外设驱动以BSP包形式提供，学生无需从零编写寄存器级驱动，但需理解驱动接口、完成工程集成与业务逻辑开发。

## 课程安排与考核方式

<!-- image-->

## 课时安排

<table><tr><td>教学班</td><td>上机时间 (7.6-7.11)</td><td>地点 (4教)</td></tr><tr><td>01、 02、03</td><td>8:30--12:15</td><td>427-429-431</td></tr><tr><td>04、11、13、14</td><td>13:30--17:15</td><td>427-429-431</td></tr><tr><td>05、 06、09</td><td>8:30--12:15</td><td>403-405</td></tr><tr><td>07、08、10</td><td>13:30--17:15</td><td>403-405</td></tr></table>

<!-- image-->

## 成组方式

1\~2 人 / 组

鼓励结对协作

相互 review 代码

<!-- image-->

## 考核形式

总评成绩由电路设计、软件功能设计、测评验收、实践报告和课程思政成绩等几个部分构成。实物测评验收成绩占60%，平时作业、报告占40%

## 成绩组成

<table><tr><td rowspan=1 colspan=1>考核项目</td><td rowspan=1 colspan=1>考核形式</td><td rowspan=1 colspan=1>说明</td></tr><tr><td rowspan=1 colspan=1>必做功能</td><td rowspan=1 colspan=1>作业1-6</td><td rowspan=1 colspan=1>原理图设计、下位机阶段一+阶段二+上位机阶段一的基础内容</td></tr><tr><td rowspan=1 colspan=1>扩展功能</td><td rowspan=1 colspan=1>测评验收</td><td rowspan=1 colspan=1>下位机阶段三+上位机阶段二的扩展功能</td></tr><tr><td rowspan=1 colspan=1>设计报告</td><td rowspan=1 colspan=1>设计报告作业</td><td rowspan=1 colspan=1>结构完整、图表清晰、关键设计思路阐述清楚</td></tr><tr><td rowspan=1 colspan=1>验收答辩</td><td rowspan=1 colspan=1>测评验收</td><td rowspan=1 colspan=1>下位机+上位机所有必做内容演示，回答提问</td></tr></table>

## 02

## 功能要求与阶段划分

三阶段递进式开发：从最小可用闭环到联网通信

## 下位机阶段一：基础刷卡考勤

## 目标：实现"刷卡 → 读卡验证 → 显示人员信息→防重复"的最小闭环，可独立验证

## 1.1 驱动集成

将各BSP 驱动加入编译，确认初始化成功

实时读取并显示日期时间、星期；首次上电或时间不准时，通过按键进入时间设置界面调整

## 1.4 RTC 时钟与手动校时

## 1.2 刷卡读卡与防重复

周期性寻卡，读取UID与账户头，校验数据完整性，实现离卡检测与1分钟防重复

## 1.5 发卡代写

通过串口接收上位机下发的账户头与图像数据，写入Mifare 卡对应扇区

## 1.3 信息显示与界面切换

默认显示时钟界面；刷卡自动切换到结果界面（头像+姓名+工号+部门+时间）

## 1.6 声光状态提示

刷卡成功、失败、重复等不同事件，用不同LED灯号与蜂鸣器音调组合提示

<!-- image-->

## 阶段一验证标准

上电显示课程名称、项目名称、学号姓名、杭电LOGO；待机界面显示正确时钟；刷已发卡后，OLED 正确显示头像（或工号）、姓名、部门、时间；重复刷卡被过滤；通过上位机发卡后，下位机读卡能正确显示写入的信息。

授课重点：本阶段是后续所有功能的基础，要求必须完成。从最小可用闭环开始，确保刷卡→显示→防重复的核心链路跑通后，再进入下一阶段。建议教学时先演示完整效果，再引导学生逐步拆解实现。

## 下位机阶段二：考勤逻辑与存储

目标：在阶段一基础上加入考勤模式判定、脱机存储、设备配置，实现完整的单机考勤能力

## 2.1 考勤记录存储

将每条有效考勤记录写入Flash循环日志；设计写指针与记录区头部，掉电可恢复

## 2.3 设备配置区

在Flash固定区域存储设备编号与考勤模式，掉电不丢失；首次上电自动初始化默认值

## 2.2 考勤模式判定

出入口点模式下，离场时计算并显示从入场到离场的时长

支持三种模式：入口点（仅记录入场）、出口点（仅记录离场）、出入口点（交替入场离场）

## 2.4 管理员模式

识别管理员卡后进入设置界面，限时内可通过按键修改设备编号与考勤模式

## 2.5 在场时长计算

## 2.6 最近记录查看

通过串口下发指令打印最近N条记录

## 三种考勤模式对比

<table><tr><td rowspan=1 colspan=1>模式</td><td rowspan=1 colspan=1>入场刷卡</td><td rowspan=1 colspan=1>离场刷卡</td><td rowspan=1 colspan=1>典型场景</td></tr><tr><td rowspan=1 colspan=1>入口点</td><td rowspan=1 colspan=1>记录签到时间</td><td rowspan=1 colspan=1>拒绝 (无离场权限)</td><td rowspan=1 colspan=1>单门单向通道</td></tr><tr><td rowspan=1 colspan=1>出口点</td><td rowspan=1 colspan=1>拒绝 (无入场权限)</td><td rowspan=1 colspan=1>记录离场+计算时长 (需联网)</td><td rowspan=1 colspan=1>单门单向通道</td></tr><tr><td rowspan=1 colspan=1>出入口点</td><td rowspan=1 colspan=1>记录入场时间</td><td rowspan=1 colspan=1>记录离场+计算时长</td><td rowspan=1 colspan=1>需要统计在场时长的场合</td></tr></table>

## 下位机阶段三：联网通信（扩展加分）

<!-- image-->

## 扩展加分项：加入 WiFi 联网能力，实现与上位机的远程通信，适合能力较强的同学尝试

## 3.1 ESP01S 联网与 NTP 校时

自动连接 WiFi，通过NTP 服务器校准RTC，替代手动校时

## 3.3 加密上传协议

通过 TCP 透传将未上传记录以加密二进制帧推送到上位机

## 3.2 时间偏差校准

计算本地 RTC 与服务器时间偏差，供上位机统一时间

## 3.4 断网续传

断网期间记录暂存Flash；恢复网络后自动按顺序续传

## 3.5 天气预报

开机联网后查询当日天气，在待机界面滚动显示

## 3.6 心跳保活

定时向上位机发送心跳，上报设备状态与未上传记录数

<!-- image-->

## 提示

阶段三属于扩展加分项，不强求所有学生完成。建议在完成阶段一和阶段二后，根据学生能力和时间进度选择性尝试。测评时的优良成绩指标至少实现3.1+3.5

## 上位机阶段一：发卡工具

目标：使用C++/C#等任意高级语言设计一个PC端运行的上位机软件，能通过串口或网络与硬件板进行数据收发操作，实现发卡和考勤记录管理功能。  
阶段一目标：实现人员信息录入与发卡写卡，为下位机刷卡考勤提供已写入信息的卡片。

<table><tr><td rowspan=1 colspan=1>编号</td><td rowspan=1 colspan=1>功能</td><td rowspan=1 colspan=1>说明</td></tr><tr><td rowspan=1 colspan=1>A1.1</td><td rowspan=1 colspan=1>串口通信</td><td rowspan=1 colspan=1>打开串口、配置波特率、与下位机收发数据</td></tr><tr><td rowspan=1 colspan=1>A1.2</td><td rowspan=1 colspan=1>人员信息录入</td><td rowspan=1 colspan=1>界面录入工号、姓名、部门、卡类型 (普通/图像/管理员)</td></tr><tr><td rowspan=1 colspan=1>A1.3</td><td rowspan=1 colspan=1>头像处理</td><td rowspan=1 colspan=1>选择照片后缩放为48x64，灰度化、二值化，预览二值化效果</td></tr><tr><td rowspan=1 colspan=1>A1.4</td><td rowspan=1 colspan=1>姓名图像生成</td><td rowspan=1 colspan=1>将姓名文本渲染为80x16单色位图，预览效果</td></tr><tr><td rowspan=1 colspan=1>A1.5</td><td rowspan=1 colspan=1>部门图像生成</td><td rowspan=1 colspan=1>将部门文本渲染为 80x16单色位图</td></tr><tr><td rowspan=1 colspan=1>A1.6</td><td rowspan=1 colspan=1>发卡写卡</td><td rowspan=1 colspan=1>通过串口依次下发账户头、头像、姓名图像、部门图像，由下位机执行物理写卡</td></tr><tr><td rowspan=1 colspan=1>A1.7</td><td rowspan=1 colspan=1>清卡/补卡</td><td rowspan=1 colspan=1>清空卡片数据，或重新发卡绑定新 UID</td></tr><tr><td rowspan=1 colspan=1>A1.8 (扩展)</td><td rowspan=1 colspan=1>人员信息持久化</td><td rowspan=1 colspan=1>将发卡记录存入本地数据库或文件，便于后续查询</td></tr></table>

## 通信协议

上下位机之间采用纯 ASCII 英文命令的文本帧协议，以换行符结束。主要命令包括：读卡、清卡、发卡（写账户头）、逐块下发头像/姓名/部门图像、提交图像写入。  
下位机以OK或ERR开头回复执行结果。

## 上位机阶段二：考勤管理（扩展加分）

<!-- image-->

扩展加分项：接收下位机 WiFi 上传的考勤记录，实现存储、查询、统计与设备管理

<table><tr><td rowspan=1 colspan=1>编号</td><td rowspan=1 colspan=1>功能</td><td rowspan=1 colspan=1>说明</td></tr><tr><td rowspan=1 colspan=1>A2.1</td><td rowspan=1 colspan=1>TCP Server</td><td rowspan=1 colspan=1>监听固定端口，接收下位机加密上传的连接与数据</td></tr><tr><td rowspan=1 colspan=1>A2.2</td><td rowspan=1 colspan=1>帧解密解析</td><td rowspan=1 colspan=1>使用AES解密上传帧，校验完整性，解析出设备编号与考勤记录</td></tr><tr><td rowspan=1 colspan=1>A2.3</td><td rowspan=1 colspan=1>时间校准</td><td rowspan=1 colspan=1>利用记录中的时间偏差字段，将本地时间校准为统一服务器时间</td></tr><tr><td rowspan=1 colspan=1>A2.4</td><td rowspan=1 colspan=1>记录存储</td><td rowspan=1 colspan=1>将考勤记录持久化到数据库，保存原始时间与校准后统一时间</td></tr><tr><td rowspan=1 colspan=1>A2.5</td><td rowspan=1 colspan=1>记录查询</td><td rowspan=1 colspan=1>按时间范围、工号、姓名、部门等条件查询并展示</td></tr><tr><td rowspan=1 colspan=1>A2.6</td><td rowspan=1 colspan=1>人员管理</td><td rowspan=1 colspan=1>增删改查人员信息，支持挂失与补卡</td></tr><tr><td rowspan=1 colspan=1>A2.7</td><td rowspan=1 colspan=1>设备状态</td><td rowspan=1 colspan=1>显示各下位机在线状态、最后通信时间</td></tr><tr><td rowspan=1 colspan=1>A2.8</td><td rowspan=1 colspan=1>考勤统计</td><td rowspan=1 colspan=1>统计出勤、迟到、早退等，支持导出报表 (可选)</td></tr></table>

注意：上位机阶段二需与下位机阶段三配套验证，两者必须同时完成才能进行联调测试。测评时优良成绩指标至少实现A1.8/A2.6、A2.5、A2.4功能。

## 阶段配套关系

下位机（STM32）

## 上位机（PC）

阶段一：基础刷卡考勤

配套

阶段一：发卡工具

刷卡/ 显示/发卡/校时

人员录入/图像处理/发卡/清卡

可独立验证 ←两套均可各自独立运行和测试

阶段二：考勤逻辑 + 存储

考勤模式/ 记录存储/管理员设置

可独立验证 ← 通过串口LIST 命令查看记录，无需上位机配合

阶段三：联网通信

配套

阶段二：考勤管理

WiFi 上传 / 加密 / NTP 校时 / 天气

TCP接收/ 解密/存储/查询

需互相配合验证 ← 下位机需上位机配合验证，上位机需下位机配合验证

## 03

## 验收标准与评分

必做内容验收标准、扩展加分项与评分说明

## 实物验收考核标准

<table><tr><td rowspan=1 colspan=1>验收得分</td><td rowspan=2 colspan=1>验收标准上位机阶段一、下位机阶段一、二全部完成，且UI界面美观，操作流畅，工作稳定。上位机阶段二和下位机阶段三功能至少完成一半，功能演示良好。能清晰地回答教师现场对设计相关的软硬件提问。</td></tr><tr><td rowspan=1 colspan=1>优</td></tr><tr><td rowspan=1 colspan=1>良</td><td rowspan=1 colspan=1>上位机阶段一、下位机阶段一、二全部完成，操作流畅，工作稳定。扩展功能完成至少完成基本要求，功能演示良好。能清晰地回答教师现场对设计相关的软硬件提问。</td></tr><tr><td rowspan=1 colspan=1>中</td><td rowspan=1 colspan=1>上位机阶段一、下位机阶段一、二基本完成，至少能演示上位机发卡、下位机刷卡考勤功能。扩展功能完成2个以上。能正确回答教师现场对设计相关的软硬件提问。</td></tr><tr><td rowspan=1 colspan=1>及格</td><td rowspan=1 colspan=1>下位机阶段一、二基本完成，至少能演示下位机刷卡考勤功能。扩展功能完成1个以上。回答教师提问基本正确。</td></tr></table>

注1：考核时需提供代码，现场下载、测试，单片机程序功能必须集中在一个程序内，不可分割多个程序进行测试（创新功能可以单独测试）。  
注2：考核结束，实物材料收回。若有损坏的传感器、OLED和WiFi等模块，请尽快到淘宝买好替换。

警告：课程有配套下发的IC卡，不要用学校饭卡进行实验，写卡有风险，使用饭卡出现任何意外概不负责。

## 04

## 技术方案与资源

系统软件架构、推荐技术栈与开发资源

## 系统软件推荐方案

## 下位机技术栈

<table><tr><td rowspan=1 colspan=1>层级</td><td rowspan=1 colspan=1>方案</td><td rowspan=1 colspan=1>说明</td></tr><tr><td rowspan=1 colspan=1>硬件抽象</td><td rowspan=1 colspan=1>STM32 HAL库</td><td rowspan=1 colspan=1>CubeMX生成标准外设驱动</td></tr><tr><td rowspan=1 colspan=1>RTOS</td><td rowspan=1 colspan=1>FreeRTOS + CMSIS-RTOS v2</td><td rowspan=1 colspan=1>已配置，关注任务划分与同步</td></tr><tr><td rowspan=1 colspan=1>任务通信</td><td rowspan=1 colspan=1>任务通知+消息队列</td><td rowspan=1 colspan=1>nfcTask与 guiTask 事件传递</td></tr><tr><td rowspan=1 colspan=1>存储介质</td><td rowspan=1 colspan=1>SPI NOR Flash (W25Qxx)</td><td rowspan=1 colspan=1>自定义循环日志管理层</td></tr><tr><td rowspan=1 colspan=1>发卡通信</td><td rowspan=1 colspan=1>串口文本帧</td><td rowspan=1 colspan=1>ASCI 便于调试</td></tr><tr><td rowspan=1 colspan=1>上传通信</td><td rowspan=1 colspan=1>TCP二进制帧</td><td rowspan=1 colspan=1>加密帧保证安全 (扩展)</td></tr></table>

## 上位机技术栈

<table><tr><td rowspan=1 colspan=1>组件</td><td rowspan=1 colspan=1>推荐方案</td><td rowspan=1 colspan=1>用途</td></tr><tr><td rowspan=1 colspan=1>开发语言</td><td rowspan=1 colspan=1>C++ (Qt)</td><td rowspan=1 colspan=1>参考程序 DXQCard 基于 Qt/C++</td></tr><tr><td rowspan=1 colspan=1>GUI框架</td><td rowspan=1 colspan=1>Qt5 (Qt Creator)</td><td rowspan=1 colspan=1>跨平台桌面应用框架</td></tr><tr><td rowspan=1 colspan=1>串口通信</td><td rowspan=1 colspan=1>QSerialPort</td><td rowspan=1 colspan=1>USB 串口与下位机收发数据</td></tr><tr><td rowspan=1 colspan=1>图像处理</td><td rowspan=1 colspan=1>Qlmage/ PhotoShop</td><td rowspan=1 colspan=1>缩放、灰度化、二值化、位图渲染</td></tr><tr><td rowspan=1 colspan=1>数据存储</td><td rowspan=1 colspan=1>SQLite (QtSql)</td><td rowspan=1 colspan=1>人员信息与发卡记录持久化</td></tr><tr><td rowspan=1 colspan=1>加密解密</td><td rowspan=1 colspan=1>OpenSSL / QCryp..Hash</td><td rowspan=1 colspan=1>AES 解密上传帧 (扩展)</td></tr></table>

## 软件架构要点

<!-- image-->

## 分层解耦

驱动层(BSP) →中间层(存储/通信)→应用层(考勤/显示)

<!-- image-->

## 任务分离

刷卡/显示/按键/串口/维护分别由独立任务承 担

<!-- image-->

## 显示与检测分离

读卡任务不直接操作OLED，通过事件通知给显示任务

<!-- image-->

## 存储原子性

主区+备份区双冗余策略，先写备份再擦写主区

## 数据格式与通信协议

## 卡片数据布局设计

Mifare S50 卡片共 16 个扇区，每个扇区 4 个块（每块 16 字节），每个扇区最后一个块禁止写入。以下是本项目推荐的卡片数据存储布局：

<table><tr><td rowspan=2 colspan=1>扇区</td><td rowspan=2 colspan=1>块号</td><td rowspan=2 colspan=1>存储内容</td><td></td></tr><tr><td rowspan=2 colspan=1>说明CID卡号、SID工号、PTS积分（预留）、卡类别、卡状态（预留）、校验码</td></tr><tr><td rowspan=1 colspan=1>扇区0</td><td rowspan=1 colspan=1>块1</td><td rowspan=1 colspan=1>账户头</td></tr><tr><td rowspan=1 colspan=1>扇区1~8</td><td rowspan=1 colspan=1>块0~2</td><td rowspan=1 colspan=1>头像图像</td><td rowspan=2 colspan=1>48x64 二值化图像数据 (共384 字节)姓名80x16+部门80x16单色位图 (共240字节)</td></tr><tr><td rowspan=1 colspan=1>扇区9~13</td><td rowspan=1 colspan=1>块0~2</td><td rowspan=1 colspan=1>姓名+部门图像</td></tr></table>

串口通信协议（上位机下发）

<table><tr><td>命令</td><td>帧格式</td><td>参数说明</td><td>功能</td></tr><tr><td>READ</td><td> READ\n</td><td>无</td><td>读取当前感应区卡片信息</td></tr><tr><td>ISSUE</td><td>ISSUE:CID_HEX,SID_DEC,PTS,CTYPE\n</td><td>CID_HEX:8 字符大写 HEX（4 字节 UID）；SID_DEC:十进制工号；PTS:积 分（默认0）；CTYPE:0=普通，1=图像，2=管理员</td><td>写账户头到卡片</td></tr><tr><td>CLEAR</td><td>CLEAR:UID_HEX\n</td><td>UID_HEX: 8字符大写 HEX</td><td>清空整张卡片数据</td></tr><tr><td> IMGAxX</td><td> IMGAxx:HEX32\n</td><td> xx: 00~23； HEX32: 32个 HEX字符 (16 字节)</td><td>下发头像图像块 (共24块)</td></tr><tr><td>IMGNxX</td><td>IMGNxx:HEX32\n</td><td> xx: 00~09；HEX32:32个 HEX 字符 (16字节)</td><td>下发姓名图像块 (共10 块)</td></tr><tr><td> IMGDxx</td><td> IMGDxx:HEX32\n</td><td> xX: 00~09； HEX32: 32个 HEX字符 (16字节)</td><td>下发部门图像块 (共10 块)</td></tr><tr><td> UPDATEIMG</td><td> UPDATEIMG\n</td><td>无</td><td>将缓存的44块图像数据一次性写入卡片</td></tr><tr><td>LIST</td><td> LIST:ALL\n 或 LIST:N\n</td><td>ALL:全部记录；N:最近 N条</td><td>查询考勤记录</td></tr></table>

串口通信协议（下位机上传）
<table><tr><td rowspan=1 colspan=1>应答类型</td><td rowspan=1 colspan=1>帧格式</td><td rowspan=1 colspan=1>含义</td></tr><tr><td rowspan=1 colspan=1>成功</td><td rowspan=1 colspan=1>OK\n</td><td rowspan=1 colspan=1>命令执行成功</td></tr><tr><td rowspan=1 colspan=1>参数错误</td><td rowspan=1 colspan=1>ERR:PARSE\n</td><td rowspan=1 colspan=1>参数解析失败</td></tr><tr><td rowspan=1 colspan=1>无卡</td><td rowspan=1 colspan=1> ERR:NOCARD\n</td><td rowspan=1 colspan=1>感应区内无卡片</td></tr><tr><td rowspan=1 colspan=1>卡号不匹配</td><td rowspan=1 colspan=1> ERR:CID_MISMATCH\n</td><td rowspan=1 colspan=1>命令 UID 与实际读到的 UID 不一致</td></tr><tr><td rowspan=1 colspan=1>认证失败</td><td rowspan=1 colspan=1> ERR:AUTH\n</td><td rowspan=1 colspan=1>RC522 扇区密钥认证失败</td></tr><tr><td rowspan=1 colspan=1>写入失败</td><td rowspan=1 colspan=1> ERR:WRITE_B1\n</td><td rowspan=1 colspan=1>写入块1 （账户头）失败</td></tr><tr><td rowspan=1 colspan=1>图像解析错误</td><td rowspan=1 colspan=1>ERR:IMG_PARSE\n</td><td rowspan=1 colspan=1>图像块命令格式错误</td></tr><tr><td rowspan=1 colspan=1>HEX数据错误</td><td rowspan=1 colspan=1> ERR:IMG_HEX\n</td><td rowspan=1 colspan=1>图像块 HEX数据长度或格式不对</td></tr><tr><td rowspan=1 colspan=1>块索引错误</td><td rowspan=1 colspan=1>ERR:IMG_BLOCK\n</td><td rowspan=1 colspan=1>图像块序号越界</td></tr><tr><td rowspan=1 colspan=1>图像类型错误</td><td rowspan=1 colspan=1> ERR:IMG_TYPE\n</td><td rowspan=1 colspan=1>图像类型（A/N/D）无法识别</td></tr><tr><td rowspan=1 colspan=1>未知命令</td><td rowspan=1 colspan=1> ERR:UNKNOWN_CMD\n</td><td rowspan=1 colspan=1>收到未定义的命令</td></tr><tr><td rowspan=1 colspan=1>读卡UID</td><td rowspan=1 colspan=1> UID:XXXXXXX\n</td><td rowspan=1 colspan=1>返回8位大写 HEX卡号 (READ 响应）</td></tr><tr><td rowspan=1 colspan=1>读卡工号</td><td rowspan=1 colspan=1> SID:XXXXX\n</td><td rowspan=1 colspan=1>返回十进制工号 (READ 响应)</td></tr><tr><td rowspan=1 colspan=1>读卡积分</td><td rowspan=1 colspan=1>PTs:XXXXIn</td><td rowspan=1 colspan=1>返回十进制积分 (READ 响应)</td></tr><tr><td rowspan=1 colspan=1>读卡类型</td><td rowspan=1 colspan=1>TYPE:X\n</td><td rowspan=1 colspan=1>返回卡类型：0=普通，1=图像，2=管理员 (READ 响应)</td></tr><tr><td rowspan=1 colspan=1>记录条目</td><td rowspan=1 colspan=1> REC:SEQ=N/UID=XXXXXXX|SID=XXXXXITYPE[YYYY-MM-DD HH:MM:SS|DEV=N|STATUS\n</td><td rowspan=1 colspan=1>单条考勤记录 (LIST响应)</td></tr><tr><td rowspan=1 colspan=1>记录数量</td><td rowspan=1 colspan=1>LIST:N\n</td><td rowspan=1 colspan=1>返回记录总数 (LIST响应头)</td></tr><tr><td rowspan=1 colspan=1>列表结束</td><td rowspan=1 colspan=1> LIST:END\n</td><td rowspan=1 colspan=1>记录列表发送完毕 (LIST响应尾)</td></tr></table>

## 典型发卡流程

<table><tr><td rowspan=1 colspan=1>步骤</td><td rowspan=1 colspan=1>上位机发送</td><td rowspan=1 colspan=1>下位机动作</td><td rowspan=1 colspan=1>下位机应答</td></tr><tr><td rowspan=1 colspan=1>1</td><td rowspan=1 colspan=1> ISSUE:CID_HEX,SID,0,CTYPE\n</td><td rowspan=1 colspan=1>写账户头到 Block 1</td><td rowspan=1 colspan=1>OK\n</td></tr><tr><td rowspan=1 colspan=1>2</td><td rowspan=1 colspan=1> IMGA00:HEX32\n</td><td rowspan=1 colspan=1>缓存头像第0块</td><td rowspan=1 colspan=1>OK\n</td></tr><tr><td rowspan=1 colspan=1>3</td><td rowspan=1 colspan=1> (共24块)</td><td rowspan=1 colspan=1>：·</td><td rowspan=1 colspan=1>OK\n</td></tr><tr><td rowspan=1 colspan=1>4</td><td rowspan=1 colspan=1>IMGN00:HEX32\n</td><td rowspan=1 colspan=1>缓存姓名图像第0块</td><td rowspan=1 colspan=1>OK\n</td></tr><tr><td rowspan=1 colspan=1>5</td><td rowspan=1 colspan=1> (共10块)</td><td rowspan=1 colspan=1></td><td rowspan=1 colspan=1>OK\n</td></tr><tr><td rowspan=1 colspan=1>6</td><td rowspan=1 colspan=1>IMGD00:HEX32\n</td><td rowspan=1 colspan=1>缓存部门图像第0块</td><td rowspan=1 colspan=1> OK\n</td></tr><tr><td rowspan=1 colspan=1>7</td><td rowspan=1 colspan=1>： (共10块)</td><td rowspan=1 colspan=1>：</td><td rowspan=1 colspan=1>OK\n</td></tr><tr><td rowspan=1 colspan=1>8</td><td rowspan=1 colspan=1> UPDATEIMG\n</td><td rowspan=1 colspan=1>将缓存的 44 块写入卡片</td><td rowspan=1 colspan=1>OK\n</td></tr><tr><td rowspan=1 colspan=1>9</td><td rowspan=1 colspan=1>READ\n</td><td rowspan=1 colspan=1>读卡验证</td><td rowspan=1 colspan=1> UID / SID / TYPE</td></tr></table>