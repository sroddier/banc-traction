@echo off
REM Recopie web\ vers firmware\data\ (Windows)
cd /d "%~dp0"
python sync_web.py
if errorlevel 1 py -3 sync_web.py
