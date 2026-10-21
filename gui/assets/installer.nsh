!macro customInstall
  ; NSIS itself is 32-bit. Write the native ASIO and COM keys explicitly.
  SetRegView 64
  CreateDirectory "$PROGRAMFILES64\Steinberg\VirtualASIO\1.2.7"
  SetOutPath "$PROGRAMFILES64\Steinberg\VirtualASIO\1.2.7"
  SetOverwrite ifnewer
  ClearErrors
  File /oname=TimoxVasio.dll "${PROJECT_DIR}\..\build_driver_110\Release\TimoxVasio.dll"
  ${If} ${Errors}
    ${IfNot} ${Silent}
      MessageBox MB_ICONSTOP "La copie du pilote ASIO a échoué. L'installation de Control est annulée."
    ${EndIf}
    Abort
  ${EndIf}
  SetOutPath "$INSTDIR"
  SetOverwrite on

  DeleteRegKey HKLM "SOFTWARE\ASIO\VASIO1"
  DeleteRegKey HKLM "SOFTWARE\ASIO\VASIO2"
  DeleteRegKey HKLM "SOFTWARE\ASIO\VASIO3"
  DeleteRegKey HKLM "SOFTWARE\ASIO\VASIO4"
  DeleteRegKey HKLM "SOFTWARE\Classes\CLSID\{7A9F4D01-4D7D-4D54-9A61-564153494F31}"
  DeleteRegKey HKLM "SOFTWARE\Classes\CLSID\{7A9F4D02-4D7D-4D54-9A61-564153494F32}"
  DeleteRegKey HKLM "SOFTWARE\Classes\CLSID\{7A9F4D03-4D7D-4D54-9A61-564153494F33}"
  DeleteRegKey HKLM "SOFTWARE\Classes\CLSID\{7A9F4D04-4D7D-4D54-9A61-564153494F34}"
  DeleteRegKey HKLM "SOFTWARE\ASIO\TimoxVasio"
  DeleteRegKey HKLM "SOFTWARE\Classes\CLSID\{A4D39126-78CB-4D89-9E0A-54494D4F5856}"
  ClearErrors
  WriteRegStr HKLM "SOFTWARE\ASIO\TimoxVasio" "CLSID" "{A4D39126-78CB-4D89-9E0A-54494D4F5856}"
  WriteRegStr HKLM "SOFTWARE\ASIO\TimoxVasio" "Description" "TimoxVasio"
  WriteRegStr HKLM "SOFTWARE\Classes\CLSID\{A4D39126-78CB-4D89-9E0A-54494D4F5856}" "" "TimoxVasio"
  WriteRegStr HKLM "SOFTWARE\Classes\CLSID\{A4D39126-78CB-4D89-9E0A-54494D4F5856}\InprocServer32" "" "$PROGRAMFILES64\Steinberg\VirtualASIO\1.2.7\TimoxVasio.dll"
  WriteRegStr HKLM "SOFTWARE\Classes\CLSID\{A4D39126-78CB-4D89-9E0A-54494D4F5856}\InprocServer32" "ThreadingModel" "Apartment"
  ${If} ${Errors}
    ${IfNot} ${Silent}
      MessageBox MB_ICONSTOP "L'enregistrement du pilote ASIO a échoué. L'installation de Control est annulée."
    ${EndIf}
    Abort
  ${EndIf}
!macroend

!macro customUnInstall
  ; Upgrades must retain the driver until customInstall writes the new version.
  ${IfNot} ${isUpdated}
    SetRegView 64
    ReadRegStr $R0 HKLM "SOFTWARE\Classes\CLSID\{A4D39126-78CB-4D89-9E0A-54494D4F5856}\InprocServer32" ""
    ${If} $R0 == "$PROGRAMFILES64\Steinberg\VirtualASIO\1.2.7\TimoxVasio.dll"
      DeleteRegKey HKLM "SOFTWARE\ASIO\TimoxVasio"
      DeleteRegKey HKLM "SOFTWARE\Classes\CLSID\{A4D39126-78CB-4D89-9E0A-54494D4F5856}"
    ${EndIf}
    Delete "$PROGRAMFILES64\Steinberg\VirtualASIO\1.2.7\TimoxVasio.dll"
    RMDir "$PROGRAMFILES64\Steinberg\VirtualASIO\1.2.7"
  ${EndIf}
!macroend
