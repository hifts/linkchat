#include "encryptionmanager.h"
#include "logger.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QMutexLocker>
#include <QThread>
#include <QtGlobal>
#include <cmath>

EncryptionManager& EncryptionManager::instance()
{
    static EncryptionManager instance;
    return instance;
}

EncryptionManager::EncryptionManager(QObject* parent)
    : QObject(parent)
{
    // EncryptionManager initialized
}

// ========== 密码相关 ==========

QByteArray EncryptionManager::generateSalt(int length)
{
    if (length <= 0) {
        LOG_WARN(QString("[Encryption] Invalid salt length: %1").arg(length));
        return QByteArray();
    }
    
    QByteArray salt;
    salt.reserve(length);
    
    // 使用QRandomGenerator生成随机字节
    for (int i = 0; i < length; ++i) {
        salt.append(static_cast<char>(QRandomGenerator::global()->bounded(256)));
    }
    
    return salt;
}

QByteArray EncryptionManager::hashPassword(const QString& password, const QByteArray& salt)
{
    if (password.isEmpty()) {
        LOG_WARN("[Encryption] Empty password for hashing");
        return QByteArray();
    }
    
    if (salt.isEmpty()) {
        LOG_WARN("[Encryption] Empty salt for hashing");
        return QByteArray();
    }
    
    // 组合密码和盐值
    QByteArray combined = password.toUtf8() + salt;
    
    // 使用SHA-256进行哈希
    QByteArray hash = QCryptographicHash::hash(combined, QCryptographicHash::Sha256);
    
    if (hash.isEmpty()) {
        LOG_ERROR("[Encryption] Hash generation failed");
        return QByteArray();
    }
    
    return hash;
}

bool EncryptionManager::verifyPassword(const QString& password, 
                                      const QByteArray& salt,
                                      const QByteArray& storedHash)
{
    if (password.isEmpty() || salt.isEmpty() || storedHash.isEmpty()) {
        LOG_WARN("[Encryption] Empty parameters for password verification");
        return false;
    }
    
    // 对输入密码进行哈希
    QByteArray computedHash = hashPassword(password, salt);
    
    if (computedHash.isEmpty()) {
        LOG_ERROR("[Encryption] Failed to compute hash for verification");
        return false;
    }
    
    // 比对哈希值
    bool match = (computedHash == storedHash);
    
    return match;
}

// ========== 消息加密相关 ==========

QByteArray EncryptionManager::generateChatKey(int userId1, int userId2)
{
    if (userId1 <= 0 || userId2 <= 0) {
        QString errorMsg = QString("无效的用户ID: %1, %2").arg(userId1).arg(userId2);
        LOG_WARN(QString("[Encryption] %1").arg(errorMsg));
        emit keyGenerationError(1, errorMsg);
        return QByteArray();
    }
    
    if (userId1 == userId2) {
        QString errorMsg = QString("无法为同一用户生成密钥: %1").arg(userId1);
        LOG_WARN(QString("[Encryption] %1").arg(errorMsg));
        emit keyGenerationError(1, errorMsg);
        return QByteArray();
    }
    
    // 确保密钥生成的确定性：较小ID在前
    int minId = qMin(userId1, userId2);
    int maxId = qMax(userId1, userId2);
    
    // 组合ID字符串
    QString combined = QString("%1_%2").arg(minId).arg(maxId);
    
    // SHA-256哈希生成32字节密钥
    QByteArray key = QCryptographicHash::hash(combined.toUtf8(), 
                                             QCryptographicHash::Sha256);
    
    if (key.isEmpty()) {
        QString errorMsg = QString("私聊密钥生成失败 (用户 %1 和 %2)").arg(userId1).arg(userId2);
        LOG_ERROR(QString("[Encryption] %1").arg(errorMsg));
        emit keyGenerationError(1, errorMsg);
        return QByteArray();
    }
    
    return key;
}

QByteArray EncryptionManager::generateGroupKey(int groupId)
{
    if (groupId <= 0) {
        QString errorMsg = QString("无效的群ID: %1").arg(groupId);
        LOG_WARN(QString("[Encryption] %1").arg(errorMsg));
        emit keyGenerationError(2, errorMsg);
        return QByteArray();
    }
    
    // 使用群ID生成密钥
    QString groupStr = QString("group_%1").arg(groupId);
    
    // SHA-256哈希生成32字节密钥
    QByteArray key = QCryptographicHash::hash(groupStr.toUtf8(), 
                                             QCryptographicHash::Sha256);
    
    if (key.isEmpty()) {
        QString errorMsg = QString("群聊密钥生成失败 (群 %1)").arg(groupId);
        LOG_ERROR(QString("[Encryption] %1").arg(errorMsg));
        emit keyGenerationError(2, errorMsg);
        return QByteArray();
    }
    
    return key;
}

QByteArray EncryptionManager::xorEncryptDecrypt(const QByteArray& data, 
                                               const QByteArray& key)
{
    if (data.isEmpty()) {
        LOG_WARN("[Encryption] Empty data for XOR operation");
        emit encryptionOperationError("数据为空，无法进行加密操作");
        return QByteArray();
    }
    
    if (key.isEmpty()) {
        LOG_ERROR("[Encryption] Empty encryption key for XOR operation");
        emit encryptionOperationError("加密密钥为空");
        return QByteArray();
    }
    
    // 执行XOR操作
    QByteArray result = data;
    int keyLen = key.length();
    
    for (int i = 0; i < result.length(); ++i) {
        result[i] = result[i] ^ key[i % keyLen];
    }
    
    return result;
}

