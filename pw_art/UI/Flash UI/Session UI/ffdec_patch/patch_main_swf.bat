@echo off
rem Rebuild main.swf WITHOUT Flash CS6: compile the classes from this folder into the CS6 build of main.swf (JPEXS FFDec 26.3.0+, Java).
rem   patch_main_swf.bat C:\tools\ffdec\ffdec.jar path\to\cs6_build_main.swf
rem Result: ..\main.swf and pw\branches\r1117\Data\UI\Screens\Combat\Flash\main.swf
setlocal
set "FFDEC=%~1"
set "SRC=%~2"
if "%FFDEC%"=="" (echo Usage: %~nx0 path\to\ffdec.jar path\to\cs6_build_main.swf & exit /b 1)
if "%SRC%"=="" (echo Usage: %~nx0 path\to\ffdec.jar path\to\cs6_build_main.swf & exit /b 1)
set "HERE=%~dp0"
set "OUT=%HERE%..\main.swf"
pushd "%HERE%"
java -Xmx3g -jar "%FFDEC%" -replace "%SRC%" "%OUT%" ^
  EscMenu.Preferences       EscMenu\Preferences.as ^
  EscMenu.OptionsWindow     EscMenu\OptionsWindow.as ^
  TalentBar.Talent          TalentBar\Talent.as ^
  TalentBar.TalentShortCut  TalentBar\TalentShortCut.as ^
  ActionBar.ActionButton    ActionBar\ActionButton.as ^
  Chat.ChatBar              Chat\ChatBar.as ^
  MainInterface             MainInterface.as ^
  Effects.AttentionEffect   Effects\AttentionEffect.as ^
  Controllers.SoundController Controllers\SoundController.as
set ERR=%errorlevel%
popd
if not "%ERR%"=="0" (echo FAILED & exit /b 1)
copy /Y "%OUT%" "%HERE%..\..\..\..\..\pw\branches\r1117\Data\UI\Screens\Combat\Flash\main.swf" >nul
echo main.swf rebuilt.
