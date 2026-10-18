@echo off
setlocal

where mips64r5900el-ps2-elf-gcc >nul 2>&1
if %ERRORLEVEL% equ 0 (
    if exist "%~dp0melo\bridge2\meloc.exe" (
        "%~dp0melo\bridge2\meloc.exe" %*
        exit /b %ERRORLEVEL%
    )
)

where docker >nul 2>&1
if %ERRORLEVEL% equ 0 (
    docker run --rm -v "%~dp0:/workspace" -w /workspace/melo/bridge2 ps2dev-runner:latest ./meloc %*
    exit /b %ERRORLEVEL%
)

echo ERROR: Neither native PS2 toolchain nor Docker was found in PATH.
exit /b 1
