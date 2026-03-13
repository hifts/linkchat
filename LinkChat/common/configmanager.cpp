#include "configmanager.h"
#include "logger.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMutexLocker>
#include <functional>

namespace {
QJsonValue getConfigValueUnlocked(const QJsonObject& root, const QString& key)
{
    const QStringList keys = key.split('.');
    QJsonValue value(root);
    for (const QString& k : keys) {
        if (!value.isObject()) {
            return QJsonValue();
        }
        value = value.toObject().value(k);
        if (value.isUndefined()) {
            return QJsonValue();
        }
    }
    return value;
}
}

ConfigManager::ConfigManager()
    : QObject(nullptr)
    , m_configPath("./config.json")
{
}

ConfigManager::~ConfigManager()
{
}

ConfigManager& ConfigManager::instance()
{
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::initialize(const QString& configPath)
{
    if (!configPath.isEmpty()) {
        m_configPath = configPath;
    } else {
        m_configPath = "./config.json";
    }
    
    LOG_INFO(QString("Initializing ConfigManager with path: %1").arg(m_configPath));
    
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
    QMutexLocker locker(&m_mutex);
    
    QFile file(m_configPath);
    if (!file.exists()) {
        LOG_INFO(QString("Config file not found: %1, creating default config").arg(m_configPath));
        locker.unlock();
        createDefaultConfig();
        return true;
    }
    
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR(QString("Failed to open config file: %1").arg(m_configPath));
        LOG_INFO("Loading default config from resource");
        locker.unlock();
        return loadDefaultFromResource();
    }
    
    QByteArray data = file.readAll();
    file.close();
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    
    if (error.error != QJsonParseError::NoError) {
        LOG_ERROR(QString("Failed to parse config file: %1").arg(error.errorString()));
        LOG_INFO("Loading default config from resource");
        return loadDefaultFromResource();
    }
    
    m_config = doc.object();
    LOG_INFO(QString("Successfully loaded config from: %1").arg(m_configPath));

    locker.unlock();
    if (!validate()) {
        LOG_WARN("Config validation failed, using default values for invalid items");
    }
    
    return true;
}

bool ConfigManager::save()
{
    QMutexLocker locker(&m_mutex);
    
    QJsonDocument doc(m_config);
    QByteArray jsonData = doc.toJson(QJsonDocument::Indented);
    
    QFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR(QString("Failed to open config file for writing: %1").arg(m_configPath));
        return false;
    }
    
    qint64 bytesWritten = file.write(jsonData);
    file.close();
    
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
    QMutexLocker locker(&m_mutex);
    
    LOG_INFO(QString("Reloading config from: %1").arg(m_configPath));
    
    QJsonObject backupConfig = m_config;
    
    locker.unlock();
    
    bool loadSuccess = load();
    
    locker.relock();
    
    if (!loadSuccess) {
        LOG_ERROR("Failed to reload config, restoring previous configuration");
        m_config = backupConfig;
        return false;
    }
    
    LOG_INFO("Config reloaded successfully");
    
    locker.unlock();
    emit configReloaded();
    
    return true;
}

QString ConfigManager::getString(const QString& key, const QString& defaultValue) const
{
    QMutexLocker locker(&m_mutex);
    
    QStringList keys = key.split('.');
    QJsonValue value = m_config;
    
    for (const QString& k : keys) {
        if (!value.isObject()) {
            return defaultValue;
        }
        value = value.toObject()[k];
        if (value.isUndefined()) {
            return defaultValue;
        }
    }
    
    return value.toString(defaultValue);
}

int ConfigManager::getInt(const QString& key, int defaultValue) const
{
    QMutexLocker locker(&m_mutex);
    
    QStringList keys = key.split('.');
    QJsonValue value = m_config;
    
    for (const QString& k : keys) {
        if (!value.isObject()) {
            return defaultValue;
        }
        value = value.toObject()[k];
        if (value.isUndefined()) {
            return defaultValue;
        }
    }
    
    return value.toInt(defaultValue);
}

