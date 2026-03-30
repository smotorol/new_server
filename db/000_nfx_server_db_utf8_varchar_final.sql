/****************************************************************************
 목적
 - 현재 new_server 구조에 맞춘 SQL Server UTF-8/VARCHAR 기반 운영형 초기 스크립트
 - AUTH / GAME / META / LOG 분리
 - Redis write-behind + character blob 저장/복구 + 월드 조회 + 로그인 세션 추적 대응
 - 샘플/실험용 구조보다 운영형 제약조건, 인덱스, 필터드 유니크 인덱스를 보강

 주요 변경점
 - 모든 문자열 컬럼: VARCHAR + UTF-8 DB Collation
 - N'' / NVARCHAR 제거
 - 캐릭터명 / 슬롯 유니크: 삭제 캐릭터 제외 필터드 인덱스 적용
 - 자주 조회하는 경로(account/world/state, blob world/update, session account/time) 인덱스 보강
 - 좌표 타입은 서버 저장용으로 REAL 사용
 - Proc는 현재 C++ ODBC 호출 포맷에 맞게 단순/안전하게 유지
****************************************************************************/

SET NOCOUNT ON;
SET XACT_ABORT ON;
GO

/****************************************************************************
 0. DB 생성 및 UTF-8 Collation 고정
****************************************************************************/
IF DB_ID('NFX_AUTH') IS NULL
    EXEC('CREATE DATABASE [NFX_AUTH] COLLATE Korean_100_CI_AS_SC_UTF8');
GO
IF DB_ID('NFX_GAME') IS NULL
    EXEC('CREATE DATABASE [NFX_GAME] COLLATE Korean_100_CI_AS_SC_UTF8');
GO
IF DB_ID('NFX_META') IS NULL
    EXEC('CREATE DATABASE [NFX_META] COLLATE Korean_100_CI_AS_SC_UTF8');
GO
IF DB_ID('NFX_LOG') IS NULL
    EXEC('CREATE DATABASE [NFX_LOG] COLLATE Korean_100_CI_AS_SC_UTF8');
GO

ALTER DATABASE [NFX_AUTH] COLLATE Korean_100_CI_AS_SC_UTF8;
GO
ALTER DATABASE [NFX_GAME] COLLATE Korean_100_CI_AS_SC_UTF8;
GO
ALTER DATABASE [NFX_META] COLLATE Korean_100_CI_AS_SC_UTF8;
GO
ALTER DATABASE [NFX_LOG] COLLATE Korean_100_CI_AS_SC_UTF8;
GO

/****************************************************************************
 1. META DB
****************************************************************************/
USE [NFX_META];
GO

IF NOT EXISTS (SELECT 1 FROM sys.schemas WHERE name = 'ref')
    EXEC('CREATE SCHEMA [ref]');
GO

IF OBJECT_ID('ref.world', 'U') IS NULL
BEGIN
    CREATE TABLE [ref].[world]
    (
        [world_code]          INT             NOT NULL,
        [world_name]          VARCHAR(32)     NOT NULL,
        [display_name]        VARCHAR(64)     NOT NULL,
        [is_enabled]          BIT             NOT NULL CONSTRAINT [DF_ref_world_is_enabled] DEFAULT (1),
        [db_key]              VARCHAR(32)     NOT NULL CONSTRAINT [DF_ref_world_db_key] DEFAULT ('default'),
        [created_at_utc]      DATETIME2(3)    NOT NULL CONSTRAINT [DF_ref_world_created_at] DEFAULT (SYSUTCDATETIME()),
        [updated_at_utc]      DATETIME2(3)    NOT NULL CONSTRAINT [DF_ref_world_updated_at] DEFAULT (SYSUTCDATETIME()),
        CONSTRAINT [PK_ref_world] PRIMARY KEY CLUSTERED ([world_code]),
        CONSTRAINT [UQ_ref_world_world_name] UNIQUE ([world_name])
    );
END
GO

