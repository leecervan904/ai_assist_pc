@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "MODE=%~1"
if /I "%MODE%"=="help" goto :usage
if /I "%MODE%"=="-h" goto :usage
if /I "%MODE%"=="--help" goto :usage
if /I "%MODE%"=="/?" goto :usage
if "%MODE%"=="" set "MODE=webchannel"

if /I not "%MODE%"=="webchannel" if /I not "%MODE%"=="qml" if /I not "%MODE%"=="webunderlay" (
  echo 未知模式: %MODE%
  goto :usage
)

set "QTDIR=C:\Qt\6.12.0\msvc2022_64"
if not exist "%QTDIR%\bin\qmake.exe" (
  echo 找不到 Qt 6.12.0 MSVC: %QTDIR%
  exit /b 1
)
set "PATH=%QTDIR%\bin;%PATH%"
if exist "C:\Qt\Tools\CMake_64\bin\cmake.exe" set "PATH=C:\Qt\Tools\CMake_64\bin;%PATH%"
if exist "C:\Qt\Tools\Ninja\ninja.exe" set "PATH=C:\Qt\Tools\Ninja;%PATH%"

where cmake >nul 2>&1
if errorlevel 1 (
  echo 找不到 cmake，请先安装 CMake 并加入 PATH。
  exit /b 1
)

cmake --preset vs2022
if errorlevel 1 exit /b 1
cmake --build --preset debug
if errorlevel 1 exit /b 1

set "EXE=%~dp0build\Debug\SonoWorkstation.exe"
if not exist "%EXE%" (
  echo 编译完成但未找到 %EXE%
  exit /b 1
)

echo 启动模式: %MODE%
if /I "%~2"=="bench" (
  echo 自动测 1280x720 FPS 12 秒后退出，结果写在 build\Debug\bench-%MODE%-1280x720.json
  "%EXE%" --mode %MODE% --bench 12 --size 1280x720
) else if /I "%~2"=="verify" (
  echo 全流程自测，结果写在 build\Debug\selftest-%MODE%.json，日志 sono.log
  "%EXE%" --mode %MODE% --verify
) else (
  start "" "%EXE%" --mode %MODE%
)
endlocal
exit /b 0

:usage
echo.
echo 用法: run-windows.bat [webchannel^|qml^|webunderlay] [bench^|verify]
echo.
echo   webchannel   C++ 采集 -^> QVideoWidget GPU 直出，WebChannel 只传控制  ^(默认^)
echo   qml          C++ 采集 -^> QML VideoOutput GPU 直出
echo   webunderlay  原型：视频/AI HUD 垫底，WebEngine 透明洞，测量走网页
echo.
echo 连接采集卡后请选择 1280x720 @ 240（1K），底部状态栏显示 采集/显示/推理 FPS。
echo 加 bench 会自动打开采集卡并写出 JSON。
exit /b 1
