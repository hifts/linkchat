#include "configmanager.h"
#include "logger.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMutexLocker>
#include <functional>

ConfigManager::ConfigManager()
    : QObject(nullptr)
    , m_configPath("./config.json")
{
    // 构造函数：初始化成员变量
}

ConfigManager::~ConfigManager()
{
    // 析构函数：清理资源
}

ConfigManager& ConfigManager::instance()
{
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::initialize(const QString& configPath)
{
    // 设置配置文件路径（如果未指定，使用默认路径）
    if (!configPath.isEmpty()) {
        m_configPath = configPath;
    } else {
        m_configPath = "./config.json";
    }
    
    LOG_INFO(QString("Initializing ConfigManager with path: %1").arg(m_configPath));
    
    // 调用 load() 方法加载配置
    bool result = load();
    
    if (result) {
        LOG_INFO("ConfigManager initialized successfully");
    } else {
        LOG_ERROR("ConfigManager initialization failed");
    }
    
    return result;
}

bool ConfigManager::load()
{
    // 使用 QMutexLocker 保护线程安全
    QMutexLocker locker(&m_mutex);
    
    // 检查文件是否存在
    QFile file(m_configPath);
    if (!file.exists()) {
        LOG_INFO(QString("Config file not found: %1, creating default config").arg(m_configPath));
        createDefaultConfig();
        return true;  // createDefaultConfig 已经调用了 save()
    }
    
    // 打开文件
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR(QString("Failed to open config file: %1").arg(m_configPath));
        LOG_INFO("Loading default config from resource");
        return loadDefaultFromResource();
    }
    
    // 读取文件内容
    QByteArray data = file.readAll();
    file.close();
    
    // 解析 JSON
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    
    if (error.error != QJsonParseError::NoError) {
        LOG_ERROR(QString("Failed to parse config file: %1").arg(error.errorString()));
        LOG_INFO("Loading default config from resource");
        return loadDefaultFromResource();
    }
    
    // 存储配置
    m_config = doc.object();
    LOG_INFO(QString("Successfully loaded config from: %1").arg(m_configPath));
    
    // 验证配置
    if (!validate()) {
        LOG_WARN("Config validation failed, using default values for invalid items");
        // 不返回 false，允许使用部分有效的配置
    }
    
    return true;
}

bool ConfigManager::save()
{
    // 使用 QMutexLocker 保护线程安全
    QMutexLocker locker(&m_mutex);
    
    // 将 m_config 序列化为 JSON
    QJsonDocument doc(m_config);
    QByteArray jsonData = doc.toJson(QJsonDocument::Indented);
    
    // 写入文件
    QFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR(QString("Failed to open config file for writing: %1").arg(m_configPath));
        return false;
    }
    
    qint64 bytesWritten = file.write(jsonData);
    file.close();
    
    // 处理写入错误
    if (bytesWritten == -1) {
        LOG_ERROR(QString("Failed to write config to file: %1").arg(m_configPath));
        return false;
    }
    
    if (bytesWritten != jsonData.size()) {
        LOG_ERROR(QString("Incomplete write to config file: %1 (wrote %2 of %3 bytes)")
                  .arg(m_configPath).arg(bytesWritten).arg(jsonData.size()));
        return false;
    }
    
    LOG_INFO(QString("Successfully saved config to: %1").arg(m_configPath));
    return true;
}

bool ConfigManager::reload()
{
    // 使用 QMutexLocker 保护线程安全
    QMutexLocker locker(&m_mutex);
    
    LOG_INFO(QString("Reloading config from: %1").arg(m_configPath));
    
    // 保存当前配置的副本（QJsonObject 拷贝）
    QJsonObject backupConfig = m_config;
    
    // 临时解锁以调用 load()（load() 内部会加锁）
    locker.unlock();
    
    // 尝试重新加载配置文件
    bool loadSuccess = load();
    
    // 重新加锁以访问成员变量
    locker.relock();
    
    // 如果加载失败，恢复原配置
    if (!loadSuccess) {
        LOG_ERROR("Failed to reload config, restoring previous configuration");
        m_config = backupConfig;
        return false;
    }
    
    // 如果加载成功，发出 configReloaded 信号
    LOG_INFO("Config reloaded successfully");
    
    // 解锁后发出信号（避免在持有锁时发出信号）
    locker.unlock();
    emit configReloaded();
    
    return true;
}

