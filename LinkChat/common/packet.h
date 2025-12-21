#pragma once
#ifndef PACKET_H
#define PACKET_H

#include <QByteArray>
#include <cstdint>
#include <cstring>

enum MessageType{
    MSG_UNDEFINED = 0,              // 未定义消息类型
    MSG_HEARTBEAT_REQ,              // 心跳请求
    MSG_HEARTBEAT_RESP,             // 心跳响应
    MSG_REGISTER_REQ,               // 注册请求
    MSG_REGISTER_RESP,              // 注册响应
    MSG_LOGIN_REQ,                  // 登录请求
    MSG_LOGIN_RESP,                 // 登录响应
    MSG_FRIEND_LIST_REQ,            // 好友列表请求
    MSG_FRIEND_LIST_RESP,           // 好友列表响应
    MSG_FRIEND_STATUS_NOTIFY,       // 好友状态通知
    MSG_CHAT_HISTORY_REQ,           // 历史消息请求
    MSG_CHAT_HISTORY_RESP,          // 历史消息响应
    MSG_SEARCH_USER_REQ,            // 搜索请求
    MSG_SEARCH_USER_RESP,           // 搜索响应
    MSG_ADD_FRIEND_REQ,             // 添加好友申请
    MSG_ADD_FRIEND_NOTIFY,          // 添加好友通知
    MSG_ADD_FRIEND_RESP,            // 处理好友回复（好友同意/拒绝）
    MSG_ADD_FRIEND_RESULT,          // 添加好友响应
    MSG_CHAT_TEXT,                  // 文本聊天
    MSG_FILE_TRANSFER_REQ,          // 文件传输请求
    MSG_FILE_TRANSFER_RESP,         // 文件传输响应
    MSG_FILE_CHUNK,                 // 文件分片数据
    MSG_FILE_TRANSFER_ACK,          // 文件传输确认
    MSG_FILE_TRANSFER_CANCEL,       // 取消文件传输

    // 断点续传协议
    MSG_FILE_RESUME_REQ,            // 恢复传输请求
    MSG_FILE_RESUME_RESP,           // 恢复传输响应
    MSG_FILE_VERIFY_REQ,            // 文件校验请求
    MSG_FILE_VERIFY_RESP,           // 文件校验响应

    // 群聊协议
    MSG_CREATE_GROUP_REQ,           // 创建群聊请求
    MSG_CREATE_GROUP_RESP,          // 创建群聊响应
    MSG_GROUP_LIST_REQ,             // 获取群列表请求
    MSG_GROUP_LIST_RESP,            // 获取群列表响应
    MSG_GROUP_MEMBER_LIST_REQ,      // 获取群成员列表请求
    MSG_GROUP_MEMBER_LIST_RESP,     // 获取群成员列表响应
    MSG_INVITE_TO_GROUP_REQ,        // 邀请加入群聊请求
    MSG_INVITE_TO_GROUP_NOTIFY,     // 邀请加入群聊通知
    MSG_REMOVE_FROM_GROUP_REQ,      // 移除群成员请求
    MSG_REMOVE_FROM_GROUP_NOTIFY,   // 移除群成员通知
    MSG_LEAVE_GROUP_REQ,            // 退出群聊请求
    MSG_GROUP_CHAT_TEXT,            // 群聊消息
    MSG_GROUP_CHAT_HISTORY_REQ,     // 群聊历史消息请求
    MSG_GROUP_CHAT_HISTORY_RESP     // 群聊历史消息响应
};

// 定义协议头部
#pragma pack(push,1)
struct PDUHeader
{
    uint32_t total_len;     // 整个数据包的长度（包括Header + Body）
    uint32_t msg_type;      // 消息类型
    uint32_t dest_id;       // 目标用户id
    uint32_t src_id;        // 发送用户的id
};
#pragma pack(pop)

// body 第一个字节表示消息子类型(文本消息/图片消息)
enum ChatSubType : char {
    SUB_TEXT  = 0,
    SUB_IMAGE = 1,
    SUB_FILE = 2
};

// 加密标记位（用于标识消息是否加密）
// 将子类型与此标记进行OR运算来标识加密消息
// 例如：SUB_TEXT | ENCRYPTED_FLAG = 0x80 表示加密的文本消息
const char ENCRYPTED_FLAG = (char)0x80;

// 辅助函数：检查子类型是否标记为加密
inline bool isSubTypeEncrypted(char subType) {
    return (subType & ENCRYPTED_FLAG) != 0;
}

// 辅助函数：获取原始子类型（去除加密标记）
inline char getOriginalSubType(char subType) {
    return subType & ~ENCRYPTED_FLAG;
}

// 辅助函数：添加加密标记到子类型
inline char addEncryptedFlag(char subType) {
    return subType | ENCRYPTED_FLAG;
}

// 心跳包结构
struct HeartbeatPacket {
    uint64_t timestamp;     // 时间戳
};

// 登录请求包
struct LoginReq
{
    char userName[32];      // 登录名
    char password[32];      // 登录密码
};

// 注册请求包
struct RegisterReq
{
    char userName[32];      // 用户名
    char passwordHash[64];  // Base64编码的密码哈希值
    char salt[64];          // Base64编码的盐值
};