// ========== 密钥缓存管理 ==========

QByteArray EncryptionManager::getCachedChatKey(int userId1, int userId2)
{
    // 确保缓存键的一致性
    int minId = qMin(userId1, userId2);
    int maxId = qMax(userId1, userId2);
    QString cacheKey = QString("chat_%1_%2").arg(minId).arg(maxId);
    
    // 线程安全地访问缓存
    QMutexLocker locker(&m_cacheMutex);
    
    // 检查缓存中是否存在
    if (m_keyCache.contains(cacheKey)) {
        return m_keyCache[cacheKey];
    }
    
    // 不存在则生成并缓存，带重试机制
    locker.unlock(); // 解锁以调用generateChatKey（避免死锁）
    
    QByteArray key;
    const int maxRetries = 3;
    int attempt = 0;
    
    while (attempt < maxRetries) {
        attempt++;
        key = generateChatKey(userId1, userId2);
        
        if (!key.isEmpty()) {
            // 成功生成密钥
            locker.relock(); // 重新加锁以写入缓存
            m_keyCache[cacheKey] = key;
            return key;
        }
        
        // 生成失败，记录重试
        LOG_WARN(QString("[Encryption] Chat key generation failed (attempt %1/%2) for users %3 and %4")
                .arg(attempt).arg(maxRetries).arg(userId1).arg(userId2));
        
        if (attempt < maxRetries) {
            // 短暂延迟后重试
            QThread::msleep(50 * attempt); // 递增延迟：50ms, 100ms, 150ms
        }
    }
    
    // 所有重试都失败
    QString errorMsg = QString("私聊密钥生成失败，已重试 %1 次").arg(maxRetries);
    LOG_ERROR(QString("[Encryption] %1 for users %2 and %3").arg(errorMsg).arg(userId1).arg(userId2));
    emit keyGenerationError(1, errorMsg);
    
    return QByteArray();
}

QByteArray EncryptionManager::getCachedGroupKey(int groupId)
{
    QString cacheKey = QString("group_%1").arg(groupId);
    
    // 线程安全地访问缓存
    QMutexLocker locker(&m_cacheMutex);
    
    // 检查缓存中是否存在
    if (m_keyCache.contains(cacheKey)) {
        return m_keyCache[cacheKey];
    }
    
    // 不存在则生成并缓存，带重试机制
    locker.unlock(); // 解锁以调用generateGroupKey（避免死锁）
    
    QByteArray key;
    const int maxRetries = 3;
    int attempt = 0;
    
    while (attempt < maxRetries) {
        attempt++;
        key = generateGroupKey(groupId);
        
        if (!key.isEmpty()) {
            // 成功生成密钥
            locker.relock(); // 重新加锁以写入缓存
            m_keyCache[cacheKey] = key;
            return key;
        }
        
        // 生成失败，记录重试
        LOG_WARN(QString("[Encryption] Group key generation failed (attempt %1/%2) for group %3")
                .arg(attempt).arg(maxRetries).arg(groupId));
        
        if (attempt < maxRetries) {
            // 短暂延迟后重试
            QThread::msleep(50 * attempt); // 递增延迟：50ms, 100ms, 150ms
        }
    }
    
    // 所有重试都失败
    QString errorMsg = QString("群聊密钥生成失败，已重试 %1 次").arg(maxRetries);
    LOG_ERROR(QString("[Encryption] %1 for group %2").arg(errorMsg).arg(groupId));
    emit keyGenerationError(2, errorMsg);
    
    return QByteArray();
}

void EncryptionManager::clearKeyCache()
{
    QMutexLocker locker(&m_cacheMutex);
    m_keyCache.clear();
}

bool EncryptionManager::isValidImageFormat(const QByteArray& data)
{
    if (data.size() < 8) {
        return false;
    }
    
    // 检查常见图片格式的文件头魔数
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data.constData());
    
    // JPEG: FF D8 FF
    if (bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
        return true;
    }
    
    // PNG: 89 50 4E 47 0D 0A 1A 0A
    if (bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47 &&
        bytes[4] == 0x0D && bytes[5] == 0x0A && bytes[6] == 0x1A && bytes[7] == 0x0A) {
        return true;
    }
    
    // GIF: 47 49 46 38 (GIF8)
    if (bytes[0] == 0x47 && bytes[1] == 0x49 && bytes[2] == 0x46 && bytes[3] == 0x38) {
        return true;
    }
    
    // BMP: 42 4D (BM)
    if (bytes[0] == 0x42 && bytes[1] == 0x4D) {
        return true;
    }
    
    // WebP: 52 49 46 46 ... 57 45 42 50 (RIFF...WEBP)
    if (data.size() >= 12 && bytes[0] == 0x52 && bytes[1] == 0x49 && bytes[2] == 0x46 && bytes[3] == 0x46 &&
        bytes[8] == 0x57 && bytes[9] == 0x45 && bytes[10] == 0x42 && bytes[11] == 0x50) {
        return true;
    }
    
    return false;
}
