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

} // namespace cilogg::update_protocol