IF OBJECT_ID('ref.server_node', 'U') IS NULL
BEGIN
    CREATE TABLE [ref].[server_node]
    (
        [server_node_id]      BIGINT          NOT NULL IDENTITY(1,1),
        [world_code]          INT             NOT NULL,
        [server_role]         VARCHAR(20)     NOT NULL, -- login / world / channel / control / log
        [node_name]           VARCHAR(64)     NOT NULL,
        [host_name]           VARCHAR(128)    NOT NULL,
        [bind_ip]             VARCHAR(64)     NOT NULL,
        [bind_port]           INT             NOT NULL,
        [is_enabled]          BIT             NOT NULL CONSTRAINT [DF_ref_server_node_is_enabled] DEFAULT (1),
        [created_at_utc]      DATETIME2(3)    NOT NULL CONSTRAINT [DF_ref_server_node_created_at] DEFAULT (SYSUTCDATETIME()),
        CONSTRAINT [PK_ref_server_node] PRIMARY KEY CLUSTERED ([server_node_id]),
        CONSTRAINT [FK_ref_server_node_world] FOREIGN KEY ([world_code]) REFERENCES [ref].[world]([world_code]),
        CONSTRAINT [CK_ref_server_node_role] CHECK ([server_role] IN ('login', 'world', 'channel', 'control', 'log')),
        CONSTRAINT [CK_ref_server_node_port] CHECK ([bind_port] BETWEEN 1 AND 65535)
    );

    CREATE UNIQUE INDEX [UX_ref_server_node_world_role_name]
        ON [ref].[server_node]([world_code], [server_role], [node_name]);
END
GO

IF NOT EXISTS (SELECT 1 FROM [ref].[world] WHERE [world_code] = 0)
BEGIN
    INSERT INTO [ref].[world] ([world_code], [world_name], [display_name], [is_enabled], [db_key])
    VALUES (0, 'DEFAULT_WORLD', '기본 월드', 1, 'default');
END
GO

/****************************************************************************
 2. AUTH DB
****************************************************************************/
USE [NFX_AUTH];
GO

IF NOT EXISTS (SELECT 1 FROM sys.schemas WHERE name = 'auth')
    EXEC('CREATE SCHEMA [auth]');
GO

IF OBJECT_ID('auth.account', 'U') IS NULL
BEGIN
    CREATE TABLE [auth].[account]
    (
        [account_id]              BIGINT            NOT NULL IDENTITY(1,1),
        [login_id]                VARCHAR(64)       NOT NULL,
        [password_hash]           VARBINARY(256)    NULL,
        [password_salt]           VARBINARY(128)    NULL,
        [account_state]           TINYINT           NOT NULL CONSTRAINT [DF_auth_account_state] DEFAULT (1), -- 0:잠금,1:정상,2:휴면,9:탈퇴
        [is_deleted]              BIT               NOT NULL CONSTRAINT [DF_auth_account_is_deleted] DEFAULT (0),
        [last_login_at_utc]       DATETIME2(3)      NULL,
        [created_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_auth_account_created_at] DEFAULT (SYSUTCDATETIME()),
        [updated_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_auth_account_updated_at] DEFAULT (SYSUTCDATETIME()),
        [row_ver]                 ROWVERSION,
        CONSTRAINT [PK_auth_account] PRIMARY KEY CLUSTERED ([account_id]),
        CONSTRAINT [CK_auth_account_state] CHECK ([account_state] IN (0,1,2,9))
    );

    CREATE UNIQUE INDEX [UX_auth_account_login_id_active]
        ON [auth].[account]([login_id])
        WHERE [is_deleted] = 0;
END
GO

IF OBJECT_ID('auth.account_ban', 'U') IS NULL
BEGIN
    CREATE TABLE [auth].[account_ban]
    (
        [account_ban_id]          BIGINT            NOT NULL IDENTITY(1,1),
        [account_id]              BIGINT            NOT NULL,
        [ban_reason]              VARCHAR(400)      NOT NULL,
        [start_at_utc]            DATETIME2(3)      NOT NULL,
        [end_at_utc]              DATETIME2(3)      NULL,
        [created_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_auth_account_ban_created_at] DEFAULT (SYSUTCDATETIME()),
        CONSTRAINT [PK_auth_account_ban] PRIMARY KEY CLUSTERED ([account_ban_id]),
        CONSTRAINT [FK_auth_account_ban_account] FOREIGN KEY ([account_id]) REFERENCES [auth].[account]([account_id])
    );

    CREATE INDEX [IX_auth_account_ban_account_id]
        ON [auth].[account_ban]([account_id], [start_at_utc] DESC);