bool ConfigManager::getBool(const QString& key, bool defaultValue) const
{
    QMutexLocker locker(&m_mutex);
    
    QStringList keys = key.split('.');
    QJsonValue value = m_config;
    
    for (const QString& k : keys) {
        if (!value.isObject()) {
            return defaultValue;
        }
        value = value.toObject()[k];
        if (value.isUndefined()) {
            return defaultValue;
        }
    }
    
    return value.toBool(defaultValue);
}

double ConfigManager::getDouble(const QString& key, double defaultValue) const
{
    QMutexLocker locker(&m_mutex);
    
    QStringList keys = key.split('.');
    QJsonValue value = m_config;
    
    for (const QString& k : keys) {
        if (!value.isObject()) {
            return defaultValue;
        }
        value = value.toObject()[k];
        if (value.isUndefined()) {
            return defaultValue;
        }
    }
    
    return value.toDouble(defaultValue);
}

void ConfigManager::setString(const QString& key, const QString& value)
{
    QMutexLocker locker(&m_mutex);
    
    QStringList keys = key.split('.');
    
    if (keys.isEmpty()) {
        LOG_WARN(QString("Invalid empty key for setString"));
        return;
    }
    
    QJsonObject root = m_config;
    
    std::function<void(QJsonObject&, const QStringList&, int, const QJsonValue&)> setNestedValue;
    setNestedValue = [&](QJsonObject& obj, const QStringList& keyList, int index, const QJsonValue& val) {
        if (index == keyList.size() - 1) {
            obj[keyList[index]] = val;
        } else {
            QString currentKey = keyList[index];
            QJsonObject nested;
            
            if (obj.contains(currentKey) && obj[currentKey].isObject()) {
                nested = obj[currentKey].toObject();
            }
            
            setNestedValue(nested, keyList, index + 1, val);
            
            obj[currentKey] = nested;
        }
    };
    
    setNestedValue(root, keys, 0, QJsonValue(value));
    
    m_config = root;
    
    emit configChanged(key);
    
    LOG_DEBUG(QString("Config updated: %1 = %2").arg(key).arg(value));
}

void ConfigManager::setInt(const QString& key, int value)
{
    QMutexLocker locker(&m_mutex);
    
    QStringList keys = key.split('.');
    
    if (keys.isEmpty()) {
        LOG_WARN(QString("Invalid empty key for setInt"));
        return;
    }
    
    QJsonObject root = m_config;
    
    std::function<void(QJsonObject&, const QStringList&, int, const QJsonValue&)> setNestedValue;
    setNestedValue = [&](QJsonObject& obj, const QStringList& keyList, int index, const QJsonValue& val) {
        if (index == keyList.size() - 1) {
            obj[keyList[index]] = val;
        } else {
            QString currentKey = keyList[index];
            QJsonObject nested;
            
            if (obj.contains(currentKey) && obj[currentKey].isObject()) {
                nested = obj[currentKey].toObject();
            }
            
            setNestedValue(nested, keyList, index + 1, val);
            
            obj[currentKey] = nested;
        }
    };
    
    setNestedValue(root, keys, 0, QJsonValue(value));
    
    m_config = root;
    
    emit configChanged(key);
    
    LOG_DEBUG(QString("Config updated: %1 = %2").arg(key).arg(value));
}

void ConfigManager::setBool(const QString& key, bool value)
{
    QMutexLocker locker(&m_mutex);
    
    QStringList keys = key.split('.');
    
    if (keys.isEmpty()) {
        LOG_WARN(QString("Invalid empty key for setBool"));
        return;
    }
    
    QJsonObject root = m_config;
    
    std::function<void(QJsonObject&, const QStringList&, int, const QJsonValue&)> setNestedValue;
    setNestedValue = [&](QJsonObject& obj, const QStringList& keyList, int index, const QJsonValue& val) {
        if (index == keyList.size() - 1) {
            obj[keyList[index]] = val;
        } else {
            QString currentKey = keyList[index];
            QJsonObject nested;
            
            if (obj.contains(currentKey) && obj[currentKey].isObject()) {
                nested = obj[currentKey].toObject();
            }
            
            setNestedValue(nested, keyList, index + 1, val);
            
            obj[currentKey] = nested;
        }
    };
    
    setNestedValue(root, keys, 0, QJsonValue(value));
    
    m_config = root;
    
    emit configChanged(key);
    
    LOG_DEBUG(QString("Config updated: %1 = %2").arg(key).arg(value ? "true" : "false"));
}