QString ConfigManager::getString(const QString& key, const QString& defaultValue) const
{
    // 使用 QMutexLocker 保护线程安全
    QMutexLocker locker(&m_mutex);
    
    // 解析点号分隔的键名
    QStringList keys = key.split('.');
    QJsonValue value = m_config;
    
    // 递归访问嵌套 JSON 对象
    for (const QString& k : keys) {
        if (!value.isObject()) {
            return defaultValue;
        }
        value = value.toObject()[k];
        if (value.isUndefined()) {
            return defaultValue;
        }
    }
    
    // 返回值或默认值
    return value.toString(defaultValue);
}

int ConfigManager::getInt(const QString& key, int defaultValue) const
{
    // 使用 QMutexLocker 保护线程安全
    QMutexLocker locker(&m_mutex);
    
    // 解析点号分隔的键名
    QStringList keys = key.split('.');
    QJsonValue value = m_config;
    
    // 递归访问嵌套 JSON 对象
    for (const QString& k : keys) {
        if (!value.isObject()) {
            return defaultValue;
        }
        value = value.toObject()[k];
        if (value.isUndefined()) {
            return defaultValue;
        }
    }
    
    // 处理类型转换，返回值或默认值
    return value.toInt(defaultValue);
}

bool ConfigManager::getBool(const QString& key, bool defaultValue) const
{
    // 使用 QMutexLocker 保护线程安全
    QMutexLocker locker(&m_mutex);
    
    // 解析点号分隔的键名
    QStringList keys = key.split('.');
    QJsonValue value = m_config;
    
    // 递归访问嵌套 JSON 对象
    for (const QString& k : keys) {
        if (!value.isObject()) {
            return defaultValue;
        }
        value = value.toObject()[k];
        if (value.isUndefined()) {
            return defaultValue;
        }
    }
    
    // 处理类型转换，返回值或默认值
    return value.toBool(defaultValue);
}

double ConfigManager::getDouble(const QString& key, double defaultValue) const
{
    // 使用 QMutexLocker 保护线程安全
    QMutexLocker locker(&m_mutex);
    
    // 解析点号分隔的键名
    QStringList keys = key.split('.');
    QJsonValue value = m_config;
    
    // 递归访问嵌套 JSON 对象
    for (const QString& k : keys) {
        if (!value.isObject()) {
            return defaultValue;
        }
        value = value.toObject()[k];
        if (value.isUndefined()) {
            return defaultValue;
        }
    }
    
    // 处理类型转换，返回值或默认值
    return value.toDouble(defaultValue);
}

void ConfigManager::setString(const QString& key, const QString& value)
{
    // 使用 QMutexLocker 保护线程安全
    QMutexLocker locker(&m_mutex);
    
    // 解析点号分隔的键名
    QStringList keys = key.split('.');
    
    // 如果键名为空，直接返回
    if (keys.isEmpty()) {
        LOG_WARN(QString("Invalid empty key for setString"));
        return;
    }
    
    // 创建或更新嵌套 JSON 对象
    // 使用递归方式构建嵌套结构
    QJsonObject root = m_config;
    
    // 辅助函数：递归设置嵌套值
    std::function<void(QJsonObject&, const QStringList&, int, const QJsonValue&)> setNestedValue;
    setNestedValue = [&](QJsonObject& obj, const QStringList& keyList, int index, const QJsonValue& val) {
        if (index == keyList.size() - 1) {
            // 到达最后一个键，设置值
            obj[keyList[index]] = val;
        } else {
            // 还有更深层的键
            QString currentKey = keyList[index];
            QJsonObject nested;
            
            // 如果当前键存在且是对象，使用现有对象
            if (obj.contains(currentKey) && obj[currentKey].isObject()) {
                nested = obj[currentKey].toObject();
            }
            
            // 递归设置下一层
            setNestedValue(nested, keyList, index + 1, val);
            
            // 更新当前层
            obj[currentKey] = nested;
        }
    };
    
    // 执行递归设置
    setNestedValue(root, keys, 0, QJsonValue(value));
    
    // 更新配置
    m_config = root;
    
    // 发出 configChanged 信号
    emit configChanged(key);
    
    LOG_DEBUG(QString("Config updated: %1 = %2").arg(key, value));
}

