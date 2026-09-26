@echo off
setlocal

:: =================================================================
:: 1. Define Variables
:: =================================================================

:: The remote name (almost always 'origin')
set REMOTE_NAME=origin

echo.
echo === Starting Git Push Operation ===
echo.

:: Check for Git and Repo existence
where git >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Git command not found.
    goto :end
)
if not exist .git (
    echo [ERROR] This directory is NOT a Git repository.
    goto :end
)

:: Get the current branch name
for /f %%i in ('git rev-parse --abbrev-ref HEAD') do set CURRENT_BRANCH=%%i
echo Current working branch: %CURRENT_BRANCH%

:: =================================================================
:: 2. Pre-Check for Uncommitted Changes (SAFETY CHECK)
:: =================================================================

echo 1. Checking for uncommitted or unstaged changes...
git status --porcelain
if not errorlevel 1 (
    :: This loop only runs if git status --porcelain outputs something (i.e., changes exist)
    for /f "delims=" %%l in ('git status --porcelain') do (
        echo [ERROR] Uncommitted or unstaged changes found!
        echo Please run 'GitCommitAll.bat' to commit your work before pushing.
        goto :end
    )
)

:: =================================================================
:: 3. Check for New Local Commits
:: =================================================================

echo 2. Comparing local branch with remote branch...
:: Fetch updates from the remote to accurately compare local vs. remote
git fetch %REMOTE_NAME% >nul 2>&1
pause
:: Check if the local branch has commits that the remote does not have
git log %REMOTE_NAME%/%CURRENT_BRANCH%..%CURRENT_BRANCH% --oneline | findstr /R /C:".*" >nul
if errorlevel 1 (
    echo [INFO] No new local commits to push. Local and remote branches are in sync.
    goto :end
)
pause
:: =================================================================
:: 4. Perform the Push
:: =================================================================

echo 3. Pushing local commits from %CURRENT_BRANCH% to %REMOTE_NAME%...
git push %REMOTE_NAME% %CURRENT_BRANCH%
pause
if %errorlevel% equ 0 (
    echo.
    echo [SUCCESS] Git Push complete. Changes are now on the remote repository.
) else (
    echo.
    echo [ERROR] Git Push failed. Check the messages above for details (e.g., remote rejected update).
) 
pause
:end
endlocal
pause