void ConfigManager::setDouble(const QString& key, double value)
{
    QMutexLocker locker(&m_mutex);
    
    QStringList keys = key.split('.');
    
    if (keys.isEmpty()) {
        LOG_WARN(QString("Invalid empty key for setDouble"));
        return;
    }
    
    QJsonObject root = m_config;
    
    std::function<void(QJsonObject&, const QStringList&, int, const QJsonValue&)> setNestedValue;
    setNestedValue = [&](QJsonObject& obj, const QStringList& keyList, int index, const QJsonValue& val) {
        if (index == keyList.size() - 1) {
            obj[keyList[index]] = val;
        } else {
            QString currentKey = keyList[index];
            QJsonObject nested;
            
            if (obj.contains(currentKey) && obj[currentKey].isObject()) {
                nested = obj[currentKey].toObject();
            }
            
            setNestedValue(nested, keyList, index + 1, val);
            
            obj[currentKey] = nested;
        }
    };
    
    setNestedValue(root, keys, 0, QJsonValue(value));
    
    m_config = root;
    
    emit configChanged(key);
    
    LOG_DEBUG(QString("Config updated: %1 = %2").arg(key).arg(value));
}

bool ConfigManager::validate()
{
    QMutexLocker locker(&m_mutex);
    m_validationErrors.clear();
    bool valid = true;

    const auto readInt = [this](const QString& key, int defaultValue) {
        const QJsonValue v = getConfigValueUnlocked(m_config, key);
        return v.isUndefined() ? defaultValue : v.toInt(defaultValue);
    };
    const auto readString = [this](const QString& key, const QString& defaultValue) {
        const QJsonValue v = getConfigValueUnlocked(m_config, key);
        return v.isUndefined() ? defaultValue : v.toString(defaultValue);
    };

    const int serverPort = readInt("server.port", 8080);
    if (!validatePort(serverPort)) {
        m_validationErrors.append(QString("Invalid server port: %1 (must be 1-65535)").arg(serverPort));
        valid = false;
    }

    const int clientServerPort = readInt("client.server_port", 8080);
    if (!validatePort(clientServerPort)) {
        m_validationErrors.append(QString("Invalid client server port: %1 (must be 1-65535)").arg(clientServerPort));
        valid = false;
    }

    const int dbPort = readInt("server.database.port", 3306);
    if (!validatePort(dbPort)) {
        m_validationErrors.append(QString("Invalid database port: %1 (must be 1-65535)").arg(dbPort));
        valid = false;
    }

    const QString logLevel = readString("server.log.level", "INFO");
    if (!validateLogLevel(logLevel)) {
        m_validationErrors.append(QString("Invalid log level: %1 (must be DEBUG/INFO/WARNING/ERROR)").arg(logLevel));
        valid = false;
    }

    const QString logFilePath = readString("server.log.file_path", "server.log");
    if (!validatePath(logFilePath)) {
        m_validationErrors.append("Invalid log file path: path cannot be empty");
        valid = false;
    }

    const QString dbHost = readString("server.database.host", "localhost");
    if (!validatePath(dbHost)) {
        m_validationErrors.append("Invalid database host: host cannot be empty");
        valid = false;
    }

    const QString dbName = readString("server.database.database", "linkchat");
    if (!validatePath(dbName)) {
        m_validationErrors.append("Invalid database name: name cannot be empty");
        valid = false;
    }

    const int serverHeartbeatInterval = readInt("server.heartbeat_interval", 30000);
    const int serverHeartbeatTimeout = readInt("server.heartbeat_timeout", 90000);
    if (serverHeartbeatInterval <= 0) {
        m_validationErrors.append(QString("Invalid server heartbeat interval: %1 (must be > 0)").arg(serverHeartbeatInterval));
        valid = false;
    }
    if (serverHeartbeatTimeout <= serverHeartbeatInterval) {
        m_validationErrors.append(QString("Invalid server heartbeat timeout: %1 (must be > heartbeat interval %2)")
                                      .arg(serverHeartbeatTimeout).arg(serverHeartbeatInterval));
        valid = false;
    }

    const int clientHeartbeatInterval = readInt("client.heartbeat_interval", 30000);
    const int clientHeartbeatTimeout = readInt("client.heartbeat_timeout", 90000);
    if (clientHeartbeatInterval <= 0) {
        m_validationErrors.append(QString("Invalid client heartbeat interval: %1 (must be > 0)").arg(clientHeartbeatInterval));
        valid = false;
    }
    if (clientHeartbeatTimeout <= clientHeartbeatInterval) {
        m_validationErrors.append(QString("Invalid client heartbeat timeout: %1 (must be > heartbeat interval %2)")
                                      .arg(clientHeartbeatTimeout).arg(clientHeartbeatInterval));
        valid = false;
    }

    const QString fileSavePath = readString("client.file_save_path", "./downloads");
    if (!validatePath(fileSavePath)) {
        m_validationErrors.append("Invalid file save path: path cannot be empty");
        valid = false;
    }

    const QString serverAddress = readString("client.server_address", "127.0.0.1");
    if (!validatePath(serverAddress)) {
        m_validationErrors.append("Invalid server address: address cannot be empty");
        valid = false;
    }

    const int reconnectMaxAttempts = readInt("client.reconnect_max_attempts", 5);
    if (reconnectMaxAttempts <= 0) {
        m_validationErrors.append(QString("Invalid reconnect max attempts: %1 (must be > 0)").arg(reconnectMaxAttempts));
        valid = false;
    }

    const int reconnectInterval = readInt("client.reconnect_interval", 3000);
    if (reconnectInterval <= 0) {
        m_validationErrors.append(QString("Invalid reconnect interval: %1 (must be > 0)").arg(reconnectInterval));
        valid = false;
    }

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
    return m_validationErrors;
}

