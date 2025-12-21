#pragma once
#ifndef ENCRYPTIONMANAGER_H
#define ENCRYPTIONMANAGER_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QMap>
#include <QMutex>

/**
 * @brief EncryptionManager 加密管理器
 * 
 * 提供密码哈希、消息加密/解密、密钥生成和管理功能
 * 使用单例模式，确保全局只有一个实例
 */
class EncryptionManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 获取单例实例
     * @return EncryptionManager& 单例引用
     */
    static EncryptionManager& instance();

    // ========== 密码相关 ==========
    
    /**
     * @brief 生成随机盐值
     * @param length 盐值长度（字节），默认16字节
     * @return QByteArray 生成的随机盐值
     */
    QByteArray generateSalt(int length = 16);
    
    /**
     * @brief 对密码进行哈希处理
     * @param password 明文密码
     * @param salt 盐值
     * @return QByteArray SHA-256哈希值（32字节），失败返回空
     */
    QByteArray hashPassword(const QString& password, const QByteArray& salt);
    
    /**
     * @brief 验证密码是否正确
     * @param password 输入的明文密码
     * @param salt 存储的盐值
     * @param storedHash 存储的哈希值
     * @return bool 密码是否匹配
     */
    bool verifyPassword(const QString& password, const QByteArray& salt, 
                       const QByteArray& storedHash);

    // ========== 消息加密相关 ==========
    
    /**
     * @brief 生成私聊密钥（基于两个用户ID）
     * @param userId1 用户1的ID
     * @param userId2 用户2的ID
     * @return QByteArray 生成的密钥（32字节），失败返回空
     */
    QByteArray generateChatKey(int userId1, int userId2);
    
    /**
     * @brief 生成群聊密钥（基于群ID）
     * @param groupId 群ID
     * @return QByteArray 生成的密钥（32字节），失败返回空
     */
    QByteArray generateGroupKey(int groupId);
    
    /**
     * @brief XOR加密/解密（对称操作）
     * @param data 要加密/解密的数据
     * @param key 密钥
     * @return QByteArray 加密/解密后的数据，失败返回空
     */
    QByteArray xorEncryptDecrypt(const QByteArray& data, const QByteArray& key);
    
    // ========== 密钥缓存管理 ==========
    
    /**
     * @brief 获取缓存的私聊密钥（如果不存在则生成并缓存）
     * @param userId1 用户1的ID
     * @param userId2 用户2的ID
     * @return QByteArray 密钥（32字节），失败返回空
     */
    QByteArray getCachedChatKey(int userId1, int userId2);
    
    /**
     * @brief 获取缓存的群聊密钥（如果不存在则生成并缓存）
     * @param groupId 群ID
     * @return QByteArray 密钥（32字节），失败返回空
     */
    QByteArray getCachedGroupKey(int groupId);
    
    /**
     * @brief 清除所有缓存的密钥（用户登出时调用）
     */
    void clearKeyCache();
    
    /**
     * @brief 检测数据是否是有效的图片格式
     * @param data 要检测的数据
     * @return bool true表示是有效的图片格式（JPEG、PNG、GIF、BMP）
     * 
     * 通过检查文件头魔数来判断是否是有效的图片格式
     */
    bool isValidImageFormat(const QByteArray& data);

signals:
    /**
     * @brief 加密错误信号
     * @param errorMessage 错误消息
     */
    void encryptionError(const QString& errorMessage);
    
    /**
     * @brief 密钥生成错误信号
     * @param errorType 错误类型：1=私聊密钥失败, 2=群聊密钥失败
     * @param errorMessage 错误消息
     */
    void keyGenerationError(int errorType, const QString& errorMessage);
    
    /**
     * @brief 加密操作错误信号
     * @param errorMessage 错误消息
     */
    void encryptionOperationError(const QString& errorMessage);
    
    /**
     * @brief 解密操作错误信号
     * @param errorMessage 错误消息
     */
    void decryptionOperationError(const QString& errorMessage);

private:
    /**
     * @brief 私有构造函数（单例模式）
     * @param parent 父对象
     */
    explicit EncryptionManager(QObject* parent = nullptr);
    
    /**
     * @brief 禁用拷贝构造函数
     */
    EncryptionManager(const EncryptionManager&) = delete;
    
    /**
     * @brief 禁用赋值操作符
     */
    EncryptionManager& operator=(const EncryptionManager&) = delete;
    
    // 密钥缓存：键为"chat_{min_id}_{max_id}"或"group_{group_id}"
    QMap<QString, QByteArray> m_keyCache;
    
    // 保护密钥缓存的互斥锁
    QMutex m_cacheMutex;
};

#endif // ENCRYPTIONMANAGER_H