END
GO

IF OBJECT_ID('auth.login_session', 'U') IS NULL
BEGIN
    CREATE TABLE [auth].[login_session]
    (
        [session_id]              UNIQUEIDENTIFIER  NOT NULL CONSTRAINT [DF_auth_login_session_id] DEFAULT (NEWSEQUENTIALID()),
        [account_id]              BIGINT            NOT NULL,
        [world_code]              INT               NULL,
        [session_state]           TINYINT           NOT NULL CONSTRAINT [DF_auth_login_session_state] DEFAULT (1), -- 1:active,2:closed,3:kicked
        [client_ip]               VARCHAR(64)       NULL,
        [client_platform]         VARCHAR(20)       NULL,
        [issued_at_utc]           DATETIME2(3)      NOT NULL CONSTRAINT [DF_auth_login_session_issued_at] DEFAULT (SYSUTCDATETIME()),
        [closed_at_utc]           DATETIME2(3)      NULL,
        [last_seen_at_utc]        DATETIME2(3)      NULL,
        CONSTRAINT [PK_auth_login_session] PRIMARY KEY CLUSTERED ([session_id]),
        CONSTRAINT [FK_auth_login_session_account] FOREIGN KEY ([account_id]) REFERENCES [auth].[account]([account_id]),
        CONSTRAINT [CK_auth_login_session_state] CHECK ([session_state] IN (1,2,3))
    );

    CREATE INDEX [IX_auth_login_session_account_id]
        ON [auth].[login_session]([account_id], [issued_at_utc] DESC);

    CREATE INDEX [IX_auth_login_session_world_state]
        ON [auth].[login_session]([world_code], [session_state], [issued_at_utc] DESC);
END
GO

/****************************************************************************
 3. GAME DB
****************************************************************************/
USE [NFX_GAME];
GO

IF NOT EXISTS (SELECT 1 FROM sys.schemas WHERE name = 'game')
    EXEC('CREATE SCHEMA [game]');
GO

