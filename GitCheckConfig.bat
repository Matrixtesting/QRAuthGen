@echo off
setlocal

echo.
echo === Git Repository Configuration Check ===
echo.

:: --- Pre-flight Checks ---
where git >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] Git command not found.
    goto :end
)
if not exist .git (
    echo [ERROR] This directory is NOT a Git repository.
    goto :end
)

:: =================================================================
:: 1. Local Identity (Who are you?)
:: =================================================================

echo --- 1. Local User Identity (for this repository) ---
echo User Name:
git config user.name
echo User Email:
git config user.email
echo.

:: =================================================================
:: 2. Remote Configuration (Where do you push/pull?)
:: =================================================================

echo --- 2. Remote Configuration (URLs) ---
:: Displays the fetch and push URLs for all defined remotes (e.g., origin)
git remote -v
echo.

:: =================================================================
:: 3. Branch Status (What are you working on?)
:: =================================================================

echo --- 3. Current Branch and Tracking Status ---
:: -vv shows the tracking branch (e.g., [origin/main])
:: -a shows all branches, including remotes
git branch -vv -a
echo.

:: =================================================================
:: 4. Status Check
:: =================================================================

echo --- 4. Working Directory Status ---
:: Quick check for any untracked or modified files
git status -s

echo.
echo === Configuration Check Complete ===

:end
endlocal
pause