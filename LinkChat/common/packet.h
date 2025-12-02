#pragma once
#ifndef PACKET_H
#define PACKET_H

#include <QByteArray>
#include <cstdint>
#include <cstring>

enum MessageType{
    MSG_UNDEFINED = 0,              // 未定义消息类型
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
    // MSG_CHAT_IMAGE,              // 图片
    MSG_FILE_TRANSFER_REQ,          // 文件传输请求
    MSG_FILE_TRANSFER_RESP,         // 文件传输响应
    MSG_FILE_CHUNK,                 // 文件分片数据
    MSG_FILE_TRANSFER_ACK,          // 文件传输确认
    MSG_FILE_TRANSFER_CANCEL        // 取消文件传输
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

// 登录/注册 请求包
struct LoginReq
{
    char userName[32];      // 登录名
    char password[32];      // 登录密码
};

// 登录/注册 响应包
struct LoginResp
{
    int result;             // 0=失败，1=成功
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

QByteArray makePacket(uint32_t type, const QByteArray& body, uint32_t src = 0, uint32_t dest = 0);

#endif // PACKET_H