IF OBJECT_ID('game.character', 'U') IS NULL
BEGIN
    CREATE TABLE [game].[character]
    (
        [char_id]                 BIGINT            NOT NULL IDENTITY(1,1),
        [account_id]              BIGINT            NOT NULL,
        [world_code]              INT               NOT NULL,
        [char_name]               VARCHAR(20)       NOT NULL,
        [slot_no]                 TINYINT           NOT NULL,
        [char_state]              TINYINT           NOT NULL CONSTRAINT [DF_game_character_state] DEFAULT (1), -- 1:정상,2:삭제대기,9:삭제
        [level]                   INT               NOT NULL CONSTRAINT [DF_game_character_level] DEFAULT (1),
        [exp]                     BIGINT            NOT NULL CONSTRAINT [DF_game_character_exp] DEFAULT (0),
        [gold]                    BIGINT            NOT NULL CONSTRAINT [DF_game_character_gold] DEFAULT (0),
        [zone_id]                 INT               NOT NULL CONSTRAINT [DF_game_character_zone_id] DEFAULT (1),
        [pos_x]                   REAL              NOT NULL CONSTRAINT [DF_game_character_pos_x] DEFAULT (0),
        [pos_y]                   REAL              NOT NULL CONSTRAINT [DF_game_character_pos_y] DEFAULT (0),
        [pos_z]                   REAL              NOT NULL CONSTRAINT [DF_game_character_pos_z] DEFAULT (0),
        [hp]                      INT               NOT NULL CONSTRAINT [DF_game_character_hp] DEFAULT (100),
        [mp]                      INT               NOT NULL CONSTRAINT [DF_game_character_mp] DEFAULT (100),
        [version_no]              BIGINT            NOT NULL CONSTRAINT [DF_game_character_version_no] DEFAULT (1),
        [last_login_at_utc]       DATETIME2(3)      NULL,
        [created_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_game_character_created_at] DEFAULT (SYSUTCDATETIME()),
        [updated_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_game_character_updated_at] DEFAULT (SYSUTCDATETIME()),
        [row_ver]                 ROWVERSION,
        CONSTRAINT [PK_game_character] PRIMARY KEY CLUSTERED ([char_id]),
        CONSTRAINT [CK_game_character_state] CHECK ([char_state] IN (1,2,9)),
        CONSTRAINT [CK_game_character_slot_no] CHECK ([slot_no] BETWEEN 0 AND 31),
        CONSTRAINT [CK_game_character_level] CHECK ([level] >= 1),
        CONSTRAINT [CK_game_character_hp] CHECK ([hp] >= 0),
        CONSTRAINT [CK_game_character_mp] CHECK ([mp] >= 0)
    );

    CREATE UNIQUE INDEX [UX_game_character_world_name_live]
        ON [game].[character]([world_code], [char_name])
        WHERE [char_state] <> 9;

    CREATE UNIQUE INDEX [UX_game_character_account_slot_live]
        ON [game].[character]([account_id], [world_code], [slot_no])
        WHERE [char_state] <> 9;

    CREATE INDEX [IX_game_character_account_world_state]
        ON [game].[character]([account_id], [world_code], [char_state], [char_id]);

    CREATE INDEX [IX_game_character_world_zone]
        ON [game].[character]([world_code], [zone_id], [char_state]);
END
GO

IF OBJECT_ID('game.character_state_blob', 'U') IS NULL
BEGIN
    CREATE TABLE [game].[character_state_blob]
    (
        [char_id]                 BIGINT            NOT NULL,
        [world_code]              INT               NOT NULL,
        [blob_version]            INT               NOT NULL CONSTRAINT [DF_game_character_state_blob_version] DEFAULT (1),
        [state_blob]              VARBINARY(MAX)    NOT NULL,
        [state_hash]              VARBINARY(32)     NULL,
        [updated_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_game_character_state_blob_updated_at] DEFAULT (SYSUTCDATETIME()),
        CONSTRAINT [PK_game_character_state_blob] PRIMARY KEY CLUSTERED ([char_id]),
        CONSTRAINT [FK_game_character_state_blob_character] FOREIGN KEY ([char_id]) REFERENCES [game].[character]([char_id]),
        CONSTRAINT [CK_game_character_state_blob_version] CHECK ([blob_version] >= 1)
    );

    CREATE INDEX [IX_game_character_state_blob_world_code]
        ON [game].[character_state_blob]([world_code], [updated_at_utc] DESC);
END
GO

IF OBJECT_ID('game.character_item', 'U') IS NULL
BEGIN
    CREATE TABLE [game].[character_item]
    (
        [item_uid]                BIGINT            NOT NULL IDENTITY(1,1),
        [char_id]                 BIGINT            NOT NULL,
        [item_id]                 INT               NOT NULL,
        [item_count]              BIGINT            NOT NULL CONSTRAINT [DF_game_character_item_count] DEFAULT (1),
        [slot_type]               TINYINT           NOT NULL CONSTRAINT [DF_game_character_item_slot_type] DEFAULT (1), -- 1:inventory,2:equip,3:warehouse
        [slot_no]                 INT               NULL,
        [is_deleted]              BIT               NOT NULL CONSTRAINT [DF_game_character_item_is_deleted] DEFAULT (0),
        [created_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_game_character_item_created_at] DEFAULT (SYSUTCDATETIME()),
        [updated_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_game_character_item_updated_at] DEFAULT (SYSUTCDATETIME()),
        [row_ver]                 ROWVERSION,
        CONSTRAINT [PK_game_character_item] PRIMARY KEY CLUSTERED ([item_uid]),
        CONSTRAINT [FK_game_character_item_character] FOREIGN KEY ([char_id]) REFERENCES [game].[character]([char_id]),
        CONSTRAINT [CK_game_character_item_slot_type] CHECK ([slot_type] IN (1,2,3)),
        CONSTRAINT [CK_game_character_item_count] CHECK ([item_count] >= 0)
    );

    CREATE INDEX [IX_game_character_item_char_slot]
        ON [game].[character_item]([char_id], [slot_type], [slot_no])
        INCLUDE ([item_id], [item_count], [is_deleted]);

    CREATE INDEX [IX_game_character_item_char_item]
        ON [game].[character_item]([char_id], [item_id], [is_deleted]);
