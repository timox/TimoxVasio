!macro customInstall
  ; NSIS runs as a 32-bit process. Sysnative reaches 64-bit PowerShell so HKLM\SOFTWARE
  ; is not redirected to Wow6432Node when registering the x64 ASIO driver.
  StrCpy $R1 "$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
  ${IfNot} ${FileExists} "$R1"
    StrCpy $R1 "$SYSDIR\WindowsPowerShell\v1.0\powershell.exe"
  ${EndIf}
  File /oname=$PLUGINSDIR\TimoxVasio.dll "${PROJECT_DIR}\..\build_driver_110\Release\TimoxVasio.dll"
  File /oname=$PLUGINSDIR\setup-driver.ps1 "${PROJECT_DIR}\..\install\setup-driver.ps1"
  File /oname=$PLUGINSDIR\register_drivers.ps1 "${PROJECT_DIR}\..\install\register_drivers.ps1"
  ExecWait '"$R1" -NoProfile -ExecutionPolicy Bypass -File "$PLUGINSDIR\setup-driver.ps1" install -SourceDll "$PLUGINSDIR\TimoxVasio.dll"' $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "L'installation du pilote ASIO a échoué (code $0). L'installation de Control est annulée."
    Abort
  ${EndIf}
!macroend

!macro customUnInstall
  StrCpy $R1 "$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
  ${IfNot} ${FileExists} "$R1"
    StrCpy $R1 "$SYSDIR\WindowsPowerShell\v1.0\powershell.exe"
  ${EndIf}
  File /oname=$PLUGINSDIR\setup-driver.ps1 "${PROJECT_DIR}\..\install\setup-driver.ps1"
  File /oname=$PLUGINSDIR\register_drivers.ps1 "${PROJECT_DIR}\..\install\register_drivers.ps1"
  ExecWait '"$R1" -NoProfile -ExecutionPolicy Bypass -File "$PLUGINSDIR\setup-driver.ps1" uninstall' $0
  ${If} $0 != 0
    MessageBox MB_ICONEXCLAMATION "Le pilote ASIO n'a pas pu être désinstallé (code $0). Vérifiez son enregistrement avant de le supprimer manuellement."
  ${EndIf}
!macroend