void ConfigManager::setInt(const QString& key, int value)
{
    // 使用 QMutexLocker 保护线程安全
    QMutexLocker locker(&m_mutex);
    
    // 解析点号分隔的键名
    QStringList keys = key.split('.');
    
    // 如果键名为空，直接返回
    if (keys.isEmpty()) {
        LOG_WARN(QString("Invalid empty key for setInt"));
        return;
    }
    
    // 创建或更新嵌套 JSON 对象
    // 使用递归方式构建嵌套结构
    QJsonObject root = m_config;
    
    // 辅助函数：递归设置嵌套值
    std::function<void(QJsonObject&, const QStringList&, int, const QJsonValue&)> setNestedValue;
    setNestedValue = [&](QJsonObject& obj, const QStringList& keyList, int index, const QJsonValue& val) {
        if (index == keyList.size() - 1) {
            // 到达最后一个键，设置值
            obj[keyList[index]] = val;
        } else {
            // 还有更深层的键
            QString currentKey = keyList[index];
            QJsonObject nested;
            
            // 如果当前键存在且是对象，使用现有对象
            if (obj.contains(currentKey) && obj[currentKey].isObject()) {
                nested = obj[currentKey].toObject();
            }
            
            // 递归设置下一层
            setNestedValue(nested, keyList, index + 1, val);
            
            // 更新当前层
            obj[currentKey] = nested;
        }
    };
    
    // 执行递归设置
    setNestedValue(root, keys, 0, QJsonValue(value));
    
    // 更新配置
    m_config = root;
    
    // 发出 configChanged 信号
    emit configChanged(key);
    
    LOG_DEBUG(QString("Config updated: %1 = %2").arg(key).arg(value));
}

void ConfigManager::setBool(const QString& key, bool value)
{
    // 使用 QMutexLocker 保护线程安全
    QMutexLocker locker(&m_mutex);
    
    // 解析点号分隔的键名
    QStringList keys = key.split('.');
    
    // 如果键名为空，直接返回
    if (keys.isEmpty()) {
        LOG_WARN(QString("Invalid empty key for setBool"));
        return;
    }
    
    // 创建或更新嵌套 JSON 对象
    // 使用递归方式构建嵌套结构
    QJsonObject root = m_config;
    
    // 辅助函数：递归设置嵌套值
    std::function<void(QJsonObject&, const QStringList&, int, const QJsonValue&)> setNestedValue;
    setNestedValue = [&](QJsonObject& obj, const QStringList& keyList, int index, const QJsonValue& val) {
        if (index == keyList.size() - 1) {
            // 到达最后一个键，设置值
            obj[keyList[index]] = val;
        } else {
            // 还有更深层的键
            QString currentKey = keyList[index];
            QJsonObject nested;
            
            // 如果当前键存在且是对象，使用现有对象
            if (obj.contains(currentKey) && obj[currentKey].isObject()) {
                nested = obj[currentKey].toObject();
            }
            
            // 递归设置下一层
            setNestedValue(nested, keyList, index + 1, val);
            
            // 更新当前层
            obj[currentKey] = nested;
        }
    };
    
    // 执行递归设置
    setNestedValue(root, keys, 0, QJsonValue(value));
    
    // 更新配置
    m_config = root;
    
    // 发出 configChanged 信号
    emit configChanged(key);
    
    LOG_DEBUG(QString("Config updated: %1 = %2").arg(key).arg(value ? "true" : "false"));
}

void ConfigManager::setDouble(const QString& key, double value)
{
    // 使用 QMutexLocker 保护线程安全
    QMutexLocker locker(&m_mutex);
    
    // 解析点号分隔的键名
    QStringList keys = key.split('.');
    
    // 如果键名为空，直接返回
    if (keys.isEmpty()) {
        LOG_WARN(QString("Invalid empty key for setDouble"));
        return;
    }
    
    // 创建或更新嵌套 JSON 对象
    // 使用递归方式构建嵌套结构
    QJsonObject root = m_config;
    
    // 辅助函数：递归设置嵌套值
    std::function<void(QJsonObject&, const QStringList&, int, const QJsonValue&)> setNestedValue;
    setNestedValue = [&](QJsonObject& obj, const QStringList& keyList, int index, const QJsonValue& val) {
        if (index == keyList.size() - 1) {
            // 到达最后一个键，设置值
            obj[keyList[index]] = val;
        } else {
            // 还有更深层的键
            QString currentKey = keyList[index];
            QJsonObject nested;
            
            // 如果当前键存在且是对象，使用现有对象
            if (obj.contains(currentKey) && obj[currentKey].isObject()) {
                nested = obj[currentKey].toObject();
            }
            
            // 递归设置下一层
            setNestedValue(nested, keyList, index + 1, val);
            
            // 更新当前层
            obj[currentKey] = nested;
        }
    };
    
    // 执行递归设置
    setNestedValue(root, keys, 0, QJsonValue(value));
    
    // 更新配置
    m_config = root;
    
    // 发出 configChanged 信号
    emit configChanged(key);
    
    LOG_DEBUG(QString("Config updated: %1 = %2").arg(key).arg(value));
}