END
GO

IF OBJECT_ID('game.character_wallet', 'U') IS NULL
BEGIN
    CREATE TABLE [game].[character_wallet]
    (
        [char_id]                 BIGINT            NOT NULL,
        [gold]                    BIGINT            NOT NULL CONSTRAINT [DF_game_character_wallet_gold] DEFAULT (0),
        [cash_free]               BIGINT            NOT NULL CONSTRAINT [DF_game_character_wallet_cash_free] DEFAULT (0),
        [cash_paid]               BIGINT            NOT NULL CONSTRAINT [DF_game_character_wallet_cash_paid] DEFAULT (0),
        [mileage]                 BIGINT            NOT NULL CONSTRAINT [DF_game_character_wallet_mileage] DEFAULT (0),
        [updated_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_game_character_wallet_updated_at] DEFAULT (SYSUTCDATETIME()),
        CONSTRAINT [PK_game_character_wallet] PRIMARY KEY CLUSTERED ([char_id]),
        CONSTRAINT [FK_game_character_wallet_character] FOREIGN KEY ([char_id]) REFERENCES [game].[character]([char_id]),
        CONSTRAINT [CK_game_character_wallet_gold] CHECK ([gold] >= 0),
        CONSTRAINT [CK_game_character_wallet_cash_free] CHECK ([cash_free] >= 0),
        CONSTRAINT [CK_game_character_wallet_cash_paid] CHECK ([cash_paid] >= 0),
        CONSTRAINT [CK_game_character_wallet_mileage] CHECK ([mileage] >= 0)
    );
END
GO

IF OBJECT_ID('game.character_quest', 'U') IS NULL
BEGIN
    CREATE TABLE [game].[character_quest]
    (
        [char_id]                 BIGINT            NOT NULL,
        [quest_id]                INT               NOT NULL,
        [quest_state]             TINYINT           NOT NULL, -- 1:진행중,2:완료가능,3:완료
        [progress_json]           VARCHAR(MAX)      NULL,
        [updated_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_game_character_quest_updated_at] DEFAULT (SYSUTCDATETIME()),
        CONSTRAINT [PK_game_character_quest] PRIMARY KEY CLUSTERED ([char_id], [quest_id]),
        CONSTRAINT [FK_game_character_quest_character] FOREIGN KEY ([char_id]) REFERENCES [game].[character]([char_id]),
        CONSTRAINT [CK_game_character_quest_state] CHECK ([quest_state] IN (1,2,3))
    );
END
GO

IF OBJECT_ID('game.character_social', 'U') IS NULL
BEGIN
    CREATE TABLE [game].[character_social]
    (
        [char_id]                 BIGINT            NOT NULL,
        [friend_char_id]          BIGINT            NOT NULL,
        [social_type]             TINYINT           NOT NULL, -- 1:friend,2:block
        [created_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_game_character_social_created_at] DEFAULT (SYSUTCDATETIME()),
        CONSTRAINT [PK_game_character_social] PRIMARY KEY CLUSTERED ([char_id], [friend_char_id], [social_type]),
        CONSTRAINT [FK_game_character_social_character] FOREIGN KEY ([char_id]) REFERENCES [game].[character]([char_id]),
        CONSTRAINT [CK_game_character_social_type] CHECK ([social_type] IN (1,2)),
        CONSTRAINT [CK_game_character_social_not_self] CHECK ([char_id] <> [friend_char_id])
    );

    CREATE INDEX [IX_game_character_social_friend]
        ON [game].[character_social]([friend_char_id], [social_type]);
