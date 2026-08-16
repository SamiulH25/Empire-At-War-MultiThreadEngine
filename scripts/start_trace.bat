@echo off
rem Start a WPR CPU sampling trace for the EAW dynamic analysis.
rem Usage (as Administrator): start_trace.bat
wpr -start CPU -filemode -recordtempto C:\Temp\eaw_trace.etl
if errorlevel 1 (
    echo FAILED to start WPR. Run this as Administrator.
    exit /b 1
)
echo Trace recording started. Play the battle, then run: wpr -stop C:\Temp\eaw_trace.etl
