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


QByteArray EncryptionManager::generateSalt(int length)
{
    if (length <= 0) {
        LOG_WARN(QString("[Encryption] Invalid salt length: %1").arg(length));
        return QByteArray();
    }
    
    QByteArray salt;
    salt.reserve(length);
    
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
    
    QByteArray combined = password.toUtf8() + salt;
    
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
    
    QByteArray computedHash = hashPassword(password, salt);
    
    if (computedHash.isEmpty()) {
        LOG_ERROR("[Encryption] Failed to compute hash for verification");
        return false;
    }
    
    bool match = (computedHash == storedHash);
    
    return match;
}


QByteArray EncryptionManager::generateChatKey(int userId1, int userId2)
{
    if (userId1 <= 0 || userId2 <= 0) {
        QString errorMsg = QString("Invalid user IDs: %1, %2").arg(userId1).arg(userId2);
        LOG_WARN(QString("[Encryption] %1").arg(errorMsg));
        emit keyGenerationError(1, errorMsg);
        return QByteArray();
    }
    
    if (userId1 == userId2) {
        QString errorMsg = QString("Cannot generate a key for the same user: %1").arg(userId1);
        LOG_WARN(QString("[Encryption] %1").arg(errorMsg));
        emit keyGenerationError(1, errorMsg);
        return QByteArray();
    }
    
    int minId = qMin(userId1, userId2);
    int maxId = qMax(userId1, userId2);
    
    QString combined = QString("%1_%2").arg(minId).arg(maxId);
    
    QByteArray key = QCryptographicHash::hash(combined.toUtf8(), 
                                             QCryptographicHash::Sha256);
    
    if (key.isEmpty()) {
        QString errorMsg = QString("Failed to generate chat key (users %1 and %2)").arg(userId1).arg(userId2);
        LOG_ERROR(QString("[Encryption] %1").arg(errorMsg));
        emit keyGenerationError(1, errorMsg);
        return QByteArray();
    }
    
    return key;
}

QByteArray EncryptionManager::generateGroupKey(int groupId)
{
    if (groupId <= 0) {
        QString errorMsg = QString("Invalid group ID: %1").arg(groupId);
        LOG_WARN(QString("[Encryption] %1").arg(errorMsg));
        emit keyGenerationError(2, errorMsg);
        return QByteArray();
    }
    
    QString groupStr = QString("group_%1").arg(groupId);
    
    QByteArray key = QCryptographicHash::hash(groupStr.toUtf8(), 
                                             QCryptographicHash::Sha256);
    
    if (key.isEmpty()) {
        QString errorMsg = QString("Failed to generate group key (group %1)").arg(groupId);
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
        emit encryptionOperationError("Data is empty, cannot perform encryption operation.");
        return QByteArray();
    }
    
    if (key.isEmpty()) {
        LOG_ERROR("[Encryption] Empty encryption key for XOR operation");
        emit encryptionOperationError("Encryption key is empty.");
        return QByteArray();
    }
    
    QByteArray result;
    result.resize(data.length());
    
    int keyLen = key.length();
    int dataLen = data.length();
    
    const char* src = data.constData();
    const char* k = key.constData();
    char* dst = result.data();
    
    for (int i = 0; i < dataLen; ++i) {
        dst[i] = src[i] ^ k[i % keyLen];
    }
    
    return result;
}


QByteArray EncryptionManager::getCachedChatKey(int userId1, int userId2)
{
    int minId = qMin(userId1, userId2);
    int maxId = qMax(userId1, userId2);
    QString cacheKey = QString("chat_%1_%2").arg(minId).arg(maxId);
    
    QMutexLocker locker(&m_cacheMutex);
    
    if (m_keyCache.contains(cacheKey)) {
        return m_keyCache[cacheKey];
    }
    
    locker.unlock();
    
    QByteArray key;
    const int maxRetries = 3;
    int attempt = 0;
    
    while (attempt < maxRetries) {
        attempt++;
        key = generateChatKey(userId1, userId2);
        
        if (!key.isEmpty()) {
            locker.relock();
            m_keyCache[cacheKey] = key;
            return key;
        }
        
        LOG_WARN(QString("[Encryption] Chat key generation failed (attempt %1/%2) for users %3 and %4")
                .arg(attempt).arg(maxRetries).arg(userId1).arg(userId2));
        
        if (attempt < maxRetries) {
            QThread::msleep(50 * attempt);
        }
    }
    
    QString errorMsg = QString("Failed to generate chat key after %1 retries.").arg(maxRetries);
    LOG_ERROR(QString("[Encryption] %1 for users %2 and %3").arg(errorMsg).arg(userId1).arg(userId2));
    emit keyGenerationError(1, errorMsg);
    
    return QByteArray();
}

QByteArray EncryptionManager::getCachedGroupKey(int groupId)
{
    QString cacheKey = QString("group_%1").arg(groupId);
    
    QMutexLocker locker(&m_cacheMutex);
    
    if (m_keyCache.contains(cacheKey)) {
        return m_keyCache[cacheKey];
    }
    
    locker.unlock();
    
    QByteArray key;
    const int maxRetries = 3;
    int attempt = 0;
    
    while (attempt < maxRetries) {
        attempt++;
        key = generateGroupKey(groupId);
        
        if (!key.isEmpty()) {
            locker.relock();
            m_keyCache[cacheKey] = key;
            return key;
        }
        
        LOG_WARN(QString("[Encryption] Group key generation failed (attempt %1/%2) for group %3")
                .arg(attempt).arg(maxRetries).arg(groupId));
        
        if (attempt < maxRetries) {
            QThread::msleep(50 * attempt);
        }
    }
    
    QString errorMsg = QString("Failed to generate group key after %1 retries.").arg(maxRetries);
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