QString ConfigManager::getConfigPath() const
{
    return m_configPath;
}

bool ConfigManager::loadDefaultFromResource()
{
    QFile resourceFile(":/config/default_config.json");
    if (!resourceFile.open(QIODevice::ReadOnly)) {
        LOG_ERROR("Failed to open default config resource: :/config/default_config.json");
        return false;
    }
    
    QByteArray data = resourceFile.readAll();
    resourceFile.close();
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    
    if (error.error != QJsonParseError::NoError) {
        LOG_ERROR(QString("Failed to parse default config resource: %1").arg(error.errorString()));
        return false;
    }
    
    m_config = doc.object();
    LOG_INFO("Successfully loaded default config from resource");
    return true;
}

void ConfigManager::createDefaultConfig()
{
    if (loadDefaultFromResource()) {
        LOG_INFO("Loaded default config from resource, saving to file system");
        if (save()) {
            LOG_INFO(QString("Default config saved to: %1").arg(m_configPath));
        } else {
            LOG_WARN("Failed to save default config to file system");
        }
        return;
    }
    
    LOG_WARN("Resource file not available, using hardcoded defaults");
    
    QJsonObject config;
    
    QJsonObject server;
    server["port"] = 8080;
    server["max_connections"] = 1000;
    server["heartbeat_interval"] = 30000;
    server["heartbeat_timeout"] = 90000;
    
    QJsonObject database;
    database["host"] = "localhost";
    database["port"] = 3306;
    database["username"] = "root";
    database["password"] = "root";
    database["database"] = "linkchat";
    server["database"] = database;
    
    QJsonObject log;
    log["level"] = "INFO";
    log["file_path"] = "server.log";
    server["log"] = log;
    
    config["server"] = server;
    
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
    return port >= 1 && port <= 65535;
}

bool ConfigManager::validatePath(const QString& path)
{
    return !path.isEmpty();
}

bool ConfigManager::validateLogLevel(const QString& level)
{
    static QStringList validLevels = {"DEBUG", "INFO", "WARNING", "ERROR"};
    return validLevels.contains(level.toUpper());
}