END
GO

IF OBJECT_ID('game.guild', 'U') IS NULL
BEGIN
    CREATE TABLE [game].[guild]
    (
        [guild_id]                BIGINT            NOT NULL IDENTITY(1,1),
        [world_code]              INT               NOT NULL,
        [guild_name]              VARCHAR(24)       NOT NULL,
        [leader_char_id]          BIGINT            NOT NULL,
        [notice_text]             VARCHAR(400)      NULL,
        [level]                   INT               NOT NULL CONSTRAINT [DF_game_guild_level] DEFAULT (1),
        [created_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_game_guild_created_at] DEFAULT (SYSUTCDATETIME()),
        [updated_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_game_guild_updated_at] DEFAULT (SYSUTCDATETIME()),
        CONSTRAINT [PK_game_guild] PRIMARY KEY CLUSTERED ([guild_id]),
        CONSTRAINT [UQ_game_guild_world_name] UNIQUE ([world_code], [guild_name]),
        CONSTRAINT [CK_game_guild_level] CHECK ([level] >= 1)
    );

    CREATE INDEX [IX_game_guild_world_leader]
        ON [game].[guild]([world_code], [leader_char_id]);
END
GO

IF OBJECT_ID('game.guild_member', 'U') IS NULL
BEGIN
    CREATE TABLE [game].[guild_member]
    (
        [guild_id]                BIGINT            NOT NULL,
        [char_id]                 BIGINT            NOT NULL,
        [member_role]             TINYINT           NOT NULL CONSTRAINT [DF_game_guild_member_role] DEFAULT (1), -- 1:member,2:officer,9:leader
        [joined_at_utc]           DATETIME2(3)      NOT NULL CONSTRAINT [DF_game_guild_member_joined_at] DEFAULT (SYSUTCDATETIME()),
        CONSTRAINT [PK_game_guild_member] PRIMARY KEY CLUSTERED ([guild_id], [char_id]),
        CONSTRAINT [FK_game_guild_member_guild] FOREIGN KEY ([guild_id]) REFERENCES [game].[guild]([guild_id]),
        CONSTRAINT [FK_game_guild_member_character] FOREIGN KEY ([char_id]) REFERENCES [game].[character]([char_id]),
        CONSTRAINT [CK_game_guild_member_role] CHECK ([member_role] IN (1,2,9))
    );

    CREATE UNIQUE INDEX [UX_game_guild_member_char]
        ON [game].[guild_member]([char_id]);
END
GO

IF OBJECT_ID('game.tx_applied', 'U') IS NULL
BEGIN
    CREATE TABLE [game].[tx_applied]
    (
        [tx_id]                   UNIQUEIDENTIFIER  NOT NULL,
        [char_id]                 BIGINT            NOT NULL,
        [tx_type]                 VARCHAR(32)       NOT NULL,
        [applied_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_game_tx_applied_at] DEFAULT (SYSUTCDATETIME()),
        [memo]                    VARCHAR(200)      NULL,
        CONSTRAINT [PK_game_tx_applied] PRIMARY KEY CLUSTERED ([tx_id]),
        CONSTRAINT [FK_game_tx_applied_character] FOREIGN KEY ([char_id]) REFERENCES [game].[character]([char_id])
    );

    CREATE INDEX [IX_game_tx_applied_char_id]
        ON [game].[tx_applied]([char_id], [applied_at_utc] DESC);
END
GO

/****************************************************************************
 4. LOG DB
****************************************************************************/
USE [NFX_LOG];
GO

IF NOT EXISTS (SELECT 1 FROM sys.schemas WHERE name = 'log')
    EXEC('CREATE SCHEMA [log]');
GO

