;NSIS OpenXcom Windows Installer

;--------------------------------
;Special

	;Enable support for Unicode
	Unicode true

	;Compress the hell out of it
	SetCompressor /SOLID lzma
	
	!addplugindir /x86-ansi ".\plugins\x86-ansi"
	!addplugindir /x86-unicode ".\plugins\x86-unicode"

;--------------------------------
;Includes

	!include "MUI2.nsh"
	!include "nsDialogs.nsh"
	!include "x64.nsh"
	!include "Sections.nsh"
	!include "LangFile.nsh"
	!include "StrFunc.nsh"

;--------------------------------
;Defines

	!define GAME_NAME "OpenXcom Extended"
	!define GAME_VERSION "8.3.0"
	!define GAME_AUTHOR "OpenXcom Developers"
	!include "version.nsh"

;--------------------------------
;General

	;Name and file
	Name "${GAME_NAME} ${GAME_VERSION}"
	OutFile "openxcom_extended_v${GAME_VERSION}-win64.exe"

	;Default installation folder
	InstallDir "$PROGRAMFILES\${GAME_NAME}"

	;Get installation folder from registry if available
	InstallDirRegKey HKLM "Software\${GAME_NAME}" ""

	;Request application privileges for Windows Vista
	RequestExecutionLevel admin

;--------------------------------
;Variables

	Var XDialog
	Var XLabelHeader
	Var XLabelDirUFO
	Var XDirUFO
	Var XBrowseUFO
	Var XLabelDirTFTD
	Var XDirTFTD
	Var XBrowseTFTD

	Var StartMenuFolder
	Var UFO_DIR
	Var TFTD_DIR
	Var PortableMode

;--------------------------------
;Interface Settings

	!define MUI_HEADERIMAGE
	!define MUI_HEADERIMAGE_BITMAP logo.bmp
	!define MUI_WELCOMEFINISHPAGE_BITMAP side.bmp
	
	;Show all languages, despite user's codepage
	!define MUI_LANGDLL_ALLLANGUAGES
	!define MUI_LANGDLL_ALWAYSSHOW

;--------------------------------
;Language Selection Dialog Settings

	;Remember the installer language
	!define MUI_LANGDLL_REGISTRY_ROOT "HKLM"
	!define MUI_LANGDLL_REGISTRY_KEY "Software\${GAME_NAME}"
	!define MUI_LANGDLL_REGISTRY_VALUENAME "Installer Language"

;--------------------------------
;Pages

	!insertmacro MUI_PAGE_WELCOME
	!insertmacro MUI_PAGE_COMPONENTS
	!define MUI_PAGE_CUSTOMFUNCTION_PRE PreDirectory
	!define MUI_PAGE_CUSTOMFUNCTION_LEAVE PostDirectory
	!insertmacro MUI_PAGE_DIRECTORY

	Page custom XcomFolder ValidateXcom

	;Start Menu Folder Page Configuration
	!define MUI_STARTMENUPAGE_REGISTRY_ROOT "HKLM"
	!define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\${GAME_NAME}"
	!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"

	!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder

	!insertmacro MUI_PAGE_INSTFILES

	;Finish Page Configuration
	!define MUI_FINISHPAGE_RUN
	!define MUI_FINISHPAGE_RUN_FUNCTION RunAsUser
	!define MUI_FINISHPAGE_NOREBOOTSUPPORT

	!insertmacro MUI_PAGE_FINISH

	!insertmacro MUI_UNPAGE_COMPONENTS
	!insertmacro MUI_UNPAGE_CONFIRM
	!insertmacro MUI_UNPAGE_INSTFILES
	
Function RunAsUser
	ShellExecAsUser::ShellExecAsUser "open" "$INSTDIR\OpenXcomEx.exe"
FunctionEnd

