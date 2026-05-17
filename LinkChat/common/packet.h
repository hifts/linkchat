#ifndef PACKET_H
#define PACKET_H

#include <QByteArray>
#include <cstdint>
#include <cstring>

/*
 * 协议类型 (Protocol Types)
 * 定义了客户端与服务端通信时各种业务的消息类型，如心跳、登录、聊天、添加好友、文件传输等请求和响应。
 */
enum MessageType{
    MSG_UNDEFINED = 0,               // 未定义类型
    MSG_HEARTBEAT_REQ,               // 心跳包请求 (维持长连接保活)
    MSG_HEARTBEAT_RESP,              // 心跳包响应
    MSG_REGISTER_REQ,                // 账号注册请求
    MSG_REGISTER_RESP,               // 账号注册响应
    MSG_LOGIN_REQ,                   // 登录请求
    MSG_LOGIN_SALT_REQ,              // 登录获取盐值请求 (用于密码安全传输)
    MSG_LOGIN_SALT_RESP,             // 登录获取盐值响应
    MSG_LOGIN_RESP,                  // 登录响应 (返回登录结果及用户ID)
    MSG_FRIEND_LIST_REQ,             // 获取好友列表请求
    MSG_FRIEND_LIST_RESP,            // 获取好友列表响应
    MSG_FRIEND_STATUS_NOTIFY,        // 好友状态变更通知 (上下线状态更新)
    MSG_CHAT_HISTORY_REQ,            // 单聊历史记录请求
    MSG_CHAT_HISTORY_RESP,           // 单聊历史记录响应
    MSG_SEARCH_USER_REQ,             // 搜索用户请求 (通过用户名/ID查找)
    MSG_SEARCH_USER_RESP,            // 搜索用户响应
    MSG_ADD_FRIEND_REQ,              // 添加好友请求 (发起方发送)
    MSG_ADD_FRIEND_NOTIFY,           // 添加好友通知 (服务端推送给被添加方)
    MSG_ADD_FRIEND_RESP,             // 添加好友响应 (被加方处理后回复)
    MSG_ADD_FRIEND_RESULT,           // 添加好友最终结果通知
    MSG_DELETE_FRIEND_REQ,           // 删除好友请求
    MSG_DELETE_FRIEND_RESP,          // 删除好友响应
    MSG_CHAT_TEXT,                   // 单聊消息 (包含文本、图片、发送文件)
    MSG_FILE_TRANSFER_REQ,           // 文件传输请求 (握手)
    MSG_FILE_TRANSFER_RESP,          // 文件传输响应 (同意或拒绝)
    MSG_FILE_CHUNK,                  // 文件传输数据分片/块 (传输实际数据)
    MSG_FILE_TRANSFER_ACK,           // 文件块传输接收确认 (ACK)
    MSG_FILE_TRANSFER_CANCEL,        // 文件传输取消/中断
    MSG_FILE_RESUME_REQ,             // 文件断点续传请求
    MSG_FILE_RESUME_RESP,            // 文件断点续传响应进度
    MSG_FILE_VERIFY_REQ,             // 文件校验请求 (传输完成后MD5校验)
    MSG_FILE_VERIFY_RESP,            // 文件校验结果响应
    MSG_CREATE_GROUP_REQ,            // 创建群组请求
    MSG_CREATE_GROUP_RESP,           // 创建群组响应
    MSG_GROUP_LIST_REQ,              // 获取群组列表请求
    MSG_GROUP_LIST_RESP,             // 获取群组列表响应
    MSG_GROUP_MEMBER_LIST_REQ,       // 获取群成员列表请求
    MSG_GROUP_MEMBER_LIST_RESP,      // 获取群成员列表响应
    MSG_INVITE_TO_GROUP_REQ,         // 邀请加入群组请求
    MSG_INVITE_TO_GROUP_NOTIFY,      // 邀请加入群组通知 (推送给被邀请者)
    MSG_LEAVE_GROUP_REQ,             // 主动退出群组请求
    MSG_LEAVE_GROUP_RESP,            // 主动退出群组响应
    MSG_GROUP_CHAT_TEXT,             // 群聊消息 (包含文本、图片等)
    MSG_GROUP_CHAT_HISTORY_REQ,      // 群聊历史记录请求
    MSG_GROUP_CHAT_HISTORY_RESP,     // 群聊历史记录响应
    MSG_OFFLINE_CHAT_TEXT,           // 私聊离线消息投递
    MSG_GROUP_OFFLINE_CHAT_TEXT,     // 群聊离线消息投递
    MSG_OFFLINE_MSG_ACK,             // 私聊离线消息投递确认
    MSG_GROUP_OFFLINE_MSG_ACK        // 群聊离线消息投递确认
};