IF OBJECT_ID('log.login_audit', 'U') IS NULL
BEGIN
    CREATE TABLE [log].[login_audit]
    (
        [login_audit_id]          BIGINT            NOT NULL IDENTITY(1,1),
        [account_id]              BIGINT            NULL,
        [login_id]                VARCHAR(64)       NULL,
        [world_code]              INT               NULL,
        [result_code]             INT               NOT NULL,
        [client_ip]               VARCHAR(64)       NULL,
        [user_agent]              VARCHAR(200)      NULL,
        [created_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_log_login_audit_created_at] DEFAULT (SYSUTCDATETIME()),
        CONSTRAINT [PK_log_login_audit] PRIMARY KEY CLUSTERED ([login_audit_id])
    );

    CREATE INDEX [IX_log_login_audit_login_id]
        ON [log].[login_audit]([login_id], [created_at_utc] DESC);
END
GO

IF OBJECT_ID('log.character_save_audit', 'U') IS NULL
BEGIN
    CREATE TABLE [log].[character_save_audit]
    (
        [character_save_audit_id] BIGINT            NOT NULL IDENTITY(1,1),
        [world_code]              INT               NOT NULL,
        [char_id]                 BIGINT            NOT NULL,
        [save_kind]               VARCHAR(20)       NOT NULL, -- flush_dirty / flush_one / manual
        [blob_size]               INT               NOT NULL,
        [result_code]             INT               NOT NULL,
        [error_message]           VARCHAR(4000)     NULL,
        [created_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_log_character_save_audit_created_at] DEFAULT (SYSUTCDATETIME()),
        CONSTRAINT [PK_log_character_save_audit] PRIMARY KEY CLUSTERED ([character_save_audit_id])
    );

    CREATE INDEX [IX_log_character_save_audit_char]
        ON [log].[character_save_audit]([char_id], [created_at_utc] DESC);
END
GO

IF OBJECT_ID('log.server_error', 'U') IS NULL
BEGIN
    CREATE TABLE [log].[server_error]
    (
        [server_error_id]         BIGINT            NOT NULL IDENTITY(1,1),
        [server_role]             VARCHAR(20)       NOT NULL,
        [world_code]              INT               NULL,
        [error_code]              VARCHAR(64)       NULL,
        [error_message]           VARCHAR(4000)     NOT NULL,
        [payload_json]            VARCHAR(MAX)      NULL,
        [created_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_log_server_error_created_at] DEFAULT (SYSUTCDATETIME()),
        CONSTRAINT [PK_log_server_error] PRIMARY KEY CLUSTERED ([server_error_id])
    );

    CREATE INDEX [IX_log_server_error_created]
        ON [log].[server_error]([created_at_utc] DESC);
END
GO

/****************************************************************************
 5. 현재 서버에 바로 붙이기 위한 프로시저
****************************************************************************/
USE [NFX_META];
GO

CREATE OR ALTER PROCEDURE [ref].[usp_open_world_notice]
    @world_name VARCHAR(32)
AS
BEGIN
    SET NOCOUNT ON;

    SELECT TOP (1)
        [world_code],
        [world_name],
        [display_name],
        [is_enabled]
    FROM [ref].[world]
    WHERE [world_name] = @world_name
      AND [is_enabled] = 1;
END
GO

USE [NFX_GAME];
GO

CREATE OR ALTER PROCEDURE [game].[usp_create_character]
    @account_id BIGINT,
    @world_code INT,
    @char_name VARCHAR(20),
    @slot_no TINYINT,
    @char_id BIGINT OUTPUT
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;

    BEGIN TRAN;

    IF EXISTS
    (
        SELECT 1
        FROM [game].[character]
        WHERE [world_code] = @world_code
          AND [char_name] = @char_name
          AND [char_state] <> 9
    )
        THROW 50001, '이미 사용 중인 캐릭터명입니다.', 1;

    IF EXISTS
    (
        SELECT 1
        FROM [game].[character]
        WHERE [account_id] = @account_id
          AND [world_code] = @world_code
          AND [slot_no] = @slot_no
          AND [char_state] <> 9
    )
        THROW 50002, '이미 사용 중인 캐릭터 슬롯입니다.', 1;

    INSERT INTO [game].[character]
    (
        [account_id], [world_code], [char_name], [slot_no]
    )
    VALUES
    (
        @account_id, @world_code, @char_name, @slot_no
    );

    SET @char_id = CONVERT(BIGINT, SCOPE_IDENTITY());

    INSERT INTO [game].[character_wallet]
    (
        [char_id], [gold], [cash_free], [cash_paid], [mileage]
    )
    VALUES
    (
        @char_id, 0, 0, 0, 0
    );

    COMMIT TRAN;
