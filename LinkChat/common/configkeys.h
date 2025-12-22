#ifndef CONFIGKEYS_H
#define CONFIGKEYS_H

#include <QString>

namespace ConfigKeys {
    // 服务器配置
    namespace Server {
        const QString PORT = "server.port";
        const QString MAX_CONNECTIONS = "server.max_connections";
        const QString HEARTBEAT_INTERVAL = "server.heartbeat_interval";
        const QString HEARTBEAT_TIMEOUT = "server.heartbeat_timeout";
        
        namespace Database {
            const QString HOST = "server.database.host";
            const QString PORT = "server.database.port";
            const QString USERNAME = "server.database.username";
            const QString PASSWORD = "server.database.password";
            const QString DATABASE = "server.database.database";
        }
        
        namespace Log {
            const QString LEVEL = "server.log.level";
            const QString FILE_PATH = "server.log.file_path";
        }
    }
    
    // 客户端配置
    namespace Client {
        const QString SERVER_ADDRESS = "client.server_address";
        const QString SERVER_PORT = "client.server_port";
        const QString AUTO_RECONNECT = "client.auto_reconnect";
        const QString RECONNECT_MAX_ATTEMPTS = "client.reconnect_max_attempts";
        const QString RECONNECT_INTERVAL = "client.reconnect_interval";
        const QString HEARTBEAT_INTERVAL = "client.heartbeat_interval";
        const QString HEARTBEAT_TIMEOUT = "client.heartbeat_timeout";
        const QString FILE_SAVE_PATH = "client.file_save_path";
        const QString LANGUAGE = "client.language";
    }
}

#endif // CONFIGKEYS_H
