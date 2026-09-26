@echo off
setlocal

echo.
echo === Starting Git Stage and Commit Operation ===
echo.

:: Check for Git
where git >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Git command not found.
    goto :end
)

:: Check for existing repo
if not exist .git (
    echo [ERROR] This directory is NOT a Git repository.
    goto :end
)

:: =================================================================
:: 1. Stage All Changes (Git Add .)
:: =================================================================

echo 1. Checking for uncommitted changes...
git status --porcelain | findstr /R /C:".*" >nul

if errorlevel 1 (
    echo [INFO] No new or modified changes detected. Skipping commit.
    goto :end
)

echo 2. Staging all modified, new, and deleted files (git add .)...
git add .

if %errorlevel% neq 0 (
    echo [ERROR] Git Add failed. Aborting commit.
    goto :end
)

:: =================================================================
:: 2. Commit Staged Changes
:: =================================================================

:: Prompt for a commit message
:prompt_message
set /p COMMIT_MESSAGE="Enter your commit message (e.g., 'Feat: Added new feature'): "

if "%COMMIT_MESSAGE%"=="" (
    echo [WARNING] Commit message cannot be empty.
    goto :prompt_message
)

echo 3. Committing staged changes with message: "%COMMIT_MESSAGE%"
git commit -m "%COMMIT_MESSAGE%"

if %errorlevel% equ 0 (
    echo.
    echo [SUCCESS] Stage and Commit complete.
    echo Run 'GHPushChanges.bat' to upload to the remote repository.
) else (
    echo.
    echo [ERROR] Commit failed.
)

:end
endlocal
pause