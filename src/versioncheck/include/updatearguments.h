#pragma once

namespace cilogg::update_protocol {

inline constexpr char WaitPidOption[] = "--wait-pid";
inline constexpr char ModeOption[] = "--mode";
inline constexpr char CurrentOption[] = "--current";
inline constexpr char StagedOption[] = "--staged";
inline constexpr char BackupOption[] = "--backup";
inline constexpr char RelaunchOption[] = "--relaunch";
inline constexpr char HelperAcknowledgementOption[] = "--ack";
inline constexpr char HelperTokenOption[] = "--token";
inline constexpr char LogOption[] = "--log";
inline constexpr char StartupAcknowledgementOption[] = "--cilogg-update-ack";
inline constexpr char StartupTokenOption[] = "--cilogg-update-token";

#ifdef _WIN32
inline constexpr wchar_t WaitPidOptionWide[] = L"--wait-pid";
inline constexpr wchar_t ModeOptionWide[] = L"--mode";
inline constexpr wchar_t CurrentOptionWide[] = L"--current";
inline constexpr wchar_t StagedOptionWide[] = L"--staged";
inline constexpr wchar_t BackupOptionWide[] = L"--backup";
inline constexpr wchar_t RelaunchOptionWide[] = L"--relaunch";
inline constexpr wchar_t HelperAcknowledgementOptionWide[] = L"--ack";
inline constexpr wchar_t HelperTokenOptionWide[] = L"--token";
inline constexpr wchar_t LogOptionWide[] = L"--log";
inline constexpr wchar_t StartupAcknowledgementOptionWide[] = L"--cilogg-update-ack";
inline constexpr wchar_t StartupTokenOptionWide[] = L"--cilogg-update-token";
#endif

// Written by the helper next to its log; the next CILogg start reads it to explain an
// exit-time install that did not complete.
inline constexpr char InstallResultFileName[] = "install-result.txt";
// install-result.txt "phase": the installer could not be started (code is a system error or a
// helper code), it ran and returned `code`, or a self-contained replacement ran.
inline constexpr char LaunchPhase[] = "launch";
inline constexpr char InstallerPhase[] = "installer";
inline constexpr char ReplacePhase[] = "replace";

// Helper exit codes for its own failures. System error codes pass through unchanged.
inline constexpr int StagedPackageMissingExitCode = 9;

// Windows system error codes the helper treats specially (kept numeric so this header
// stays free of <windows.h>).
inline constexpr unsigned long FileNotFoundError = 2;
inline constexpr unsigned long PathNotFoundError = 3;
inline constexpr unsigned long AccessDeniedError = 5;
inline constexpr unsigned long SharingViolationError = 32;
inline constexpr unsigned long LockViolationError = 33;
inline constexpr unsigned long CancelledError = 1223;

// A freshly downloaded installer can be briefly unavailable while antivirus software
// inspects it. These launch failures are retried; a declined UAC prompt
// (CancelledError) is final.
constexpr bool isTransientLaunchError( unsigned long code )
{
    return code == FileNotFoundError || code == PathNotFoundError || code == AccessDeniedError
           || code == SharingViolationError || code == LockViolationError;
}

inline constexpr int InstallerLaunchAttempts = 5;

// Delay before retry number `attempt` (1-based): 1 s, 2 s, 4 s, 8 s.
constexpr int installerLaunchRetryDelayMs( int attempt )
{
    return attempt <= 1 ? 1000 : 1000 << ( attempt - 1 < 3 ? attempt - 1 : 3 );
}

} // namespace cilogg::update_protocol