bool ConfigManager::validate()
{
    // 使用 QMutexLocker 保护线程安全
    QMutexLocker locker(&m_mutex);
    
    // 清空 m_validationErrors
    m_validationErrors.clear();
    
    bool valid = true;
    
    // 验证服务器端口号
    int serverPort = getInt("server.port", 8080);
    if (!validatePort(serverPort)) {
        m_validationErrors.append(QString("Invalid server port: %1 (must be 1-65535)").arg(serverPort));
        valid = false;
    }
    
    // 验证客户端服务器端口号
    int clientServerPort = getInt("client.server_port", 8080);
    if (!validatePort(clientServerPort)) {
        m_validationErrors.append(QString("Invalid client server port: %1 (must be 1-65535)").arg(clientServerPort));
        valid = false;
    }
    
    // 验证数据库端口号
    int dbPort = getInt("server.database.port", 3306);
    if (!validatePort(dbPort)) {
        m_validationErrors.append(QString("Invalid database port: %1 (must be 1-65535)").arg(dbPort));
        valid = false;
    }
    
    // 验证日志级别
    QString logLevel = getString("server.log.level", "INFO");
    if (!validateLogLevel(logLevel)) {
        m_validationErrors.append(QString("Invalid log level: %1 (must be DEBUG/INFO/WARNING/ERROR)").arg(logLevel));
        valid = false;
    }
    
    // 验证日志文件路径
    QString logFilePath = getString("server.log.file_path", "server.log");
    if (!validatePath(logFilePath)) {
        m_validationErrors.append("Invalid log file path: path cannot be empty");
        valid = false;
    }
    
    // 验证数据库主机
    QString dbHost = getString("server.database.host", "localhost");
    if (!validatePath(dbHost)) {
        m_validationErrors.append("Invalid database host: host cannot be empty");
        valid = false;
    }
    
    // 验证数据库名称
    QString dbName = getString("server.database.database", "linkchat");
    if (!validatePath(dbName)) {
        m_validationErrors.append("Invalid database name: name cannot be empty");
        valid = false;
    }
    
    // 验证服务器心跳参数
    int serverHeartbeatInterval = getInt("server.heartbeat_interval", 30000);
    int serverHeartbeatTimeout = getInt("server.heartbeat_timeout", 90000);
    if (serverHeartbeatInterval <= 0) {
        m_validationErrors.append(QString("Invalid server heartbeat interval: %1 (must be > 0)").arg(serverHeartbeatInterval));
        valid = false;
    }
    if (serverHeartbeatTimeout <= serverHeartbeatInterval) {
        m_validationErrors.append(QString("Invalid server heartbeat timeout: %1 (must be > heartbeat interval %2)")
                                  .arg(serverHeartbeatTimeout).arg(serverHeartbeatInterval));
        valid = false;
    }
    
    // 验证客户端心跳参数
    int clientHeartbeatInterval = getInt("client.heartbeat_interval", 30000);
    int clientHeartbeatTimeout = getInt("client.heartbeat_timeout", 90000);
    if (clientHeartbeatInterval <= 0) {
        m_validationErrors.append(QString("Invalid client heartbeat interval: %1 (must be > 0)").arg(clientHeartbeatInterval));
        valid = false;
    }
    if (clientHeartbeatTimeout <= clientHeartbeatInterval) {
        m_validationErrors.append(QString("Invalid client heartbeat timeout: %1 (must be > heartbeat interval %2)")
                                  .arg(clientHeartbeatTimeout).arg(clientHeartbeatInterval));
        valid = false;
    }
    
    // 验证客户端文件保存路径
    QString fileSavePath = getString("client.file_save_path", "./downloads");
    if (!validatePath(fileSavePath)) {
        m_validationErrors.append("Invalid file save path: path cannot be empty");
        valid = false;
    }
    
    // 验证客户端服务器地址
    QString serverAddress = getString("client.server_address", "127.0.0.1");
    if (!validatePath(serverAddress)) {
        m_validationErrors.append("Invalid server address: address cannot be empty");
        valid = false;
    }
    
    // 验证重连参数
    int reconnectMaxAttempts = getInt("client.reconnect_max_attempts", 5);
    if (reconnectMaxAttempts <= 0) {
        m_validationErrors.append(QString("Invalid reconnect max attempts: %1 (must be > 0)").arg(reconnectMaxAttempts));
        valid = false;
    }
    
    int reconnectInterval = getInt("client.reconnect_interval", 3000);
    if (reconnectInterval <= 0) {
        m_validationErrors.append(QString("Invalid reconnect interval: %1 (must be > 0)").arg(reconnectInterval));
        valid = false;
    }
    
    // 记录验证结果
    if (valid) {
        LOG_INFO("Configuration validation passed");
    } else {
        LOG_WARN(QString("Configuration validation failed with %1 error(s)").arg(m_validationErrors.size()));
        for (const QString& error : m_validationErrors) {
            LOG_WARN(QString("  - %1").arg(error));
        }
    }
    
    return valid;
}

