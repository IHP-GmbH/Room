@echo off
setlocal EnableExtensions
cd /d "%~dp0.."
git config core.hooksPath .githooks
echo Git hooks path set to .githooks for this repository.
echo Cursor co-author trailers will be stripped on commit.
exit /b 0
