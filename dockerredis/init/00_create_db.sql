-- 변수: $(DBNAME)

IF DB_ID(N'$(DBNAME)') IS NULL
BEGIN
    DECLARE @sql NVARCHAR(MAX) = N'CREATE DATABASE [' + REPLACE(N'$(DBNAME)', N']', N']]') + N'];';
    EXEC (@sql);
END
GO