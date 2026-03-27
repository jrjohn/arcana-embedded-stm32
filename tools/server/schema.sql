-- Arcana IoT MySQL Schema
-- Database: mqtt
-- Used by: Mosquitto go-auth + Registration API

CREATE DATABASE IF NOT EXISTS mqtt;
USE mqtt;

-- Mosquitto auth: user credentials (bcrypt hashed passwords)
CREATE TABLE IF NOT EXISTS `user` (
  `id` mediumint NOT NULL AUTO_INCREMENT,
  `username` varchar(100) NOT NULL,
  `password_hash` varchar(200) NOT NULL,
  `is_admin` tinyint(1) NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Mosquitto auth: topic ACL (rw: 1=read, 2=write, 4=readwrite)
CREATE TABLE IF NOT EXISTS `acl` (
  `id` mediumint NOT NULL AUTO_INCREMENT,
  `user_id` mediumint NOT NULL,
  `topic` varchar(200) NOT NULL,
  `rw` int NOT NULL,
  PRIMARY KEY (`id`),
  KEY `idx_user_id` (`user_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Device registration (TOFU identity)
CREATE TABLE IF NOT EXISTS `device` (
  `id` int NOT NULL AUTO_INCREMENT,
  `device_id` varchar(16) NOT NULL,
  `public_key` varchar(130) NOT NULL,
  `firmware_ver` int DEFAULT '0',
  `registered_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `device_id` (`device_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Device ECDH key tokens (comm_key derivation, zero secrets in DB)
CREATE TABLE IF NOT EXISTS `device_token` (
  `id` int NOT NULL AUTO_INCREMENT,
  `client_id` varchar(48) NOT NULL,
  `token_type` varchar(40) DEFAULT 'ecdh_p256',
  `device_pub` varchar(130) NOT NULL,
  `scope` text,
  `revoked` tinyint(1) DEFAULT '0',
  `issued_at` int NOT NULL,
  `expires_in` int NOT NULL,
  `user_id` int DEFAULT NULL,
  `firmware_ver` varchar(40) DEFAULT NULL,
  `count` int NOT NULL DEFAULT '1',
  `remote_ip` varchar(45) DEFAULT NULL,
  `updatedate` timestamp NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Default admin user for MQTT monitor/test tools
-- Password: arcana (bcrypt hash)
INSERT IGNORE INTO `user` (username, password_hash, is_admin)
VALUES ('arcana', '$2b$10$YourBcryptHashHere', 0);