Function XcomFolder

	!insertmacro MUI_HEADER_TEXT $(SETUP_XCOM_FOLDER_TITLE) $(SETUP_XCOM_FOLDER_SUBTITLE)

	nsDialogs::Create 1018
	Pop $XDialog

	${If} $XDialog == error
		Abort
	${EndIf}

	${NSD_CreateLabel} 0 0 100% 50% $(SETUP_XCOM_FOLDER)
	Pop $XLabelHeader

	${NSD_CreateLabel} 0 -67u 100% 10u $(SETUP_UFO_FOLDER)
	Pop $XLabelDirUFO

	${NSD_CreateDirRequest} 0 -56u 95% 12u $UFO_DIR
	Pop $XDirUFO
	${NSD_OnChange} $XDirUFO XcomFolderOnChange

	${NSD_CreateBrowseButton} -14u -56u 14u 12u "..."
	Pop $XBrowseUFO
	${NSD_OnClick} $XBrowseUFO XcomFolderOnBrowse

	${NSD_CreateLabel} 0 -31u 95% 10u $(SETUP_TFTD_FOLDER)
	Pop $XLabelDirTFTD

	${NSD_CreateDirRequest} 0 -20u 95% 12u $TFTD_DIR
	Pop $XDirTFTD
	${NSD_OnChange} $XDirTFTD XcomFolderOnChange

	${NSD_CreateBrowseButton} -14u -20u 14u 12u "..."
	Pop $XBrowseTFTD
	${NSD_OnClick} $XBrowseTFTD XcomFolderOnBrowse

	nsDialogs::Show

FunctionEnd

Function XcomFolderOnChange

	Pop $0

	${If} $0 == $XDirUFO
		${NSD_GetText} $0 $UFO_DIR
	${EndIf}

	${If} $0 == $XDirTFTD
		${NSD_GetText} $0 $TFTD_DIR
	${EndIf}

FunctionEnd

Function XcomFolderOnBrowse

	Pop $0

	${If} $0 == $XBrowseUFO
		nsDialogs::SelectFolderDialog $(SETUP_UFO_FOLDER) $UFO_DIR
		Pop $1
		${If} $1 == error
			Return
		${EndIf}
		StrCpy $UFO_DIR $1
		${NSD_SetText} $XDirUFO $UFO_DIR
	${EndIf}

	${If} $0 == $XBrowseTFTD
		nsDialogs::SelectFolderDialog $(SETUP_TFTD_FOLDER) $TFTD_DIR
		Pop $1
		${If} $1 == error
			Return
		${EndIf}
		StrCpy $TFTD_DIR $1
		${NSD_SetText} $XDirTFTD $TFTD_DIR
	${EndIf}

FunctionEnd

;--------------------------------
;Languages

	!include "language.nsh"

;--------------------------------
;Reserve Files

	;If you are using solid compression, files that are required before
	;the actual installation should be stored first in the data block,
	;because this will make your installer start faster.

	!insertmacro MUI_RESERVEFILE_LANGDLL

;--------------------------------
;Installer Sections