constexpr uint32_t MSG_GROUP_MSG_DELIVERED_ACK = static_cast<uint32_t>(MSG_GROUP_OFFLINE_MSG_ACK) + 1;
constexpr uint32_t MSG_GROUP_MSG_READ_ACK = static_cast<uint32_t>(MSG_GROUP_OFFLINE_MSG_ACK) + 2;

constexpr uint32_t PDU_MAGIC = 0xABCD1234;

/*
 * 包类型/数据包头部结构 (Packet Header Structure)
 * 采用类似 TLV (Type-Length-Value) 的设计：
 * 完整的网络数据包 = PDUHeader (固定包头) + 具体业务数据载荷 (包体 Body)
 */
#pragma pack(push,1)
struct PDUHeader
{
    uint32_t magic;     // 协议魔数，检验数据包合法性，辅助处理粘包/半包
    uint32_t total_len; // 数据包总长度 (PDUHeader包头长度 + 包体长度)
    uint32_t msg_type;  // 具体的消息类型，对应 MessageType 枚举
    uint32_t dest_id;   // 目标用户ID (如接收方，0可代表服务端或广播)
    uint32_t src_id;    // 发送方用户ID
};
#pragma pack(pop)

/*
 * 聊天消息载荷的子包类型
 */
enum ChatSubType : char {
    SUB_TEXT  = 0, // 文本消息包
    SUB_IMAGE = 1, // 图片消息包
    SUB_FILE = 2   // 文件消息包
};

const char ENCRYPTED_FLAG = (char)0x80;

inline bool isSubTypeEncrypted(char subType) {
    return (subType & ENCRYPTED_FLAG) != 0;
}

inline char getOriginalSubType(char subType) {
    return subType & ~ENCRYPTED_FLAG;
}

inline char addEncryptedFlag(char subType) {
    return subType | ENCRYPTED_FLAG;
}

// ==================== 基础与认证协议结构体 ====================

// 心跳包载荷：定时发送，携带客户端当前时间戳协助检查网络延迟与连接保活
struct HeartbeatPacket {
    uint64_t timestamp;
};

#pragma pack(push,1)

// 登录请求包载荷：包含用户名和由于安全加密生成的密码哈希
struct LoginReq
{
    char userName[32];
    char passwordHash[64];
};

// 获取登录安全盐值请求：登录两段式认证的第一步
struct LoginSaltReq
{
    char userName[32];
};

// 获取登录安全盐值响应：服务端返回给用户的唯一防重放盐值
struct LoginSaltResp
{
    int result;
    char salt[64];
};

// 注册请求包载荷：注册账号所需的用户名、哈希处理后的密码及使用的加密盐值
struct RegisterReq
{
    char userName[32];
    char passwordHash[64];
    char salt[64];
};

// 登录响应包载荷：返回登录成功与否的结果状态及服务端分配的全局User ID
struct LoginResp
{
    int result;
    int userId;
};

// ==================== 好友与用户关系结构体 ====================

// 好友信息数据元：用于好友列表返回时的单个好友基本档案
struct FriendInfo
{
    int id;
    char userName[32];
    int status;
    qint64 lastMsgTime;
};

// 用户搜索请求包：通过填入用户名或账号关键字来模糊匹配用户
struct SearchReq {
    char keyword[32];
};

struct SearchUserBatchHeader {
    quint32 requestId;
    quint32 offset;
    quint32 count;
    quint8 hasMore;
};

// 好友在线状态更新通知：服务端推送好友上下线变化
struct FriendStatusChange {
    int uid;
    int status;
};

// 发起添加好友请求包：携带需要添加的目标用户ID
struct AddFriendReq {
    int targetId;
};

