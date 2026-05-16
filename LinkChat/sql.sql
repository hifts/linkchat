CREATE DATABASE IF NOT EXISTS linkchat CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE linkchat;

-- 1. 用户表
CREATE TABLE IF NOT EXISTS t_user (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(32) NOT NULL UNIQUE,
    password VARCHAR(64) NOT NULL, -- 实际项目中存 Hash，此处预留够长度存放 SHA-256
    salt VARCHAR(64) DEFAULT NULL, -- 密码加盐防破解
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 2. 好友表 (双向关系)
CREATE TABLE IF NOT EXISTS t_friend (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_id INT NOT NULL,
    friend_id INT NOT NULL,
    UNIQUE KEY uk_friend (user_id, friend_id),
    FOREIGN KEY (user_id) REFERENCES t_user(id) ON DELETE CASCADE,
    FOREIGN KEY (friend_id) REFERENCES t_user(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 3. 离线消息表
CREATE TABLE IF NOT EXISTS t_offline_msg (
    id INT AUTO_INCREMENT PRIMARY KEY,
    sender_id INT NOT NULL,
    receiver_id INT NOT NULL,
    content LONGBLOB, -- 使用 LONGBLOB 存储大容量消息内容及文件数据
    delivered TINYINT DEFAULT 0,
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_receiver (receiver_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 4. 历史消息表
CREATE TABLE IF NOT EXISTS t_chat_history (
    id          BIGINT PRIMARY KEY AUTO_INCREMENT,
    sender_id   INT NOT NULL,
    receiver_id INT NOT NULL,
    content     LONGBLOB, -- 使用 LONGBLOB 支持大型数据(富文本、图片)存储
    send_time   DATETIME DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_pair (sender_id, receiver_id),
    INDEX idx_receiver (receiver_id),
    INDEX idx_time (send_time)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 5. 请求添加好友表
CREATE TABLE IF NOT EXISTS t_friend_request (
    id          BIGINT PRIMARY KEY AUTO_INCREMENT,
    requester_id   INT NOT NULL,        -- 请求者ID
    target_id      INT NOT NULL,        -- 被加的人ID
    requester_name VARCHAR(32) NOT NULL, -- 请求者名字（冗余存一份，免得连表查询）
    request_time   DATETIME DEFAULT CURRENT_TIMESTAMP,
    status         TINYINT DEFAULT 0,   -- 0=待处理, 1=已同意, 2=已拒绝
    UNIQUE KEY uniq_req (requester_id, target_id),  -- 防止同一个人重复请求另一个人
    INDEX idx_target (target_id),
    INDEX idx_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 6. 群聊表
CREATE TABLE IF NOT EXISTS t_groups (
    id INT PRIMARY KEY AUTO_INCREMENT,
    group_name VARCHAR(64) NOT NULL,
    creator_id INT NOT NULL,
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_creator (creator_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 7. 群成员表
CREATE TABLE IF NOT EXISTS t_group_members (
    id INT PRIMARY KEY AUTO_INCREMENT,
    group_id INT NOT NULL,
    user_id INT NOT NULL,
    role TINYINT DEFAULT 0 COMMENT '0=普通成员 1=管理员 2=群主',
    last_delivered_msg_id BIGINT DEFAULT 0,
    last_read_msg_id BIGINT DEFAULT 0,
    join_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE KEY uk_group_user (group_id, user_id),
    INDEX idx_group (group_id),
    INDEX idx_user (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 8. 群聊消息表
CREATE TABLE IF NOT EXISTS t_group_messages (
    id INT PRIMARY KEY AUTO_INCREMENT,
    group_id INT NOT NULL,
    sender_id INT NOT NULL,
    content LONGBLOB NOT NULL, -- 改为 LONGBLOB 防止超长消息或文件截断
    send_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_group_id (group_id, id),
    INDEX idx_group_time (group_id, send_time),
    INDEX idx_sender (sender_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 9. 群聊离线消息表
CREATE TABLE IF NOT EXISTS t_group_offline_msg (
    id INT PRIMARY KEY AUTO_INCREMENT,
    group_id INT NOT NULL,
    sender_id INT NOT NULL,
    receiver_id INT NOT NULL COMMENT '接收者（离线的群成员）',
    content LONGBLOB NOT NULL, -- 改为 LONGBLOB 防止截断
    delivered TINYINT DEFAULT 0,
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_receiver (receiver_id),
    INDEX idx_group (group_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ==========================================
-- 可选：预置测试数据 (仅供首次演示参考使用)
-- 实际项目中 password 会包含 Hash，此处插入为简单明文以便于直接观察
-- ==========================================
-- Demo password for all users: 123456.
INSERT IGNORE INTO t_user (username, password, salt) VALUES
('admin', 'ejFdPCK7V6CmLLNj9BXEx18/nJS4sLqbLcSGAwOWoJc=', 'WQyUYFtYcOv5jNDcbUwJ+Q=='),
('user1', '+HcVE30yhVxD6+bezse6YWA6168y18JV050GSoRNCeE=', 'YTeNYbL+wsQJgWg9AbslJA=='),
('user2', 'gU3uXRdOO8ZXo9NKNZn/jCGdEexmHQxhEIYQRC0zIrI=', 'wKVCgvQvRT0ZjslQKgdkrw==');

-- 预置好友关系 (注意：t_friend表关联了外键，此处假设上面三条插入后的自增ID依次为 1,2,3)
INSERT IGNORE INTO t_friend (user_id, friend_id) VALUES (1, 2), (2, 1);