Section "$(SETUP_GAME)" SecMain

	SectionIn RO

	SetOutPath "$INSTDIR"

	File "OpenXcomEx.exe"
	;File "..\..\LICENSE.txt"
	;File "..\..\CHANGELOG.txt"
	;File /oname=README.txt "..\..\README.md"

	;Copy UFO files
	SetOutPath "$INSTDIR\UFO"

	StrCmp $UFO_DIR "" install_ufo_no 0
	IfFileExists "$UFO_DIR\*.*" 0 install_ufo_no

	CreateDirectory "$INSTDIR\UFO\GEODATA"
	CopyFiles /SILENT "$UFO_DIR\GEODATA\*.*" "$INSTDIR\UFO\GEODATA"
	CreateDirectory "$INSTDIR\UFO\GEOGRAPH"
	CopyFiles /SILENT "$UFO_DIR\GEOGRAPH\*.*" "$INSTDIR\UFO\GEOGRAPH"
	CreateDirectory "$INSTDIR\UFO\MAPS"
	CopyFiles /SILENT "$UFO_DIR\MAPS\*.*" "$INSTDIR\UFO\MAPS"
	CreateDirectory "$INSTDIR\UFO\ROUTES"
	CopyFiles /SILENT "$UFO_DIR\ROUTES\*.*" "$INSTDIR\UFO\ROUTES"
	CreateDirectory "$INSTDIR\UFO\SOUND"
	CopyFiles /SILENT "$UFO_DIR\SOUND\*.*" "$INSTDIR\UFO\SOUND"
	CreateDirectory "$INSTDIR\UFO\TERRAIN"
	CopyFiles /SILENT "$UFO_DIR\TERRAIN\*.*" "$INSTDIR\UFO\TERRAIN"
	CreateDirectory "$INSTDIR\UFO\UFOGRAPH"
	CopyFiles /SILENT "$UFO_DIR\UFOGRAPH\*.*" "$INSTDIR\UFO\UFOGRAPH"
	CreateDirectory "$INSTDIR\UFO\UFOINTRO"
	CopyFiles /SILENT "$UFO_DIR\UFOINTRO\*.*" "$INSTDIR\UFO\UFOINTRO"
	CreateDirectory "$INSTDIR\UFO\UNITS"
	CopyFiles /SILENT "$UFO_DIR\UNITS\*.*" "$INSTDIR\UFO\UNITS"

	install_ufo_no:
	File "..\..\bin\UFO\README.txt"

	;Copy TFTD files
	SetOutPath "$INSTDIR\TFTD"

	StrCmp $TFTD_DIR "" install_tftd_no 0
	IfFileExists "$TFTD_DIR\*.*" 0 install_tftd_no

	CreateDirectory "$INSTDIR\TFTD\ANIMS"
	CopyFiles /SILENT "$TFTD_DIR\ANIMS\*.*" "$INSTDIR\TFTD\ANIMS"
	CreateDirectory "$INSTDIR\TFTD\FLOP_INT"
	CopyFiles /SILENT "$TFTD_DIR\FLOP_INT\*.*" "$INSTDIR\TFTD\FLOP_INT"
	CreateDirectory "$INSTDIR\TFTD\GEODATA"
	CopyFiles /SILENT "$TFTD_DIR\GEODATA\*.*" "$INSTDIR\TFTD\GEODATA"
	CreateDirectory "$INSTDIR\TFTD\GEOGRAPH"
	CopyFiles /SILENT "$TFTD_DIR\GEOGRAPH\*.*" "$INSTDIR\TFTD\GEOGRAPH"
	CreateDirectory "$INSTDIR\TFTD\MAPS"
	CopyFiles /SILENT "$TFTD_DIR\MAPS\*.*" "$INSTDIR\TFTD\MAPS"
	CreateDirectory "$INSTDIR\TFTD\ROUTES"
	CopyFiles /SILENT "$TFTD_DIR\ROUTES\*.*" "$INSTDIR\TFTD\ROUTES"
	CreateDirectory "$INSTDIR\TFTD\SOUND"
	CopyFiles /SILENT "$TFTD_DIR\SOUND\*.*" "$INSTDIR\TFTD\SOUND"
	CreateDirectory "$INSTDIR\TFTD\TERRAIN"
	CopyFiles /SILENT "$TFTD_DIR\TERRAIN\*.*" "$INSTDIR\TFTD\TERRAIN"
	CreateDirectory "$INSTDIR\TFTD\UFOGRAPH"
	CopyFiles /SILENT "$TFTD_DIR\UFOGRAPH\*.*" "$INSTDIR\TFTD\UFOGRAPH"
	CreateDirectory "$INSTDIR\TFTD\UNITS"
	CopyFiles /SILENT "$TFTD_DIR\UNITS\*.*" "$INSTDIR\TFTD\UNITS"

	install_tftd_no:
	File "..\..\bin\TFTD\README.txt"

	SetOutPath "$INSTDIR"

	File /r "..\..\bin\common"
	File /r "..\..\bin\standard"

${If} $PortableMode == ${SF_SELECTED}
	CreateDirectory "$INSTDIR\user"
