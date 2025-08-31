@echo off
REM === Git Commit & Push Script ===

REM Prompt user for commit message
set /p commit_msg="Enter commit message: "

REM Add all changes
git add .

REM Commit with the entered message
git commit -m "%commit_msg%"

REM Push to remote (default: origin main)
git push origin main

pause
