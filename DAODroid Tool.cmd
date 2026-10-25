@echo off
rem DAODroid's PC tool: your own disc image in, a game package for the launcher out.
rem Needs Python 3.10+ (python.org; "Add python.exe to PATH" when installing).
cd /d "%~dp0"
where python >nul 2>nul || (
    echo Python isn't installed: get it from https://www.python.org/downloads/ and tick "Add python.exe to PATH".
    pause
    exit /b 1
)
start "" pythonw -I scripts\daodroid_tool_gui.py || python -I scripts\daodroid_tool_gui.py