${EndIf}

	;Store installation folder
	WriteRegStr HKLM "Software\${GAME_NAME}" "" $INSTDIR

	;Write the uninstall keys for Windows
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${GAME_NAME}" "DisplayName" "${GAME_NAME} ${GAME_VERSION}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${GAME_NAME}" "DisplayIcon" '"$INSTDIR\OpenXcomEx.exe",0'
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${GAME_NAME}" "DisplayVersion" "${GAME_VERSION}.0"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${GAME_NAME}" "InstallLocation" "$INSTDIR"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${GAME_NAME}" "Publisher" "${GAME_AUTHOR}"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${GAME_NAME}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${GAME_NAME}" "URLInfoAbout" "https://openxcom.org"
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${GAME_NAME}" "NoModify" 1
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${GAME_NAME}" "NoRepair" 1

	;Create uninstaller
	WriteUninstaller "$INSTDIR\Uninstall.exe"

	;Create shortcuts
	SetOutPath "$INSTDIR"

	!insertmacro MUI_STARTMENU_WRITE_BEGIN Application

		CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
		CreateShortCut "$SMPROGRAMS\$StartMenuFolder\${GAME_NAME}.lnk" "$INSTDIR\OpenXcomEx.exe"
		;CreateShortCut "$SMPROGRAMS\$StartMenuFolder\$(SETUP_SHORTCUT_CHANGELOG).lnk" "$INSTDIR\CHANGELOG.txt"
		;CreateShortCut "$SMPROGRAMS\$StartMenuFolder\$(SETUP_SHORTCUT_README).lnk" "$INSTDIR\README.txt"
${If} $PortableMode == ${SF_SELECTED}
		CreateShortCut "$SMPROGRAMS\$StartMenuFolder\$(SETUP_SHORTCUT_USER).lnk" "$INSTDIR\user"
${Else}
		CreateShortCut "$SMPROGRAMS\$StartMenuFolder\$(SETUP_SHORTCUT_USER).lnk" "$DOCUMENTS\OpenXcom"
${EndIf}
		CreateShortCut "$SMPROGRAMS\$StartMenuFolder\$(SETUP_SHORTCUT_UNINSTALL).lnk" "$INSTDIR\Uninstall.exe"

	!insertmacro MUI_STARTMENU_WRITE_END

SectionEnd

Section /o "$(SETUP_PORTABLE)" SecPortable

	CreateDirectory "$INSTDIR\user"

SectionEnd

Section /o "$(SETUP_DESKTOP)" SecDesktop

	SetOutPath "$INSTDIR"

	CreateShortCut "$DESKTOP\${GAME_NAME}.lnk" "$INSTDIR\OpenXcomEx.exe"

SectionEnd

;--------------------------------
;Descriptions

	;Assign language strings to sections
	!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
		!insertmacro MUI_DESCRIPTION_TEXT ${SecMain} $(SETUP_GAME_DESC)
		!insertmacro MUI_DESCRIPTION_TEXT ${SecPortable} $(SETUP_PORTABLE_DESC)
		!insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(SETUP_DESKTOP_DESC)
	!insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------
;Installer functions

Function .onInit

	StrCpy $StartMenuFolder "${GAME_NAME}"
	StrCpy $PortableMode 0

	; Check for existing X-COM installs
	StrCpy $UFO_DIR ""
	StrCpy $TFTD_DIR ""

	Call ScanSteam
	Call ScanGOG

	!insertmacro MUI_LANGDLL_DISPLAY

FunctionEnd

Function PreDirectory

	StrCpy $1 "${SecPortable}"
    SectionGetFlags $1 $0
    IntOp $PortableMode $0 & ${SF_SELECTED}
	
!ifdef NSIS_WIN32_MAKENSIS
${If} ${RunningX64}
	StrCpy $INSTDIR "$PROGRAMFILES64\${GAME_NAME}"
${Else}
	StrCpy $INSTDIR "$PROGRAMFILES32\${GAME_NAME}"
${EndIf}
!endif
${If} $PortableMode == ${SF_SELECTED}
	StrCpy $INSTDIR "C:\${GAME_NAME}"
${EndIf}

FunctionEnd

${StrStr}
Function PostDirectory

${If} $PortableMode == ${SF_SELECTED}
	;Don't allow Portable Mode in Program Files
	${StrStr} $0 $INSTDIR $PROGRAMFILES32
	StrCmp $0 "" 0 portable_no
	${StrStr} $0 $INSTDIR $PROGRAMFILES64
	StrCmp $0 "" portable_yes portable_no

	portable_no:
	MessageBox MB_ICONSTOP|MB_OK $(SETUP_ERROR_PORTABLE)
	Abort

	portable_yes:
${EndIf}

FunctionEnd