END
GO

CREATE OR ALTER PROCEDURE [game].[usp_get_character_blob]
    @char_id BIGINT
AS
BEGIN
    SET NOCOUNT ON;

    SELECT TOP (1)
        [char_id],
        [world_code],
        [blob_version],
        [state_blob],
        [updated_at_utc]
    FROM [game].[character_state_blob]
    WHERE [char_id] = @char_id;
END
GO

CREATE OR ALTER PROCEDURE [game].[usp_upsert_character_blob]
    @char_id BIGINT,
    @world_code INT,
    @blob_version INT,
    @state_blob VARBINARY(MAX)
AS
BEGIN
    SET NOCOUNT ON;
    SET XACT_ABORT ON;

    UPDATE [game].[character_state_blob]
       SET [world_code] = @world_code,
           [blob_version] = @blob_version,
           [state_blob] = @state_blob,
           [updated_at_utc] = SYSUTCDATETIME()
     WHERE [char_id] = @char_id;

    IF @@ROWCOUNT = 0
    BEGIN
        BEGIN TRY
            INSERT INTO [game].[character_state_blob]
            (
                [char_id], [world_code], [blob_version], [state_blob]
            )
            VALUES
            (
                @char_id, @world_code, @blob_version, @state_blob
            );
        END TRY
        BEGIN CATCH
            IF ERROR_NUMBER() IN (2601, 2627)
            BEGIN
                UPDATE [game].[character_state_blob]
                   SET [world_code] = @world_code,
                       [blob_version] = @blob_version,
                       [state_blob] = @state_blob,
                       [updated_at_utc] = SYSUTCDATETIME()
                 WHERE [char_id] = @char_id;
            END
            ELSE
                THROW;
        END CATCH
    END

    UPDATE [game].[character]
       SET [version_no] = CASE WHEN [version_no] < 9223372036854775807 THEN [version_no] + 1 ELSE [version_no] END,
           [updated_at_utc] = SYSUTCDATETIME()
     WHERE [char_id] = @char_id;
END
GO

/****************************************************************************
 6. 샘플 데이터
****************************************************************************/
USE [NFX_AUTH];
GO

IF NOT EXISTS (SELECT 1 FROM [auth].[account] WHERE [login_id] = 'test01' AND [is_deleted] = 0)
BEGIN
    INSERT INTO [auth].[account] ([login_id], [account_state])
    VALUES ('test01', 1);
END
GO

USE [NFX_GAME];
GO

IF NOT EXISTS
(
    SELECT 1
    FROM [game].[character]
    WHERE [account_id] = 1
      AND [world_code] = 0
      AND [slot_no] = 0
      AND [char_state] <> 9
)
BEGIN
    DECLARE @char_id BIGINT;
    EXEC [game].[usp_create_character]
        @account_id = 1,
        @world_code = 0,
        @char_name = 'TestKnight',
        @slot_no = 0,
        @char_id = @char_id OUTPUT;

    EXEC [game].[usp_upsert_character_blob]
        @char_id = @char_id,
        @world_code = 0,
        @blob_version = 1,
        @state_blob = 0x00000000000000000000000000000000;
END
GO

/****************************************************************************
 7. C++ 코드 연결 포인트
 - open_world_notice
   EXEC [NFX_META].[ref].[usp_open_world_notice] @world_name = ?

 - save_character_blob
   EXEC [NFX_GAME].[game].[usp_upsert_character_blob]
        @char_id=?, @world_code=?, @blob_version=?, @state_blob=?

 - load_character_blob
   EXEC [NFX_GAME].[game].[usp_get_character_blob] @char_id=?
****************************************************************************/

