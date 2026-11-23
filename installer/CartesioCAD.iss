; Installatore di CartesioCAD per Windows (Inno Setup 7).
;
; Si compila con scripts/build-installer.sh, che passa:
;   /DVersione=0.5.0              versione (da CMakeLists.txt)
;   /DCartella=<dist\CartesioCAD>  cartella preparata con "cmake --install"
;   /O<cartella di uscita>
;
; Installazione in Programmi con i diritti di amministratore. Gli aggiornamenti dal programma continuano ad arrivare
; come archivio firmato: CartesioCAD chiede i diritti di amministratore (UAC) solo per installarli.
; Nessuna associazione di file. L'installatore non è firmato digitalmente: Windows può mostrare un avviso.

#ifndef Versione
  #error Manca /DVersione
#endif
#ifndef Cartella
  #error Manca /DCartella
#endif

[Setup]
AppId={{DAA98C77-2A4E-48CD-8668-E751C4A21483}
AppName=CartesioCAD
AppVersion={#Versione}
AppVerName=CartesioCAD {#Versione}
AppPublisher=SamuDiPi
AppPublisherURL=https://github.com/SamuDiPi/CartesioCAD
AppSupportURL=https://github.com/SamuDiPi/CartesioCAD/issues
AppUpdatesURL=https://github.com/SamuDiPi/CartesioCAD/releases
AppCopyright=Copyright (C) 2026 SamuDiPi - GNU GPL v3
VersionInfoVersion={#Versione}
VersionInfoDescription=Installazione di CartesioCAD
DefaultDirName={autopf}\CartesioCAD
; Scegliendo un'altra cartella si aggiunge sempre "CartesioCAD": la disinstallazione toglie solo quella.
AppendDefaultDirName=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
; Solo per le prove automatiche: /CURRENTUSER installa per l'utente, senza diritti di amministratore.
PrivilegesRequiredOverridesAllowed=commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile={#Cartella}\LICENSE.txt
InfoBeforeFile=prima-di-installare.txt
OutputBaseFilename=CartesioCAD-{#Versione}-installazione
UninstallDisplayIcon={app}\CartesioCAD.exe
UninstallDisplayName=CartesioCAD {#Versione}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
ChangesAssociations=no
ChangesEnvironment=no
; Il collegamento facoltativo sul desktop è per chi installa.
UsedUserAreasWarning=no

[Languages]
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"

[Tasks]
Name: "desktop"; Description: "Collegamento sul desktop"; Flags: unchecked

[Files]
Source: "{#Cartella}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\CartesioCAD"; Filename: "{app}\CartesioCAD.exe"; WorkingDir: "{userdocs}"
Name: "{userdesktop}\CartesioCAD"; Filename: "{app}\CartesioCAD.exe"; WorkingDir: "{userdocs}"; Tasks: desktop

[Run]
Filename: "{app}\CartesioCAD.exe"; Description: "Avvia CartesioCAD"; Flags: nowait postinstall skipifsilent runasoriginaluser

[UninstallRun]
; Toglie il connettore dell'Assistente IA da Claude Desktop, se puntava a questa installazione.
Filename: "{app}\CartesioCAD.exe"; Parameters: "--rimuovi-connettore"; Flags: runhidden waituntilterminated; RunOnceId: "RimuoviConnettore"

[Code]
// Cartella del programma "nostra": termina con \CartesioCAD (vedi AppendDefaultDirName) e contiene CartesioCAD.exe.
function CartellaNostra(const Dir: String): Boolean;
begin
  Result := (CompareText(ExtractFileName(RemoveBackslashUnlessRoot(Dir)), 'CartesioCAD') = 0)
            and (Length(Dir) > 15);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  App: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    // Restano i file arrivati con gli aggiornamenti dal programma (e "versione-precedente"): via tutta la cartella,
    // ma solo se è quella di CartesioCAD.
    App := ExpandConstant('{app}');
    if DirExists(App) and CartellaNostra(App) then
      DelTree(App, True, True, True);
    if not UninstallSilent then
      if MsgBox('Eliminare anche le impostazioni di CartesioCAD (opzioni, documenti recenti, miniature, ' +
                'salvataggi automatici non recuperati)?' + #13#10#13#10 +
                'I disegni salvati non vengono toccati.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      begin
        RegDeleteKeyIncludingSubkeys(HKCU, 'Software\CartesioCAD');
        DelTree(ExpandConstant('{localappdata}\CartesioCAD'), True, True, True);
        DelTree(ExpandConstant('{userappdata}\CartesioCAD'), True, True, True);
      end;
  end;
end;