;--------------------------------
;Scan Steam for X-COM installations
${StrLoc}
${StrTok}
${StrRep}

Function ScanSteam

	ReadRegStr $R1 HKLM "Software\Valve\Steam" "InstallPath"
	IfErrors steam_done
	Call ScanSteamLibrary

	ClearErrors
	FileOpen $0 "$R1\config\libraryfolders.vdf" r
	IfErrors steam_read_done
	steam_read_loop:
	FileRead $0 $1
	IfErrors steam_read_done
	${StrLoc} $2 $1 "path" ">"
	StrCmp $2 "" steam_read_next 0
	${StrTok} $R1 $1 '"' "3" "1"
	${StrRep} $R1 $R1 "\\" "\"
	Call ScanSteamLibrary
	steam_read_next:
	Goto steam_read_loop
	steam_read_done:
	FileClose $0

	steam_done:

FunctionEnd

;--------------------------------
;Scan a specific Steam Library

Function ScanSteamLibrary

	StrCpy $R0 "$R1\steamapps\common\XCom UFO Defense\XCOM"
	IfFileExists "$R0\*.*" steam_ufo_yes steam_ufo_no
	steam_ufo_yes:
	StrCpy $UFO_DIR $R0
	steam_ufo_no:

	StrCpy $R0 "$R1\steamapps\common\X-COM Terror from the Deep\TFD"
	IfFileExists "$R0\*.*" steam_tftd_yes steam_tftd_no
	steam_tftd_yes:
	StrCpy $TFTD_DIR $R0
	steam_tftd_no:

FunctionEnd

;--------------------------------
;Scan GOG for X-COM installations

Function ScanGOG

	ReadRegStr $R0 HKLM "Software\GOG.com\Games\1445250340" "PATH"
	IfErrors gog_ufo_no
	IfFileExists "$R0\*.*" gog_ufo_yes gog_ufo_no
	gog_ufo_yes:
	StrCpy $UFO_DIR $R0
	gog_ufo_no:

	ReadRegStr $R0 HKLM "Software\GOG.com\Games\1445249983" "PATH"
	IfErrors gog_tftd_no
	IfFileExists "$R0\*.*" gog_tftd_yes gog_tftd_no
	gog_tftd_yes:
	StrCpy $TFTD_DIR $R0
	gog_tftd_no:

FunctionEnd

;--------------------------------
;Validate X-COM folders

Function ValidateXcom

	; UFO
	StrCmp $UFO_DIR "" validate_ufo_yes
	IfFileExists "$UFO_DIR\GEODATA\*.*" 0 confirm_ufo
	IfFileExists "$UFO_DIR\GEOGRAPH\*.*" 0 confirm_ufo
	IfFileExists "$UFO_DIR\MAPS\*.*" 0 confirm_ufo
	IfFileExists "$UFO_DIR\ROUTES\*.*" 0 confirm_ufo
	IfFileExists "$UFO_DIR\SOUND\*.*" 0 confirm_ufo
	IfFileExists "$UFO_DIR\TERRAIN\*.*" 0 confirm_ufo
	IfFileExists "$UFO_DIR\UFOGRAPH\*.*" 0 confirm_ufo
	IfFileExists "$UFO_DIR\UNITS\*.*" 0 confirm_ufo
	IfFileExists "$UFO_DIR\GEOGRAPH\UP001.SPK" 0 confirm_ufo ; unique file
	IfFileExists "$UFO_DIR\XcuSetup.bat" confirm_ufo_xcu
	Goto validate_ufo_yes
	confirm_ufo:
	MessageBox MB_ICONEXCLAMATION|MB_YESNO $(SETUP_WARNING_UFO) /SD IDYES IDYES validate_ufo_yes IDNO validate_ufo_no
	confirm_ufo_xcu:
	MessageBox MB_ICONEXCLAMATION|MB_YESNO $(SETUP_WARNING_XCU) /SD IDYES IDYES validate_ufo_yes IDNO validate_ufo_no
	validate_ufo_no:
	Abort
	validate_ufo_yes:

	; TFTD
	StrCmp $TFTD_DIR "" validate_tftd_yes
	IfFileExists "$TFTD_DIR\GEODATA\*.*" 0 confirm_tftd
	IfFileExists "$TFTD_DIR\GEOGRAPH\*.*" 0 confirm_tftd
	IfFileExists "$TFTD_DIR\MAPS\*.*" 0 confirm_tftd
	IfFileExists "$TFTD_DIR\ROUTES\*.*" 0 confirm_tftd
	IfFileExists "$TFTD_DIR\SOUND\*.*" 0 confirm_tftd
	IfFileExists "$TFTD_DIR\TERRAIN\*.*" 0 confirm_tftd
	IfFileExists "$TFTD_DIR\UFOGRAPH\*.*" 0 confirm_tftd
	IfFileExists "$TFTD_DIR\UNITS\*.*" 0 confirm_tftd
	IfFileExists "$TFTD_DIR\GEOGRAPH\UP001.BDY" 0 confirm_tftd ; unique file
	IfFileExists "$TFTD_DIR\XcuSetup.bat" confirm_tftd_xcu
	Goto validate_tftd_yes
	confirm_tftd:
	MessageBox MB_ICONEXCLAMATION|MB_YESNO $(SETUP_WARNING_TFTD) /SD IDYES IDYES validate_tftd_yes IDNO validate_tftd_no
	confirm_tftd_xcu:
	MessageBox MB_ICONEXCLAMATION|MB_YESNO $(SETUP_WARNING_XCU) /SD IDYES IDYES validate_tftd_yes IDNO validate_tftd_no
	validate_tftd_no:
	Abort
	validate_tftd_yes:

FunctionEnd

;--------------------------------
;Uninstaller Functions

Function un.onInit

	!insertmacro MUI_UNGETLANGUAGE

FunctionEnd

;--------------------------------
;Uninstaller Sections

Section "un.$(SETUP_UNDATA)" UnData
	RMDir /r "$INSTDIR\TFTD"
	RMDir /r "$INSTDIR\UFO"
SectionEnd

Section /o "un.$(SETUP_UNUSER)" UnUser
	RMDir /r "$INSTDIR\user"
	RMDir /r "$DOCUMENTS\OpenXcom"
SectionEnd

Section "-un.Main"

	SetOutPath "$TEMP"

	Delete "$INSTDIR\OpenXcomEx.exe"
	Delete "$INSTDIR\*.dll"
	Delete "$INSTDIR\*.txt"
	Delete "$INSTDIR\*.md"

	RMDir /r "$INSTDIR\common"
	RMDir /r "$INSTDIR\standard"

	Delete "$INSTDIR\Uninstall.exe"
	RMDir "$INSTDIR"

	!insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder

	Delete "$SMPROGRAMS\$StartMenuFolder\*.*"
	RMDir "$SMPROGRAMS\$StartMenuFolder"

	Delete "$DESKTOP\${GAME_NAME}.lnk"

	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${GAME_NAME}"
	DeleteRegKey /ifempty HKLM "Software\${GAME_NAME}"

SectionEnd

;--------------------------------
;Uninstaller Descriptions

	;Assign language strings to sections
	!insertmacro MUI_UNFUNCTION_DESCRIPTION_BEGIN
		!insertmacro MUI_DESCRIPTION_TEXT ${UnData} $(SETUP_UNDATA_DESC)
		!insertmacro MUI_DESCRIPTION_TEXT ${UnUser} $(SETUP_UNUSER_DESC)
	!insertmacro MUI_UNFUNCTION_DESCRIPTION_END

;--------------------------------
;Version Information

	VIProductVersion "${GAME_VERSION}.0"
	VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductName" "${GAME_NAME} Installer"
	VIAddVersionKey /LANG=${LANG_ENGLISH} "ProductVersion" "${GAME_VERSION}.0"
	VIAddVersionKey /LANG=${LANG_ENGLISH} "CompanyName" "${GAME_AUTHOR}"
	VIAddVersionKey /LANG=${LANG_ENGLISH} "LegalCopyright" "Copyright 2010-2025 ${GAME_AUTHOR}"
	VIAddVersionKey /LANG=${LANG_ENGLISH} "FileDescription" "${GAME_NAME} Installer"
	VIAddVersionKey /LANG=${LANG_ENGLISH} "FileVersion" "${GAME_VERSION}.0"
