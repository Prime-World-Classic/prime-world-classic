@echo off
rem Recompile patched ActionScript classes into the combat UI main.swf (JPEXS FFDec required).
rem   patch_main_swf.bat C:\tools\ffdec\ffdec.jar
setlocal
set "FFDEC=%~1"
if "%FFDEC%"=="" (echo Usage: %~nx0 path\to\ffdec.jar & exit /b 1)
set "HERE=%~dp0"
set "SWFDIR=%HERE%..\..\..\..\..\pw\branches\r1117\Data\UI\Screens\Combat\Flash"
if not exist "%SWFDIR%\main.swf.orig" copy /Y "%SWFDIR%\main.swf" "%SWFDIR%\main.swf.orig" >nul
java -Xmx3g -jar "%FFDEC%" -replace "%SWFDIR%\main.swf.orig" "%SWFDIR%\main.swf" ^
  EscMenu.Preferences   "%HERE%EscMenu\Preferences.as" ^
  EscMenu.OptionsWindow "%HERE%EscMenu\OptionsWindow.as" ^
  TalentBar.Talent      "%HERE%TalentBar\Talent.as" ^
  ActionBar.ActionButton "%HERE%ActionBar\ActionButton.as" ^
  Chat.ChatBar          "%HERE%Chat\ChatBar.as" ^
  MainInterface         "%HERE%MainInterface.as" ^
  Effects.AttentionEffect "%HERE%Effects\AttentionEffect.as" ^
  TalentBar.TalentShortCut "%HERE%TalentBar\TalentShortCut.as"
if errorlevel 1 (echo FAILED & exit /b 1)
echo main.swf rebuilt.