// 登录/注册 响应包
struct LoginResp
{
    int result;             // 0=失败，1=成功,2=已在线
    int userId;             // 请求成功返回用户id
};

// 好友包
struct FriendInfo
{
    int id;                 // 好友id
    char userName[32];      // 好友名称
    int status;             // 1=在线，0=离线
};

// 搜索请求
struct SearchReq {
    char keyword[32];
};

// 状态变更通知包
struct FriendStatusChange {
    int uid;                // 谁的状态变了
    int status;             // 变为什么 (1=上线, 0=离线)
};

// 添加好友请求包
struct AddFriendReq {
    int targetId;           // 我想加谁
    // char remark[32];     // 可选：附言，暂时留空
};

// 添加好友通知包
struct AddFriendNotify {
    int requesterId;            // 谁想加我
    char requesterName[32];
};

// 处理好友回复（好友同意/拒绝）包
struct AddFriendResp {
    int requesterId;
    bool accepted;      // true=同意，false=拒绝
};

// 文件传输请求结构
struct FileTransferReq {
    char fileName[256];      // 文件名
    quint64 fileSize;        // 文件大小
    quint32 totalChunks;     // 总分片数
    char fileId[64];         // 文件唯一标识(MD5或UUID)
};

// 文件传输响应结构
struct FileTransferResp {
    char fileId[64];         // 文件ID
    quint8 accepted;         // 是否接受 1=接受 0=拒绝
};

// 文件分片数据结构
struct FileChunk {
    char fileId[64];         // 文件ID
    quint32 chunkIndex;      // 当前分片索引
    quint32 chunkSize;       // 当前分片大小
    // 后面跟着实际的文件数据
};

// 文件传输确认结构
struct FileTransferAck {
    char fileId[64];         // 文件ID
    quint32 chunkIndex;      // 已接收的分片索引
};

// 文件传输取消结构
struct FileTransferCancel {
    char fileId[64];         // 文件ID
    quint8 reason;           // 取消原因 0=用户取消 1=错误
};

// 恢复传输请求结构
struct FileResumeReq {
    char fileId[64];         // 文件ID
};

// 恢复传输响应结构
struct FileResumeResp {
    char fileId[64];         // 文件ID
    quint8 canResume;        // 是否可以恢复 1=可以 0=不可以
    quint32 totalChunks;     // 总分片数
    quint32 receivedChunks;  // 已接收分片数
    // 后面跟着已接收分片的位图数据
    // 位图大小 = (totalChunks + 7) / 8 字节
};

// 文件校验请求结构
struct FileVerifyReq {
    char fileId[64];         // 文件ID
    char fileMD5[33];        // 文件MD5值
};

// 文件校验响应结构
struct FileVerifyResp {
    char fileId[64];         // 文件ID
    quint8 verified;         // 校验结果 1=成功 0=失败
};

// 创建群聊请求结构
struct CreateGroupReq {
    char groupName[64];     // 群名称
};

// 创建群聊响应结构
struct CreateGroupResp {
    int result;             // 0=失败，1=成功
    int groupId;            // 创建成功返回群ID
};

// 群信息结构
struct GroupInfo {
    int groupId;            // 群ID
    char groupName[64];     // 群名称
    int memberCount;        // 成员数量
    char creatorName[32];   // 创建者名称
};

// 群成员信息结构
struct GroupMemberInfo {
    int userId;             // 用户ID
    char userName[32];      // 用户名
    int role;               // 角色：0=普通成员，1=管理员，2=群主
    int status;             // 在线状态：0=离线，1=在线
};

// 邀请进群请求结构
struct InviteToGroupReq {
    int groupId;            // 群ID
    int targetUserId;       // 被邀请的用户ID
};

// 邀请进群通知结构
struct InviteToGroupNotify {
    int groupId;            // 群ID
    char groupName[64];     // 群名称
    int inviterId;          // 邀请人ID
    char inviterName[32];   // 邀请人名称
};

// 移除群成员请求结构
struct RemoveFromGroupReq {
    int groupId;            // 群ID
    int targetUserId;       // 被移除的用户ID
};

// 移除群成员通知结构
struct RemoveFromGroupNotify {
    int groupId;            // 群ID
    char groupName[64];     // 群名称
    int removedBy;          // 操作人ID
};

// 退出群聊请求结构
struct LeaveGroupReq {
    int groupId;            // 群ID
};

// 群聊消息结构（包含发送者信息，用于群聊显示发送者用户名）
struct GroupChatMessage {
    int groupId;            // 群ID
    int senderId;           // 发送者ID
    char senderName[32];    // 发送者用户名
    // 后面跟实际的消息内容 (subType + content)
};

// 群聊历史消息条目结构
struct GroupChatHistoryItem {
    int senderId;           // 发送者ID
    char senderName[32];    // 发送者用户名
    int contentLen;         // 内容长度
    // 后面跟实际的消息内容
};

QByteArray makePacket(uint32_t type, const QByteArray& body, uint32_t src = 0, uint32_t dest = 0);

#endif // PACKET_H

