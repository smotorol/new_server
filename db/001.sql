SET NOCOUNT ON;
SET XACT_ABORT ON;
GO

/****************************************************************************
 1. DB / 스키마 기본 보장
****************************************************************************/
IF DB_ID('NFX_GAME') IS NULL
    EXEC('CREATE DATABASE [NFX_GAME] COLLATE Korean_100_CI_AS_SC_UTF8');
GO

ALTER DATABASE [NFX_GAME] COLLATE Korean_100_CI_AS_SC_UTF8;
GO

USE [NFX_GAME];
GO

IF NOT EXISTS (SELECT 1 FROM sys.schemas WHERE name = 'game')
    EXEC('CREATE SCHEMA [game]');
GO

/****************************************************************************
 2. game.character 컬럼 추가
 - 이전 버전에는 job / tribe / appearance_code 가 없고
   최신 버전에는 추가됨
****************************************************************************/
IF COL_LENGTH('game.character', 'job') IS NULL
BEGIN
    ALTER TABLE [game].[character]
    ADD [job] SMALLINT NOT NULL
        CONSTRAINT [DF_game_character_job] DEFAULT (0);
END
GO

IF COL_LENGTH('game.character', 'tribe') IS NULL
BEGIN
    ALTER TABLE [game].[character]
    ADD [tribe] SMALLINT NOT NULL
        CONSTRAINT [DF_game_character_tribe] DEFAULT (0);
END
GO

IF COL_LENGTH('game.character', 'appearance_code') IS NULL
BEGIN
    ALTER TABLE [game].[character]
    ADD [appearance_code] INT NOT NULL
        CONSTRAINT [DF_game_character_appearance_code] DEFAULT (0);
END
GO

/****************************************************************************
 3. game.item_template 신규 생성
 - 이전 버전에는 없고 최신 버전에는 존재
****************************************************************************/
IF OBJECT_ID('game.item_template', 'U') IS NULL
BEGIN
    CREATE TABLE [game].[item_template]
    (
        [item_id]                 INT               NOT NULL,
        [equip_part]              SMALLINT          NOT NULL CONSTRAINT [DF_game_item_template_equip_part] DEFAULT (0),
        [equip_tribe]             SMALLINT          NOT NULL CONSTRAINT [DF_game_item_template_equip_tribe] DEFAULT (0),
        [attack]                  INT               NOT NULL CONSTRAINT [DF_game_item_template_attack] DEFAULT (0),
        [defense]                 INT               NOT NULL CONSTRAINT [DF_game_item_template_defense] DEFAULT (0),
        [life]                    INT               NOT NULL CONSTRAINT [DF_game_item_template_life] DEFAULT (0),
        [mana]                    INT               NOT NULL CONSTRAINT [DF_game_item_template_mana] DEFAULT (0),
        [vitality]                INT               NOT NULL CONSTRAINT [DF_game_item_template_vitality] DEFAULT (0),
        [ki]                      INT               NOT NULL CONSTRAINT [DF_game_item_template_ki] DEFAULT (0),
        [is_deleted]              BIT               NOT NULL CONSTRAINT [DF_game_item_template_is_deleted] DEFAULT (0),
        [source_tag]              VARCHAR(32)       NOT NULL CONSTRAINT [DF_game_item_template_source_tag] DEFAULT ('manual'),
        [created_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_game_item_template_created_at] DEFAULT (SYSUTCDATETIME()),
        [updated_at_utc]          DATETIME2(3)      NOT NULL CONSTRAINT [DF_game_item_template_updated_at] DEFAULT (SYSUTCDATETIME()),
        [row_ver]                 ROWVERSION,
        CONSTRAINT [PK_game_item_template] PRIMARY KEY CLUSTERED ([item_id])
    );

    CREATE INDEX [IX_game_item_template_live]
        ON [game].[item_template]([is_deleted], [equip_part], [equip_tribe], [item_id]);
END
GO