QStringList ConfigManager::getValidationErrors() const
{
    // 返回 m_validationErrors 列表
    // 注意：不需要加锁，因为这个方法只是返回一个副本
    // 而且 m_validationErrors 只在 validate() 中修改，validate() 已经加锁
    return m_validationErrors;
}

QString ConfigManager::getConfigPath() const
{
    // 返回当前配置文件路径 m_configPath
    return m_configPath;
}

bool ConfigManager::loadDefaultFromResource()
{
    // 从 Qt 资源文件读取默认配置
    QFile resourceFile(":/config/default_config.json");
    if (!resourceFile.open(QIODevice::ReadOnly)) {
        LOG_ERROR("Failed to open default config resource: :/config/default_config.json");
        return false;
    }
    
    // 读取文件内容
    QByteArray data = resourceFile.readAll();
    resourceFile.close();
    
    // 解析 JSON
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    
    if (error.error != QJsonParseError::NoError) {
        LOG_ERROR(QString("Failed to parse default config resource: %1").arg(error.errorString()));
        return false;
    }
    
    // 存储到 m_config
    m_config = doc.object();
    LOG_INFO("Successfully loaded default config from resource");
    return true;
}

void ConfigManager::createDefaultConfig()
{
    // 首先尝试从资源文件加载
    if (loadDefaultFromResource()) {
        LOG_INFO("Loaded default config from resource, saving to file system");
        // 保存到文件系统
        if (save()) {
            LOG_INFO(QString("Default config saved to: %1").arg(m_configPath));
        } else {
            LOG_WARN("Failed to save default config to file system");
        }
        return;
    }
    
    // 如果资源文件加载失败，使用硬编码的默认值作为后备方案
    LOG_WARN("Resource file not available, using hardcoded defaults");
    
    QJsonObject config;
    
    // 服务器配置
    QJsonObject server;
    server["port"] = 8080;
    server["max_connections"] = 1000;
    server["heartbeat_interval"] = 30000;
    server["heartbeat_timeout"] = 90000;
    
    QJsonObject database;
    database["host"] = "localhost";
    database["port"] = 3306;
    database["username"] = "root";
    database["password"] = "";
    database["database"] = "linkchat";
    server["database"] = database;
    
    QJsonObject log;
    log["level"] = "INFO";
    log["file_path"] = "server.log";
    server["log"] = log;
    
    config["server"] = server;
    
    // 客户端配置
    QJsonObject client;
    client["server_address"] = "127.0.0.1";
    client["server_port"] = 8080;
    client["auto_reconnect"] = true;
    client["reconnect_max_attempts"] = 5;
    client["reconnect_interval"] = 3000;
    client["heartbeat_interval"] = 30000;
    client["heartbeat_timeout"] = 90000;
    client["file_save_path"] = "./downloads";
    client["language"] = "zh_CN";
    config["client"] = client;
    
    m_config = config;
    LOG_INFO("Created hardcoded default config");
}

bool ConfigManager::validatePort(int port)
{
    // 检查端口号范围 1-65535
    return port >= 1 && port <= 65535;
}

bool ConfigManager::validatePath(const QString& path)
{
    // 检查路径是否为空
    return !path.isEmpty();
}

bool ConfigManager::validateLogLevel(const QString& level)
{
    // 使用 QStringList 存储有效级别
    static QStringList validLevels = {"DEBUG", "INFO", "WARNING", "ERROR"};
    // 检查日志级别是否为 DEBUG/INFO/WARNING/ERROR（不区分大小写）
    return validLevels.contains(level.toUpper());
}