// 添加好友服务端通知包：服务端主动推给被加方的申请消息
struct AddFriendNotify {
    int requesterId;
    char requesterName[32];
};

// 处理添加好友确认响应包：被加方同意或拒绝后发送给服务端
struct AddFriendResp {
    int requesterId;
    bool accepted;
};

// 删除好友请求包
struct DeleteFriendReq {
    int targetId;
};

// 删除好友动作回调响应包
struct DeleteFriendResp {
    int result;
    int targetId;
};

// ==================== 文件传输(P2P业务扩展)结构体 ====================

// 发起大文件传输请求：握手阶段，向对方告知文件名、总大小、切片总数和专属File ID
struct FileTransferReq {
    char fileName[256];
    quint64 fileSize;
    quint32 totalChunks;
    char fileId[64];
};

// 接收方文件传输决策响应：返回是否同意接收该文件
struct FileTransferResp {
    char fileId[64];
    quint8 accepted;
};

// 文件实际数据载荷分片元组：大文件拆分成此块状进行连续传输
struct FileChunk {
    char fileId[64];
    quint32 chunkIndex;
    quint32 chunkSize;
};

// 文件分片抵达确应包：每接受一定的数据块，接收方可返回ACK确认接收进度
struct FileTransferAck {
    char fileId[64];
    quint32 chunkIndex;
};

// 中断/取消文件传输通知：通信双方任一方取消传输将发送此指令及原因
struct FileTransferCancel {
    char fileId[64];
    quint8 reason;
};

// 请求断点续传指令：在未完成传输后重新链接请求恢复传输某个File ID对应的文件
struct FileResumeReq {
    char fileId[64];
};

// 断点续传参数响应：返回接收方目前持有了多少分片以此来决定从哪个Chunk续传
struct FileResumeResp {
    char fileId[64];
    quint8 canResume;
    quint32 totalChunks;
    quint32 receivedChunks;
};

// 文件防篡改完整性校验请求：文件全部传完后，校验文件全体数据的MD5值
struct FileVerifyReq {
    char fileId[64];
    char fileMD5[33];
};

// 文件防篡改完整性校验响应：返回MD5是否匹配，即文件在网络传输中是否损坏
struct FileVerifyResp {
    char fileId[64];
    quint8 verified;
};

struct OfflineMsgAck {
    quint64 offlineMsgId;
};

struct GroupMsgCursorAck {
    int groupId;
    quint64 messageId;
};

// ==================== 群组与多人聊天结构体 ====================

// 创建新群请求
struct CreateGroupReq {
    char groupName[64];
};

// 创建新群响应：由服务端分配一个新的Group ID给客户端
struct CreateGroupResp {
    int result;
    int groupId;
};

// 群组信息数据元：常用于组织并显示用户的群联系人列表
struct GroupInfo {
    int groupId;
    char groupName[64];
    int memberCount;
    char creatorName[32];
    qint64 lastMsgTime;
};

// 群成员详细信息数据元：在打开群聊面板刷新成员时用到的角色分配状态
struct GroupMemberInfo {
    int userId;
    char userName[32];
    int role;
    int status;
};

// 拉人入群请求：选定特定目标拉进指定的群聊
struct InviteToGroupReq {
    int groupId;
    int targetUserId;
};

// 进群服务端推送通知：服务端发给新组员自己被谁拉入了某个群聊的安全提醒
struct InviteToGroupNotify {
    int groupId;
    char groupName[64];
    int inviterId;
    char inviterName[32];
};

// 主动退出群聊的退出请求指令
struct LeaveGroupReq {
    int groupId;
};

// 退出群聊是否完成的业务层响应
struct LeaveGroupResp {
    int result;
    int groupId;
};

// 多人聊天消息包裹的外发壳：附带真实发送者的信息以及群ID路由信息
struct GroupChatMessage {
    int groupId;
    int senderId;
    char senderName[32];
};

// 获取远端群聊历史记录的游标及历史返回外壳
struct GroupChatHistoryItem {
    int senderId;
    char senderName[32];
    int contentLen;
};
#pragma pack(pop)

QByteArray makePacket(uint32_t type, const QByteArray& body, uint32_t src = 0, uint32_t dest = 0);

#endif // PACKET_H

