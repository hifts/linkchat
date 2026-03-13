# LinkChat (QT-C/S 即时通讯应用)

LinkChat 是一个基于 `C++17` 和 `Qt` 开发的 C/S 架构即时通讯（IM）系统。该项目拥有完整的客户端和服务端，利用 `TCP` 持久连接和**自定义封装 PDU 协议**实现网络通信，依赖 MySQL 数据库进行状态和信息的持久化。为了提供类似现代 IM 软件的用户体验，客户端部分大量引入了诸如聊天气泡 UI、智能断线重连机制、大文件分片断点续传等高级功能。

## ✨ 核心特性

### 🔐 账户与安全认证
* **安全登录注册**：防彩虹表攻击，登录注册采用两段式校验，通过 `用户名 + 随机盐（Salt） + SHA-256` 保护用户密码安全传输。

### 💬 聊天与社群功能
* **好友系统**：支持全网根据用户名或用户 ID 模糊搜索添加好友，支持双向删除逻辑。
* **实时状态推送**：基于 TCP 长连接的全双工特性，任何好友上线、下线动作都会实时在主界面列表进行刷新。
* **单聊与群聊**：
  * 支持普通文本与图片的发送。
  * 提供完善的**离线消息拉取**功能。
  * 群组管理支持创建群聊、邀请人员进群、主动退群。
* **气泡UI界面**：基于 Qt Model/View 机制及自定义绘图，手写渲染现代化的微信/QQ 级别的圆角聊天气泡 UI。

### 📁 强大的大文件传输引擎
* 抽离了专用的 `FileTransferManager` 及线程任务进行调度，不阻塞主聊天网络收发。
* **分片与组装**：避免大文件占用过大内存，采用按固定大小分片并行读取与传输。
* **MD5完整性校验**：所有文件流传输完成后自动校验 MD5，保证数据安全不损坏。
* **断点续传机制**：通过记录接收方偏移块数，可以在网络中断重启后无缝接续上次的文件传输进度。

### 📡 健壮的网络底座
* 独立的 `HeartbeatManager` 及 `ReconnectManager` 模块，拥有精细的状态机。
* 检测到网络闪断或超时，自动尝试递增延迟的并发重连。
* **粘包/半包处理方案**：通过读取自研定义的 `PDUHeader (TLV 模型)`：`魔法数字鉴权 | 包总长度 | 协议类型 | ...`，在接收缓存区循环拆包，确保无数据错乱。

---

## 🛠️ 项目技术栈

| 模块 | 技术栈 |
| :--- | :--- |
| **开发语言** | C++17 |
| **客户端前端** | Qt 5/6 (Qt Widgets, 自定义重绘组件) |
| **网络通信** | QTcpServer, QTcpSocket, 采用 `epoll/IOCP` 底层机制 |
| **序列化与协议** | 定制 TLV (Type-Length-Value) PDU 私有二进制协议 + 结构体对齐封装 |
| **服务端持久存储** | MySQL / MariaDB (通过 QSqlDatabase 连接) |
| **多线程处理** | QtConcurrent, QThread (网络IO/文件IO/UI解耦) |
| **构建系统** | CMake 3.14+ |

---

## 📂 项目结构概览

```text
LinkChat/
├── Client/                 # 客户端代码模块
│   ├── chat_ui/            # 聊天气泡UI相关组件
│   ├── networkmanager.cpp  # 网络收发及拆包粘包处理
│   ├── reconnectmanager.cpp# 网络断线重连状态机
│   ├── filetransfer*       # 大文件分片 / 断点续传逻辑核心模块
│   └── ...                 # 各类Dialog窗口逻辑
├── Server/                 # 服务端代码模块
│   ├── tcpserver.cpp       # QtcpServer 多路复用监听
│   ├── clientsocket.cpp    # Client会话层生命周期及信令分发
│   └── dbmanager.cpp       # 采用连接池理念操作 MySQL
├── common/                 # 客户端与服务端共享的 公用/协议 层
│   ├── packet.h            # 核心：几十种 MessageType 的通讯数据包定义
│   ├── encryptionmanager   # SHA-256 加解密组件
│   └── logger.h            # 统一轻量化应用层日志工具
├── CMakeLists.txt          # 主力工程构建文件
└── README.md
```

---

## 🚀 编译与运行部署

### 1. 环境准备
确保您的计算机上已经安装了：
* **CMake (>= 3.14)**
* **Qt 开发环境 (包含 Core, Gui, Widgets, Network, Sql 等依赖)**
* **MySQL** 服务端引擎。

### 2. 数据库拉起
在您的 MySQL 实例上新建一个 `linkchat` 数据库即可。服务端第一次启动时需要能正常连接上该数据库：

考虑到表结构的完整性与预置的测试数据，**我们已将数据库初始化脚本统一存放在项目根目录的 `sql.sql` 库脚本中**。

您只需在 MySQL 终端或管理工具（如 Navicat, DataGrip）中，一键导入并运行根目录下的 `sql.sql` 文件，即可直接完成 `linkchat` 数据库以及所有数据表的自动创建及预设测试人员配置。

### 3. 构建
进入项目根目录：
```bash
cmake -S . -B build
# 若未配环境变量，需指定 Qt 路径: -DCMAKE_PREFIX_PATH="C:/Qt/5.xx.x/mingwXX"
cmake --build build --config Release
```

### 4. 运行
生成产物位于 `build/bin/Release` 或 `build/bin`。
1. 首先执行 `./Server`。
2. 随后执行 `./Client`。

首次运行启动时，系统将在当前工作目录生成 JSON 配置文件（例如 `server_config.json` 和 `client_config.json`）。若您的数据库账号密码或端口不是 root / 3306，请打开 `server_config.json` 针对您的本机环境修改：

```json
"server": {
    "port": 8080,
    "database": {
        "host": "localhost",
        "port": 3306,
        "username": "root",
        "password": "your_password",
        "database": "linkchat"
    }
}
```
修改后重启 Server 即可。

---

## 📸 演示与截图 (Screenshots)

登录页面  
![登录页面](img/login.png)

注册页面  
![注册页面](img/register.png)

首页  
![创群页面](img/home.png)

聊天页面  
![新朋友页面](img/chatP.png)

群聊
![新朋友页面](img/chatG.png)

创群页面  
![聊天页面](img/group.png)

新朋友页面  

![首页](img/new-friends.png)

---

*Enjoy coding with Qt! 🚀*
