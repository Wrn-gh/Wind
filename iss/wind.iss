; Wind 安装包脚本（Inno Setup 6.0 及以上）。
;
; 本地打包（先构建 Release）：
;   "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" iss\wind.iss /DAppVersion=1.0.0
;
; CI 还会额外传入 SourceDir / OutputDir / AppURL，见 .github/workflows/release.yml。
; 下面每个 #define 都能在命令行用 /D名字=值 覆盖。

#define AppName "Wind"
#define AppExeName "Wind.exe"
#define AppPublisher "Wrn-gh"

; SourcePath 是本脚本所在目录（iss\）。各版本对结尾反斜杠的说法不一，统一用
; AddBackslash 补上，下面的相对路径才不会拼成 "iss..\build"。
#define IssDir  AddBackslash(SourcePath)
#define RepoDir IssDir + ".."

#ifndef SourceDir
  #define SourceDir RepoDir + "\build\Release"
#endif
#ifndef OutputDir
  #define OutputDir RepoDir + "\dist"
#endif

; 版本号由 CI 从 git tag 传进来；本地忘了传就用 0.0.0，一眼看出是调试产物。
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

; VersionInfoVersion 只收数字，1.0.0-rc1 要截成 1.0.0 才能用。
; 末尾补一个 "-" 保证 Pos 一定有结果。
#define AppVersionShort Copy(AppVersion, 1, Pos("-", AppVersion + "-") - 1)

; 仓库地址，用于安装界面的发布者链接；CI 会用当前仓库覆盖它。
#ifndef AppURL
  #define AppURL "https://github.com/Wrn-gh/Wind"
#endif

[Setup]
; AppId 是升级识别的唯一凭据，发布之后不要再改：换了它，新版本不会覆盖旧版本，
; 而是在系统里多出一份并存的安装。这个 GUID 沿用自第一版用安装向导生成的脚本。
AppId={{A125F6E5-FB65-471A-9C05-CF972E018D0E}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
#if Len(AppURL) > 0
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
#endif
VersionInfoVersion={#AppVersionShort}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} Setup
; 免管理员：装进用户自己的 LOCALAPPDATA，卸载不留 HKLM 痕迹，和绿色软件定位一致。
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
; 只有 Win10+ 才有 CreateSwapChainForComposition / SetProcessDpiAwarenessContext。
MinVersion=10.0
; x64compatible 是 6.3 才加的写法（也能跑在 Win11 ARM64 上），老版本只认 x64。
#if Ver >= EncodeVer(6,3,0)
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#else
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
#endif
OutputDir={#OutputDir}
OutputBaseFilename={#AppName}-{#AppVersion}-setup
SetupIconFile={#IssDir}..\res\wind.ico
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName}
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
SetupLogging=yes
; 程序是托盘常驻的，退出时机由下面 [Code] 里的 StopRunningWind 掌握。
CloseApplications=no

[Languages]
; 官方发行版不带简体中文：装了第三方语言文件就用中文，否则退回英文，
; 保证在任何一台机器、任何官方安装包上都能编出安装程序。
#if FileExists(AddBackslash(CompilerPath) + "Languages\ChineseSimplified.isl")
Name: "cn"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
#else
Name: "en"; MessagesFile: "compiler:Default.isl"
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#IssDir}..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#IssDir}..\README.zh-CN.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[Code]
{ Wind.exe 是否在跑。用 tasklist 判断而不是直接发 --quit：
  老版本还不认识这个参数，对着它发 --quit 会真的启动一个窗口。 }
function IsWindRunning: Boolean;
var
  ResultCode: Integer;
  TempFile: String;
  Output: AnsiString;
begin
  Result := False;
  TempFile := ExpandConstant('{tmp}\wind-procs.txt');
  if Exec(ExpandConstant('{cmd}'),
         '/C tasklist /FI "IMAGENAME eq {#AppExeName}" /NH > "' + TempFile + '"',
         '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    if LoadStringFromFile(TempFile, Output) then
     Result := Pos(Lowercase('{#AppExeName}'), Lowercase(String(Output))) > 0;
    DeleteFile(TempFile);
  end;
end;

{ 让正在运行的 Wind 走正常退出流程：它会先还原自己改过的窗口状态。 }
procedure StopRunningWind;
var
  ResultCode: Integer;
  InstalledExe: String;
begin
  if not IsWindRunning then
    Exit;

  InstalledExe := ExpandConstant('{app}\{#AppExeName}');
  if FileExists(InstalledExe) then
  begin
    { 不等它退出：升级上来的老版本会忽略 --quit 继续运行，等下去会把安装卡死。 }
    Exec(InstalledExe, '--quit', '', SW_HIDE, ewNoWait, ResultCode);
    Sleep(2000);
  end;

  { 兜底：还没退的（老版本、放在别处的绿色版）强杀，
    总比因为文件被占用而装不上好。没有进程时 taskkill 只是返回非零。 }
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM {#AppExeName} /F', '',
      SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopRunningWind;
  Result := '';
end;

function InitializeUninstall(): Boolean;
begin
  StopRunningWind;
  Result := True;
end;
