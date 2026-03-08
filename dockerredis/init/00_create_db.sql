-- 변수: $(DBNAME)

IF DB_ID(N'$(DBNAME)') IS NULL
BEGIN
    DECLARE @sql NVARCHAR(MAX) =
        N'CREATE DATABASE [' + REPLACE(N'$(DBNAME)', N']', N']]') +
        N'] COLLATE Korean_100_CI_AS_SC_UTF8;';
    EXEC (@sql);
END
GO