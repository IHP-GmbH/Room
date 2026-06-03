@echo off
rem Build Cap'n Proto into third_party/capnp-install (run once before cmake).
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "ROOT=%ROOT:\=/%"
call "%~dp0build_capnp_windows.bat" ^
  "https://github.com/capnproto/capnproto.git" branch master "" "" ^
  "%ROOT%/third_party/capnproto" "%ROOT%/third_party/capnp-install"
exit /b %ERRORLEVEL%
