#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QMutex>
#include <QStringList>

/**
 * @brief ConfigManager 配置管理器
 * 
 * 提供统一的配置文件管理功能，支持 JSON 格式配置文件的读取、写入和验证
 * 使用单例模式，确保全局只有一个实例
 * 线程安全，支持配置热加载和变更通知
 */
class ConfigManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 获取单例实例
     * @return ConfigManager& 单例引用
     */
    static ConfigManager& instance();

    // 禁用拷贝和赋值
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    /**
     * @brief 初始化配置管理器
     * @param configPath 配置文件路径（默认为 ./config.json）
     * @return bool 初始化成功返回 true，失败返回 false
     */
    bool initialize(const QString& configPath = "");

    // ========== 配置文件操作 ==========
    
    /**
     * @brief 加载配置文件
     * @return bool 加载成功返回 true，失败返回 false
     */
    bool load();
    
    /**
     * @brief 保存配置到文件
     * @return bool 保存成功返回 true，失败返回 false
     */
    bool save();
    
    /**
     * @brief 重新加载配置文件
     * @return bool 重新加载成功返回 true，失败返回 false
     */
    bool reload();

    // ========== 类型安全的配置读取 ==========
    
    /**
     * @brief 读取字符串类型配置
     * @param key 配置键名（支持点号分隔的嵌套键，如 "server.port"）
     * @param defaultValue 默认值
     * @return QString 配置值或默认值
     */
    QString getString(const QString& key, const QString& defaultValue = "") const;
    
    /**
     * @brief 读取整数类型配置
     * @param key 配置键名
     * @param defaultValue 默认值
     * @return int 配置值或默认值
     */
    int getInt(const QString& key, int defaultValue = 0) const;
    
    /**
     * @brief 读取布尔类型配置
     * @param key 配置键名
     * @param defaultValue 默认值
     * @return bool 配置值或默认值
     */
    bool getBool(const QString& key, bool defaultValue = false) const;
    
    /**
     * @brief 读取浮点数类型配置
     * @param key 配置键名
     * @param defaultValue 默认值
     * @return double 配置值或默认值
     */
    double getDouble(const QString& key, double defaultValue = 0.0) const;

    // ========== 配置写入 ==========
    
    /**
     * @brief 设置字符串类型配置
     * @param key 配置键名
     * @param value 配置值
     */
    void setString(const QString& key, const QString& value);
    
    /**
     * @brief 设置整数类型配置
     * @param key 配置键名
     * @param value 配置值
     */
    void setInt(const QString& key, int value);
    
    /**
     * @brief 设置布尔类型配置
     * @param key 配置键名
     * @param value 配置值
     */
    void setBool(const QString& key, bool value);
    
    /**
     * @brief 设置浮点数类型配置
     * @param key 配置键名
     * @param value 配置值
     */
    void setDouble(const QString& key, double value);

    // ========== 配置验证 ==========
    
    /**
     * @brief 验证配置的有效性
     * @return bool 配置有效返回 true，无效返回 false
     */
    bool validate();
    
    /**
     * @brief 获取配置验证错误信息
     * @return QStringList 验证错误列表
     */
    QStringList getValidationErrors() const;

    // ========== 配置路径 ==========
    
    /**
     * @brief 获取当前配置文件路径
     * @return QString 配置文件路径
     */
    QString getConfigPath() const;

signals:
    /**
     * @brief 配置变更信号
     * @param key 变更的配置键名
     */
    void configChanged(const QString& key);
    
    /**
     * @brief 配置重新加载信号
     */
    void configReloaded();

private:
    /**
     * @brief 私有构造函数（单例模式）
     */
    ConfigManager();
    
    /**
     * @brief 析构函数
     */
    ~ConfigManager();

    /**
     * @brief 从 Qt 资源文件加载默认配置
     * @return bool 加载成功返回 true，失败返回 false
     */
    bool loadDefaultFromResource();
    
    /**
     * @brief 创建默认配置文件（从资源文件复制）
     */
    void createDefaultConfig();

    /**
     * @brief 验证端口号
     * @param port 端口号
     * @return bool 端口号有效返回 true，无效返回 false
     */
    bool validatePort(int port);
    
    /**
     * @brief 验证文件路径
     * @param path 文件路径
     * @return bool 路径有效返回 true，无效返回 false
     */
    bool validatePath(const QString& path);
    
    /**
     * @brief 验证日志级别
     * @param level 日志级别
     * @return bool 日志级别有效返回 true，无效返回 false
     */
    bool validateLogLevel(const QString& level);

    // ========== 成员变量 ==========
    
    QString m_configPath;              // 配置文件路径
    QJsonObject m_config;              // 配置数据
    mutable QMutex m_mutex;            // 线程安全互斥锁
    QStringList m_validationErrors;    // 验证错误列表
};

#endif // CONFIGMANAGER_H
