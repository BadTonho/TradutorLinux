#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

#if defined(__GNUC__) || defined(__clang__)
#define TL_MSABI __attribute__((ms_abi))
#else
#error "TL_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

struct GuestExecutionResult {
    bool exited_explicitly{};
    std::uint32_t exit_code{};
};
namespace abi {

// Tipos mínimos do Win32 usados pelas APIs suportadas.
using Handle = void*;
using HWnd = void*;
using Bool = int;
using Dword = std::uint32_t;
using Uint = std::uint32_t;
using Wparam = std::uintptr_t;
using Lparam = std::intptr_t;
using Lresult = std::intptr_t;
using Atom = std::uint16_t;

constexpr Dword kErrorSuccess = 0;
constexpr Dword kErrorFileNotFound = 2;
constexpr Dword kErrorNoMoreFiles = 18;
constexpr Dword kErrorNotLocked = 158;
constexpr Dword kErrorNotSupported = 50;
constexpr Dword kErrorAccessDenied = 5;
constexpr Dword kErrorInvalidHandle = 6;
constexpr Dword kErrorNotEnoughMemory = 8;
constexpr Dword kErrorAlreadyExists = 183;
constexpr Dword kErrorInvalidParameter = 87;
constexpr Dword kErrorEnvvarNotFound = 203;
constexpr Dword kErrorBrokenPipe = 109;
constexpr Dword kErrorBadLength = 24;
constexpr Dword kErrorResourceDataNotFound = 1812;
constexpr Dword kErrorResourceTypeNotFound = 1813;
constexpr Dword kErrorResourceNameNotFound = 1814;
constexpr Dword kErrorResourceNotFound = kErrorResourceNameNotFound;

constexpr Dword kGenericRead = 0x80000000U;
constexpr Dword kGenericWrite = 0x40000000U;
constexpr Dword kCreateAlways = 2;
constexpr Dword kOpenExisting = 3;
constexpr Dword kCreateNew = 1;
constexpr Dword kOpenAlways = 4;
constexpr Dword kTruncateExisting = 5;
constexpr Dword kFileAttributeReadOnly = 0x00000001U;
constexpr Dword kFileAttributeDirectory = 0x00000010U;
constexpr Dword kFileAttributeArchive = 0x00000020U;
constexpr Dword kFileAttributeNormal = 0x00000080U;
constexpr Dword kFindExInfoStandard = 0;
constexpr Dword kFindExInfoBasic = 1;
constexpr Dword kFindExSearchNameMatch = 0;
constexpr Dword kFindFirstExLargeFetch = 0x00000002U;
constexpr Dword kFileBasicInfo = 0;
constexpr Dword kFileDispositionInfo = 4;
constexpr Dword kFileDispositionInfoEx = 21;
constexpr Dword kFileDispositionFlagDelete = 0x00000001U;
constexpr Dword kFileDispositionFlagPosixSemantics = 0x00000002U;
constexpr Dword kFileDispositionFlagOnClose = 0x00000008U;
constexpr Dword kFileDispositionFlagIgnoreReadonlyAttribute = 0x00000010U;
constexpr Dword kMemCommit = 0x1000U;
constexpr Dword kMemReserve = 0x2000U;
constexpr Dword kMemRelease = 0x8000U;
constexpr Dword kMemImage = 0x1000000U;
constexpr Dword kMemPrivate = 0x20000U;
constexpr Dword kGmemMoveable = 0x0002U;
constexpr Dword kGmemZeroinit = 0x0040U;
constexpr Dword kPageNoAccess = 0x01U;
constexpr Dword kPageReadOnly = 0x02U;
constexpr Dword kPageReadWrite = 0x04U;
constexpr Dword kPageWriteCopy = 0x08U;
constexpr Dword kPageExecute = 0x10U;
constexpr Dword kPageExecuteRead = 0x20U;
constexpr Dword kPageExecuteReadWrite = 0x40U;
constexpr Dword kPageExecuteWriteCopy = 0x80U;

constexpr Dword kErrorInsufficientBuffer = 122;
constexpr Dword kErrorInvalidAddress = 487;
constexpr Dword kErrorModNotFound = 126;
constexpr Dword kErrorProcNotFound = 127;
constexpr Dword kErrorTimeout = 1460;
constexpr Dword kErrorNoUnicodeTranslation = 1113;
constexpr Dword kErrorInvalidFlags = 1004;
constexpr Dword kErrorTooManyTlsIndexes = 4323;
constexpr Dword kGetModuleHandleExFlagPin = 0x01U;
constexpr Dword kGetModuleHandleExFlagUnchangedRefcount = 0x02U;
constexpr Dword kGetModuleHandleExFlagFromAddress = 0x04U;

// Version helpers (VerifyVersionInfo)
constexpr Dword kVerMinorVersion = 0x00000001U;
constexpr Dword kVerMajorVersion = 0x00000002U;
constexpr Dword kVerBuildNumber = 0x00000004U;
constexpr Dword kVerPlatformId = 0x00000008U;
constexpr Dword kVerServicePackMinor = 0x00000010U;
constexpr Dword kVerServicePackMajor = 0x00000020U;
constexpr Dword kVerSuiteName = 0x00000040U;
constexpr Dword kVerProductType = 0x00000080U;
constexpr Dword kVerEqual = 1U;
constexpr Dword kVerGreater = 2U;
constexpr Dword kVerGreaterEqual = 3U;
constexpr Dword kVerLess = 4U;
constexpr Dword kVerLessEqual = 5U;
constexpr Dword kVerPlatformWin32Nt = 2U;
constexpr Dword kVerNtWorkstation = 1U;
constexpr Dword kFormatMessageAllocateBuffer = 0x00000100U;
constexpr Dword kFormatMessageIgnoreInserts = 0x00000200U;
constexpr Dword kFormatMessageFromSystem = 0x00001000U;
constexpr Dword kCriticalSectionNoDebugInfo = 0x01000000U;

// Fase 11: WaitForSingleObject.
constexpr Dword kWaitObject0 = 0;
constexpr Dword kWaitAbandoned0 = 0x80;
constexpr Dword kWaitTimeout = 0x102;
constexpr Dword kWaitFailed = 0xFFFFFFFF;
constexpr Dword kInfinite = 0xFFFFFFFF;
constexpr Dword kRtRsrcData = 10;

// Code pages suportadas pela conversão de strings.
constexpr Dword kCpAcp = 0;          // CP_ACP -> CP1252 (locale C do runtime)
constexpr Dword kCpOem = 1;          // CP_OEMCP -> CP437
constexpr Dword kCp437 = 437;
constexpr Dword kCp1252 = 1252;
constexpr Dword kCpUtf8 = 65001;

constexpr Dword kFlsOutOfIndexes = 0xFFFFFFFFU;

// Locale deliberadamente fixo do processo convidado.
constexpr Dword kLocaleUserDefault = 0x0400U;
constexpr Dword kLocaleSystemDefault = 0x0800U;
constexpr Dword kLocaleEnglishUnitedStates = 0x0409U;
constexpr Dword kLocaleReturnNumber = 0x20000000U;
constexpr Dword kLocaleILanguage = 0x00000001U;
constexpr Dword kLocaleSLanguage = 0x00000002U;
constexpr Dword kLocaleSEngLanguage = 0x00001001U;
constexpr Dword kLocaleSISO639LangName = 0x00000059U;
constexpr Dword kLocaleSCountry = 0x00000006U;
constexpr Dword kLocaleSEngCountry = 0x00001002U;
constexpr Dword kLocaleSISO3166CtryName = 0x0000005AU;
constexpr Dword kLocaleSDecimal = 0x0000000EU;
constexpr Dword kLocaleSThousand = 0x0000000FU;
constexpr Dword kLocaleSCurrency = 0x00000014U;
constexpr Dword kLocaleS1159 = 0x00000028U;
constexpr Dword kLocaleS2359 = 0x00000029U;
constexpr Dword kLocaleIDefaultCodePage = 0x0000000BU;
constexpr Dword kLcidInstalled = 0x00000001U;
constexpr Dword kLcidSupported = 0x00000002U;
constexpr Dword kCType1 = 0x00000001U;
constexpr std::uint16_t kC1Upper = 0x0001U;
constexpr std::uint16_t kC1Lower = 0x0002U;
constexpr std::uint16_t kC1Digit = 0x0004U;
constexpr std::uint16_t kC1Space = 0x0008U;
constexpr std::uint16_t kC1Punct = 0x0010U;
constexpr std::uint16_t kC1Cntrl = 0x0020U;
constexpr std::uint16_t kC1Blank = 0x0040U;
constexpr std::uint16_t kC1Xdigit = 0x0080U;
constexpr std::uint16_t kC1Alpha = 0x0100U;
constexpr Dword kDateShortDate = 0x00000001U;
constexpr Dword kDateLongDate = 0x00000002U;
constexpr Dword kTimeNoSeconds = 0x00000002U;
constexpr Dword kTimeNoTimeMarker = 0x00000004U;
constexpr Dword kTimeForce24HourFormat = 0x00000008U;
constexpr Dword kLcmapsLowercase = 0x00000100U;
constexpr Dword kLcmapsUppercase = 0x00000200U;

struct GuestCpInfo {
    Dword max_char_size{};
    std::uint8_t default_char[2]{};
    std::uint8_t lead_byte[12]{};
};
static_assert(sizeof(GuestCpInfo) == 20);

// Flags aceitas por MultiByteToWideChar / WideCharToMultiByte.
constexpr Dword kMbPrecomposed = 0x01U;
constexpr Dword kMbErrInvalidChars = 0x08U;
constexpr Dword kWcCompositeCheck = 0x200U;
constexpr Dword kWcNoBestFitChars = 0x400U;

constexpr Dword kStdInputHandle = 0xFFFFFFF6U;   // STD_INPUT_HANDLE (-10)
constexpr Dword kStdOutputHandle = 0xFFFFFFF5U;  // STD_OUTPUT_HANDLE (-11)
constexpr Dword kStdErrorHandle = 0xFFFFFFF4U;   // STD_ERROR_HANDLE (-12)
constexpr Dword kFileTypeUnknown = 0;
constexpr Dword kFileTypeDisk = 1;
constexpr Dword kFileTypeChar = 2;
constexpr Dword kFileTypePipe = 3;
constexpr Dword kStartfUseStdHandles = 0x00000100U;

// PROCESSOR_FEATURE_ID cobertos no hospedeiro AMD64.
constexpr Dword kPfCompareExchangeDouble = 2;
constexpr Dword kPfMmxInstructionsAvailable = 3;
constexpr Dword kPfXmmiInstructionsAvailable = 6;
constexpr Dword kPfRdtscInstructionAvailable = 8;
constexpr Dword kPfPaeEnabled = 9;
constexpr Dword kPfXmmi64InstructionsAvailable = 10;
constexpr Dword kPfNxEnabled = 12;

constexpr Uint kWmPaint = 0x000F;
constexpr Uint kWmClose = 0x0010;
constexpr Uint kWmCreate = 0x0001;
constexpr Uint kWmSize = 0x0005;
constexpr Uint kWmCommand = 0x0111;
constexpr Uint kWmSysCommand = 0x0112;
constexpr Uint kWmNotify = 0x004E;
constexpr Uint kWmSetFont = 0x0030;
constexpr Uint kWmCtlColorEdit = 0x0133;
constexpr Uint kWmCtlColorListBox = 0x0134;
constexpr Uint kWmCtlColorStatic = 0x0138;
constexpr Uint kWmTrayIcon = 0x0400 + 1;

constexpr Uint kWmDestroy = 0x0002;
constexpr Uint kWmQuit = 0x0012;
constexpr Uint kWmKeyDown = 0x0100;
constexpr Uint kWmKeyUp = 0x0101;
constexpr Uint kWmChar = 0x0102;
constexpr Uint kWmTimer = 0x0113;
constexpr Uint kWmMouseMove = 0x0200;
constexpr Uint kWmLButtonDown = 0x0201;
constexpr Uint kWmLButtonUp = 0x0202;
constexpr Wparam kMkLButton = 0x0001;  // MK_LBUTTON
constexpr int kSwShow = 1;

// Virtual keys (subconjunto suportado).
constexpr Wparam kVkBack = 0x08;
constexpr Wparam kVkTab = 0x09;
constexpr Wparam kVkReturn = 0x0D;
constexpr Wparam kVkEscape = 0x1B;
constexpr Wparam kVkSpace = 0x20;
constexpr Wparam kVkLeft = 0x25;
constexpr Wparam kVkUp = 0x26;
constexpr Wparam kVkRight = 0x27;
constexpr Wparam kVkDown = 0x28;
constexpr Wparam kVkDelete = 0x2E;

constexpr std::uint32_t kEnSetFocus = 0x0100;
constexpr std::uint32_t kEnKillFocus = 0x0200;
constexpr std::uint32_t kEnChange = 0x0300;
constexpr std::uint32_t kBnClicked = 0;
constexpr std::int32_t kNmDblClk = -3;
constexpr std::int32_t kLvnItemChanged = -101;
constexpr std::int32_t kLvnColumnClick = -108;
constexpr std::uint32_t kCbAddString = 0x0143;
constexpr std::uint32_t kCbGetCurSel = 0x0147;
constexpr std::uint32_t kCbSetCurSel = 0x014E;
constexpr std::uint32_t kLvmDeleteAllItems = 0x1009;
constexpr std::uint32_t kLvmGetItemA = 0x1005;
constexpr std::uint32_t kLvmSetItemTextA = 0x1006;
constexpr std::uint32_t kLvmInsertItemA = 0x1007;
constexpr std::uint32_t kLvmGetNextItem = 0x100C;
constexpr std::uint32_t kLvmInsertColumnA = 0x101B;
constexpr std::uint32_t kLvmGetItemTextA = 0x102D;
constexpr std::uint32_t kLvmSetExtendedListViewStyle = 0x1036;
constexpr std::uint32_t kLvmSortItemsEx = 0x1051;

// WNDCLASSA usado pelo alvo (sem o campo cbSize de WNDCLASSEXA).
struct GuestWndClassA {
    std::uint32_t style{};
    std::uint32_t padding{};
    std::uintptr_t window_proc{};
    std::int32_t class_extra{};
    std::int32_t window_extra{};
    void* instance{};
    void* icon{};
    void* cursor{};
    void* background{};
    const char* menu_name{};
    const char* class_name{};
};
static_assert(sizeof(GuestWndClassA) == 72);

struct GuestLvColumnA {
    std::uint32_t mask{};
    std::int32_t format{};
    std::int32_t width{};
    const char* text{};
    std::int32_t text_capacity{};
    std::int32_t subitem{};
};
static_assert(sizeof(GuestLvColumnA) == 32);

struct GuestLvItemA {
    std::uint32_t mask{};
    std::int32_t item{};
    std::int32_t subitem{};
    std::uint32_t state{};
    std::uint32_t state_mask{};
    char* text{};
    std::int32_t text_capacity{};
    std::int32_t image{};
    std::intptr_t param{};
    std::int32_t indent{};
    std::int32_t group_id{};
    std::int32_t columns{};
    const void* column_data{};
};
static_assert(sizeof(GuestLvItemA) == 72);

struct GuestNmListView {
    void* hwnd_from{};
    std::uintptr_t id_from{};
    std::int32_t code{};
    std::int32_t padding{};
    std::int32_t item{};
    std::int32_t subitem{};
    std::uint32_t new_state{};
    std::uint32_t old_state{};
    std::uint32_t changed{};
    std::int32_t point_x{};
    std::int32_t point_y{};
    std::intptr_t param{};
};
static_assert(sizeof(GuestNmListView) == 64);

// MSG com layout Microsoft x64 (48 bytes). Campos em offsets fixos para
// leitura/escrita de memória convidada.
struct GuestMsg {
    HWnd hwnd{};
    std::uint32_t message{};
    std::uint32_t padding{};
    Wparam wparam{};
    Lparam lparam{};
    std::uint32_t time{};
    std::int32_t pt_x{};
    std::int32_t pt_y{};
};
static_assert(sizeof(GuestMsg) == 48);

// WNDCLASSEXA com layout Microsoft x64 (80 bytes).
struct GuestWndClassExA {
    std::uint32_t cb_size{};
    std::uint32_t style{};
    std::uintptr_t window_proc{};
    std::int32_t class_extra{};
    std::int32_t window_extra{};
    void* instance{};
    void* icon{};
    void* cursor{};
    void* background{};
    const char* menu_name{};
    const char* class_name{};
    void* icon_sm{};
};
static_assert(sizeof(GuestWndClassExA) == 80);

struct GuestWndClassW {
    std::uint32_t style{};
    std::uint32_t padding{};
    std::uintptr_t window_proc{};
    std::int32_t class_extra{};
    std::int32_t window_extra{};
    void* instance{};
    void* icon{};
    void* cursor{};
    void* background{};
    const std::uint16_t* menu_name{};
    const std::uint16_t* class_name{};
};
static_assert(sizeof(GuestWndClassW) == 72);

struct GuestWndClassExW {
    std::uint32_t cb_size{};
    std::uint32_t style{};
    std::uintptr_t window_proc{};
    std::int32_t class_extra{};
    std::int32_t window_extra{};
    void* instance{};
    void* icon{};
    void* cursor{};
    void* background{};
    const std::uint16_t* menu_name{};
    const std::uint16_t* class_name{};
    void* icon_sm{};
};
static_assert(sizeof(GuestWndClassExW) == 80);

// INITCOMMONCONTROLSEX no ABI AMD64: cbSize e dwICC são DWORDs.
struct GuestInitCommonControlsEx {
    std::uint32_t size{};
    std::uint32_t classes{};
};
static_assert(sizeof(GuestInitCommonControlsEx) == 8);

// RECT com layout Microsoft x64 (16 bytes).
struct GuestRect {
    std::int32_t left{};
    std::int32_t top{};
    std::int32_t right{};
    std::int32_t bottom{};
};
static_assert(sizeof(GuestRect) == 16);

// PAINTSTRUCT com layout Microsoft x64 (72 bytes).
struct GuestPaintStruct {
    void* hdc{};
    std::int32_t f_erase{};
    GuestRect rc_paint{};
    std::int32_t f_restore{};
    std::int32_t f_inc_update{};
    std::uint8_t rgb_reserved[32]{};
};
static_assert(sizeof(GuestPaintStruct) == 72);

// MEMORY_BASIC_INFORMATION com layout Microsoft x64 (48 bytes, sem o campo
// PartitionId das versões recentes do SDK). Os campos são preenchidos pela
// fronteira a partir do /proc/self/maps do hospedeiro.
struct GuestMemoryBasicInformation {
    void* base_address{};
    void* allocation_base{};
    std::uint32_t allocation_protect{};
    std::uint32_t padding1{};
    std::uintptr_t region_size{};
    std::uint32_t state{};
    std::uint32_t protect{};
    std::uint32_t type{};
    std::uint32_t padding2{};
};
static_assert(sizeof(GuestMemoryBasicInformation) == 48);

struct GuestSystemInfo {
    std::uint16_t processor_architecture{};
    std::uint16_t reserved{};
    std::uint32_t page_size{};
    void* minimum_application_address{};
    void* maximum_application_address{};
    std::uintptr_t active_processor_mask{};
    std::uint32_t number_of_processors{};
    std::uint32_t processor_type{};
    std::uint32_t allocation_granularity{};
    std::uint16_t processor_level{};
    std::uint16_t processor_revision{};
};
static_assert(sizeof(GuestSystemInfo) == 48);

struct GuestMemoryStatusEx {
    std::uint32_t length{};
    std::uint32_t memory_load{};
    std::uint64_t total_phys{};
    std::uint64_t avail_phys{};
    std::uint64_t total_page_file{};
    std::uint64_t avail_page_file{};
    std::uint64_t total_virtual{};
    std::uint64_t avail_virtual{};
    std::uint64_t avail_extended_virtual{};
};
static_assert(sizeof(GuestMemoryStatusEx) == 64);

struct GuestSystemTime {
    std::uint16_t year{};
    std::uint16_t month{};
    std::uint16_t day_of_week{};
    std::uint16_t day{};
    std::uint16_t hour{};
    std::uint16_t minute{};
    std::uint16_t second{};
    std::uint16_t milliseconds{};
};
static_assert(sizeof(GuestSystemTime) == 16);

struct GuestStartupInfoW {
    std::uint32_t cb{};
    std::uint32_t padding{};
    std::uint16_t* reserved{};
    std::uint16_t* desktop{};
    std::uint16_t* title{};
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t x_size{};
    std::uint32_t y_size{};
    std::uint32_t x_count_chars{};
    std::uint32_t y_count_chars{};
    std::uint32_t fill_attribute{};
    std::uint32_t flags{};
    std::uint16_t show_window{};
    std::uint16_t reserved2_size{};
    std::uint8_t* reserved2{};
    void* std_input{};
    void* std_output{};
    void* std_error{};
};
static_assert(sizeof(GuestStartupInfoW) == 104);

struct GuestFileBasicInfo {
    std::int64_t creation_time{};
    std::int64_t last_access_time{};
    std::int64_t last_write_time{};
    std::int64_t change_time{};
    std::uint32_t file_attributes{};
    std::uint32_t reserved{};
};
static_assert(sizeof(GuestFileBasicInfo) == 40);

struct GuestFileDispositionInfo {
    std::uint8_t delete_file{};
};
static_assert(sizeof(GuestFileDispositionInfo) == 1);

struct GuestFileDispositionInfoEx {
    std::uint32_t flags{};
};
static_assert(sizeof(GuestFileDispositionInfoEx) == 4);

// Estruturas de segurança no layout Microsoft x64. SID é variável: começa
// com este cabeçalho de 8 bytes e contém N subautoridades de 32 bits.
struct GuestSidHeader {
    std::uint8_t revision{};
    std::uint8_t sub_authority_count{};
    std::uint8_t identifier_authority[6]{};
};
static_assert(sizeof(GuestSidHeader) == 8);

struct GuestSidAndAttributes {
    void* sid{};
    std::uint32_t attributes{};
    std::uint32_t padding{};
};
static_assert(sizeof(GuestSidAndAttributes) == 16);

struct GuestTokenUser {
    GuestSidAndAttributes user{};
};
static_assert(sizeof(GuestTokenUser) == 16);

struct GuestTokenElevation {
    std::uint32_t token_is_elevated{};
};
static_assert(sizeof(GuestTokenElevation) == 4);

struct GuestSecurityDescriptor {
    std::uint8_t revision{};
    std::uint8_t sbz1{};
    std::uint16_t control{};
    std::uint32_t padding{};
    void* owner{};
    void* group{};
    void* sacl{};
    void* dacl{};
};
static_assert(sizeof(GuestSecurityDescriptor) == 40);

struct GuestAcl {
    std::uint8_t revision{};
    std::uint8_t sbz1{};
    std::uint16_t acl_size{};
    std::uint16_t ace_count{};
    std::uint16_t sbz2{};
};
static_assert(sizeof(GuestAcl) == 8);

struct GuestTrusteeW {
    void* multiple_trustee{};
    std::uint32_t multiple_trustee_operation{};
    std::uint32_t trustee_form{};
    std::uint32_t trustee_type{};
    std::uint32_t padding{};
    void* name{};
};
static_assert(sizeof(GuestTrusteeW) == 32);

struct GuestExplicitAccessW {
    std::uint32_t access_permissions{};
    std::uint32_t access_mode{};
    std::uint32_t inheritance{};
    std::uint32_t padding{};
    GuestTrusteeW trustee{};
};
static_assert(sizeof(GuestExplicitAccessW) == 48);

constexpr Dword kTokenUser = 1;
constexpr Dword kTokenElevation = 20;
constexpr Dword kTokenQuery = 0x0008U;
constexpr Dword kOwnerSecurityInformation = 0x00000001U;
constexpr Dword kGroupSecurityInformation = 0x00000002U;
constexpr Dword kDaclSecurityInformation = 0x00000004U;
constexpr Dword kSaclSecurityInformation = 0x00000008U;
constexpr Dword kSeFileObject = 1;
constexpr Dword kSecurityDescriptorRevision = 1;
constexpr Dword kSeDaclPresent = 0x0004U;
constexpr Dword kAclRevision = 2;
constexpr Dword kAccessAllowedAceType = 0;
constexpr Dword kAccessDeniedAceType = 1;
constexpr Dword kGrantAccess = 1;
constexpr Dword kSetAccess = 2;
constexpr Dword kDenyAccess = 3;
constexpr Dword kRevokeAccess = 4;
constexpr Dword kTrusteeIsSid = 0;
constexpr Dword kTrusteeIsUnknown = 0;
constexpr Dword kNoMultipleTrustee = 0;
constexpr Dword kWinWorldSid = 1;
constexpr Dword kWinBuiltinAdministratorsSid = 26;
constexpr Dword kGenericAll = 0x10000000U;

struct alignas(16) GuestSListHeader {
    std::uint64_t alignment{};
    std::uint64_t region{};
};
static_assert(sizeof(GuestSListHeader) == 16);
static_assert(alignof(GuestSListHeader) == 16);

struct GuestCoord {
    std::int16_t x{};
    std::int16_t y{};
};

struct GuestSmallRect {
    std::int16_t left{};
    std::int16_t top{};
    std::int16_t right{};
    std::int16_t bottom{};
};

struct GuestConsoleScreenBufferInfo {
    GuestCoord dw_size{80, 25};
    GuestCoord dw_cursor_position{0, 0};
    std::uint16_t w_attributes{0x07};
    GuestSmallRect sr_window{0, 0, 79, 24};
    GuestCoord dw_maximum_window_size{80, 25};
};
static_assert(sizeof(GuestConsoleScreenBufferInfo) == 22);

struct GuestProcessEntry32W {
    std::uint32_t dwSize{};
    std::uint32_t cntUsage{};
    std::uint32_t th32ProcessID{};
    std::uint32_t padding1{};
    std::uintptr_t th32DefaultHeapID{};
    std::uint32_t th32ModuleID{};
    std::uint32_t cntThreads{};
    std::uint32_t th32ParentProcessID{};
    std::int32_t pcPriClassBase{};
    std::uint32_t dwFlags{};
    std::uint16_t szExeFile[260]{};
    std::uint32_t padding2{};
};
static_assert(sizeof(GuestProcessEntry32W) == 568);

constexpr Dword kTh32csSnapProcess = 0x00000002U;
constexpr Dword kTh32csSnapThread = 0x00000004U;
constexpr Dword kTh32csSnapModule = 0x00000008U;
constexpr Dword kInvalidHandleValue = 0xFFFFFFFFU;

}  // namespace abi

// Fronteira de ABI: funções hospedeiras chamadas por código PE32+ x86-64.
// Todas usam a convenção Microsoft x64 (TL_MSABI) e não propagam exceções
// C++. Os handles retornados são tokens opacos válidos somente para as APIs
// de console suportadas nesta fase.
extern "C" {

TL_MSABI void* tl_GetStdHandle(std::uint32_t n_std_handle) noexcept;
TL_MSABI int tl_SetStdHandle(std::uint32_t n_std_handle, void* handle) noexcept;
TL_MSABI std::uint32_t tl_GetFileType(const void* handle) noexcept;
TL_MSABI std::uint32_t tl_GetSystemDirectoryW(std::uint16_t* buffer,
                                              std::uint32_t size) noexcept;
TL_MSABI void tl_GetStartupInfoW(abi::GuestStartupInfoW* startup_info) noexcept;
TL_MSABI int tl_ReadConsoleW(const void* console_input, std::uint16_t* buffer,
                             std::uint32_t chars_to_read, std::uint32_t* chars_read,
                             const void* input_control) noexcept;
TL_MSABI int tl_WriteConsoleW(const void* console_output, const std::uint16_t* buffer,
                              std::uint32_t chars_to_write, std::uint32_t* chars_written,
                              const void* reserved) noexcept;
TL_MSABI int tl_IsDebuggerPresent() noexcept;
TL_MSABI int tl_IsProcessorFeaturePresent(std::uint32_t processor_feature) noexcept;
TL_MSABI void* tl_EncodePointer(void* pointer) noexcept;
TL_MSABI void* tl_DecodePointer(void* pointer) noexcept;
TL_MSABI void tl_InitializeSListHead(abi::GuestSListHeader* list_head) noexcept;
TL_MSABI int tl_WriteFile(const void* file, const void* buffer, std::uint32_t bytes_to_write,
                          std::uint32_t* bytes_written, void* overlapped) noexcept;
TL_MSABI int tl_ReadFile(const void* file, void* buffer, std::uint32_t bytes_to_read,
                         std::uint32_t* bytes_read, void* overlapped) noexcept;
TL_MSABI void tl_ExitProcess(std::uint32_t exit_code) noexcept;
TL_MSABI std::uint32_t tl_GetLastError() noexcept;
TL_MSABI void tl_SetLastError(std::uint32_t error) noexcept;
TL_MSABI void* tl_VirtualAlloc(void* address, std::size_t size, std::uint32_t allocation_type,
                               std::uint32_t protection) noexcept;
TL_MSABI int tl_VirtualFree(void* address, std::size_t size,
                            std::uint32_t free_type) noexcept;
TL_MSABI void* tl_CreateFileA(const char* path, std::uint32_t desired_access,
                              std::uint32_t share_mode, const void* security_attributes,
                              std::uint32_t creation_disposition, std::uint32_t flags,
                              const void* template_file) noexcept;
TL_MSABI void* tl_CreateFileW(const std::uint16_t* path, std::uint32_t desired_access,
                              std::uint32_t share_mode, const void* security_attributes,
                              std::uint32_t creation_disposition, std::uint32_t flags,
                              const void* template_file) noexcept;
TL_MSABI int tl_CloseHandle(const void* handle) noexcept;
TL_MSABI void* tl_CreateMutexA(const void* security_attributes, int initial_owner,
                               const char* name) noexcept;
TL_MSABI void* tl_CreateMutexW(const void* security_attributes, int initial_owner,
                               const std::uint16_t* name) noexcept;
TL_MSABI void* tl_CreateEventA(const void* security_attributes, int manual_reset,
                               int initial_state, const char* name) noexcept;
TL_MSABI void* tl_CreateEventW(const void* security_attributes, int manual_reset,
                               int initial_state, const std::uint16_t* name) noexcept;
TL_MSABI int tl_SetEvent(const void* event_handle) noexcept;
TL_MSABI int tl_ResetEvent(const void* event_handle) noexcept;
TL_MSABI int tl_ReleaseMutex(const void* mutex) noexcept;
TL_MSABI int tl_CreateProcessA(const char* application_name, char* command_line,
                               const void* process_attributes, const void* thread_attributes,
                               int inherit_handles, std::uint32_t creation_flags,
                               const void* environment, const char* current_directory,
                               const void* startup_info, void* process_information) noexcept;
TL_MSABI int tl_CreateProcessW(const std::uint16_t* application_name,
                               std::uint16_t* command_line, const void* process_attributes,
                               const void* thread_attributes, int inherit_handles,
                               std::uint32_t creation_flags, const void* environment,
                               const std::uint16_t* current_directory, void* startup_info,
                               void* process_information) noexcept;
TL_MSABI int tl_GetExitCodeProcess(const void* process, std::uint32_t* exit_code) noexcept;
TL_MSABI int tl_TerminateProcess(const void* process, std::uint32_t exit_code) noexcept;
TL_MSABI void* tl_CreateSemaphoreA(const void* security_attributes, std::int32_t initial_count,
                                   std::int32_t maximum_count, const char* name) noexcept;
TL_MSABI void* tl_CreateSemaphoreW(const void* security_attributes, std::int32_t initial_count,
                                   std::int32_t maximum_count, const std::uint16_t* name) noexcept;
TL_MSABI int tl_ReleaseSemaphore(const void* semaphore, std::int32_t release_count,
                                  std::int32_t* previous_count) noexcept;
TL_MSABI std::uint32_t tl_WaitForMultipleObjects(std::uint32_t count,
                                                  const void* const* handles, int wait_all,
                                                  std::uint32_t milliseconds) noexcept;
TL_MSABI void tl_GetStartupInfoA(void* startup_info) noexcept;
TL_MSABI int tl_MulDiv(int number, int numerator, int denominator) noexcept;
TL_MSABI int tl_MessageBoxA(const void* owner, const char* text, const char* caption,
                            std::uint32_t type) noexcept;
TL_MSABI abi::Atom tl_RegisterClassExA(const void* wnd_class) noexcept;
TL_MSABI abi::Atom tl_RegisterClassA(const void* wnd_class) noexcept;
TL_MSABI abi::Atom tl_RegisterClassExW(const void* wnd_class) noexcept;
TL_MSABI abi::Atom tl_RegisterClassW(const void* wnd_class) noexcept;
TL_MSABI abi::HWnd tl_CreateWindowExA(std::uint32_t ex_style, const char* class_name,
                                      const char* window_name, std::uint32_t style, int x, int y,
                                      int width, int height, const void* parent, const void* menu,
                                      const void* instance, const void* param) noexcept;
TL_MSABI abi::HWnd tl_CreateWindowExW(std::uint32_t ex_style, const std::uint16_t* class_name,
                                      const std::uint16_t* window_name, std::uint32_t style, int x, int y,
                                      int width, int height, const void* parent, const void* menu,
                                      const void* instance, const void* param) noexcept;
TL_MSABI int tl_ShowWindow(const void* window, int cmd_show) noexcept;
TL_MSABI int tl_UpdateWindow(const void* window) noexcept;
TL_MSABI int tl_GetMessageA(void* msg, const void* window, std::uint32_t filter_min,
                             std::uint32_t filter_max) noexcept;
TL_MSABI int tl_GetMessageW(void* msg, const void* window, std::uint32_t filter_min,
                             std::uint32_t filter_max) noexcept;
TL_MSABI int tl_TranslateMessage(const void* msg) noexcept;
TL_MSABI abi::Lresult tl_DispatchMessageA(const void* msg) noexcept;
TL_MSABI abi::Lresult tl_DispatchMessageW(const void* msg) noexcept;
TL_MSABI abi::Lresult tl_DefWindowProcA(const void* window, std::uint32_t message,
                                        abi::Wparam wparam, abi::Lparam lparam) noexcept;
TL_MSABI abi::Lresult tl_DefWindowProcW(const void* window, std::uint32_t message,
                                        abi::Wparam wparam, abi::Lparam lparam) noexcept;
TL_MSABI int tl_DestroyWindow(const void* window) noexcept;
TL_MSABI void tl_PostQuitMessage(int exit_code) noexcept;
TL_MSABI std::uintptr_t tl_SetTimer(const void* window, std::uintptr_t id, std::uint32_t elapsed_ms,
                                    const void* timer_proc) noexcept;
TL_MSABI int tl_KillTimer(const void* window, std::uintptr_t id) noexcept;
TL_MSABI void* tl_GetStockObject(int object) noexcept;
TL_MSABI void* tl_BeginPaint(const void* window, void* paint_struct) noexcept;
TL_MSABI int tl_EndPaint(const void* window, const void* paint_struct) noexcept;
TL_MSABI int tl_TextOut(const void* dc, int x, int y, const char* text, int length) noexcept;
TL_MSABI int tl_FillRect(const void* dc, const void* rect, const void* brush) noexcept;
TL_MSABI int tl_Rectangle(const void* dc, int left, int top, int right, int bottom) noexcept;
TL_MSABI void* tl_GetDC(const void* window) noexcept;
TL_MSABI int tl_ReleaseDC(const void* window, const void* dc) noexcept;
TL_MSABI void* tl_CreateFontA(int height, int width, int escapement, int orientation, int weight,
                              std::uint32_t italic, std::uint32_t underline,
                              std::uint32_t strikeout, std::uint32_t charset,
                              std::uint32_t output_precision, std::uint32_t clip_precision,
                              std::uint32_t quality, std::uint32_t pitch_and_family,
                              const char* face_name) noexcept;
TL_MSABI void* tl_CreateSolidBrush(std::uint32_t color) noexcept;
TL_MSABI int tl_DeleteObject(const void* object) noexcept;
TL_MSABI std::uint32_t tl_SetBkColor(const void* dc, std::uint32_t color) noexcept;
TL_MSABI std::uint32_t tl_SetTextColor(const void* dc, std::uint32_t color) noexcept;
TL_MSABI int tl_GetClientRect(const void* window, void* rect) noexcept;
TL_MSABI int tl_GetWindowRect(const void* window, void* rect) noexcept;
TL_MSABI int tl_GetCursorPos(void* point) noexcept;
TL_MSABI int tl_MoveWindow(const void* window, int x, int y, int width, int height,
                           int repaint) noexcept;
TL_MSABI std::intptr_t tl_SetWindowPos(const void* window, const void* insert_after, int x, int y,
                                       int width, int height, std::uint32_t flags) noexcept;
TL_MSABI int tl_SetWindowTextA(const void* window, const char* text) noexcept;
TL_MSABI int tl_SetWindowTextW(const void* window, const std::uint16_t* text) noexcept;
TL_MSABI int tl_GetWindowTextA(const void* window, char* text, int capacity) noexcept;
TL_MSABI int tl_GetWindowTextW(const void* window, std::uint16_t* text, int capacity) noexcept;
TL_MSABI int tl_GetWindowTextLengthA(const void* window) noexcept;
TL_MSABI int tl_GetWindowTextLengthW(const void* window) noexcept;
TL_MSABI int tl_EnableWindow(const void* window, int enable) noexcept;
TL_MSABI const void* tl_SetFocus(const void* window) noexcept;
TL_MSABI int tl_IsWindowVisible(const void* window) noexcept;
TL_MSABI int tl_InvalidateRect(const void* window, const void* rect, int erase) noexcept;
TL_MSABI const void* tl_FindWindowA(const char* class_name, const char* window_name) noexcept;
TL_MSABI const void* tl_FindWindowW(const std::uint16_t* class_name, const std::uint16_t* window_name) noexcept;
TL_MSABI std::uintptr_t tl_LoadCursorA(const void* instance, const char* name) noexcept;
TL_MSABI std::uintptr_t tl_LoadCursorW(const void* instance, const std::uint16_t* name) noexcept;
TL_MSABI std::uintptr_t tl_LoadIconA(const void* instance, const char* name) noexcept;
TL_MSABI std::uintptr_t tl_LoadIconW(const void* instance, const std::uint16_t* name) noexcept;
TL_MSABI std::intptr_t tl_SetClassLongPtrA(const void* window, int index,
                                             std::intptr_t value) noexcept;
TL_MSABI std::intptr_t tl_SetClassLongPtrW(const void* window, int index,
                                             std::intptr_t value) noexcept;
TL_MSABI int tl_SetForegroundWindow(const void* window) noexcept;
TL_MSABI int tl_SendMessageA(const void* window, std::uint32_t message, abi::Wparam wparam,
                              abi::Lparam lparam) noexcept;
TL_MSABI int tl_SendMessageW(const void* window, std::uint32_t message, abi::Wparam wparam,
                              abi::Lparam lparam) noexcept;
TL_MSABI std::intptr_t tl_DialogBoxParamW(const void* instance, const std::uint16_t* template_name,
                                           const void* parent, std::uintptr_t dialog_proc,
                                           abi::Lparam init_param) noexcept;
TL_MSABI int tl_EndDialog(const void* dialog, std::intptr_t result) noexcept;
TL_MSABI void* tl_GetDlgItem(const void* dialog, int identifier) noexcept;
TL_MSABI int tl_SetDlgItemTextW(const void* dialog, int identifier,
                                const std::uint16_t* text) noexcept;
TL_MSABI abi::Lresult tl_SendDlgItemMessageW(const void* dialog, int identifier,
                                             std::uint32_t message, abi::Wparam wparam,
                                             abi::Lparam lparam) noexcept;
TL_MSABI void* tl_GetNextDlgTabItem(const void* dialog, const void* control,
                                    int previous) noexcept;
TL_MSABI int tl_IsDialogMessageW(const void* dialog, const void* message) noexcept;
TL_MSABI std::int32_t tl_GetWindowLongW(const void* window, int index) noexcept;
TL_MSABI std::int32_t tl_SetWindowLongW(const void* window, int index,
                                        std::int32_t new_long) noexcept;
TL_MSABI void* tl_CopyImage(const void* image, std::uint32_t image_type, int width, int height,
                            std::uint32_t flags) noexcept;
TL_MSABI int tl_DestroyIcon(const void* icon) noexcept;
TL_MSABI int tl_PostMessageA(const void* window, std::uint32_t message, abi::Wparam wparam,
                              abi::Lparam lparam) noexcept;
TL_MSABI int tl_PostMessageW(const void* window, std::uint32_t message, abi::Wparam wparam,
                              abi::Lparam lparam) noexcept;
TL_MSABI void* tl_CreatePopupMenu() noexcept;
TL_MSABI int tl_AppendMenuA(const void* menu, std::uint32_t flags, std::uintptr_t command,
                             const char* text) noexcept;
TL_MSABI int tl_AppendMenuW(const void* menu, std::uint32_t flags, std::uintptr_t command,
                             const std::uint16_t* text) noexcept;
TL_MSABI int tl_DestroyMenu(const void* menu) noexcept;
TL_MSABI int tl_TrackPopupMenu(const void* menu, std::uint32_t flags, int x, int y, int reserved,
                               const void* owner, const void* rect) noexcept;
constexpr std::uint32_t kGwHwndFirst = 0;
constexpr std::uint32_t kGwHwndLast = 1;
constexpr std::uint32_t kGwHwndNext = 2;
constexpr std::uint32_t kGwHwndPrev = 3;
constexpr std::uint32_t kGwOwner = 4;
constexpr std::uint32_t kGwChild = 5;

constexpr std::uint32_t kPmNoRemove = 0x0000;
constexpr std::uint32_t kPmRemove = 0x0001;
constexpr std::uint32_t kPmNoYield = 0x0002;

constexpr int kColorScrollbar = 0;
constexpr int kColorBackground = 1;
constexpr int kColorActiveCaption = 2;
constexpr int kColorInactiveCaption = 3;
constexpr int kColorMenu = 4;
constexpr int kColorWindow = 5;
constexpr int kColorWindowFrame = 6;
constexpr int kColorMenuText = 7;
constexpr int kColorWindowText = 8;
constexpr int kColorCaptionText = 9;
constexpr int kColorActiveBorder = 10;
constexpr int kColorInactiveBorder = 11;
constexpr int kColorAppWorkspace = 12;
constexpr int kColorHighlight = 13;
constexpr int kColorHighlightText = 14;
constexpr int kColorBtnFace = 15;
constexpr int kColorBtnShadow = 16;
constexpr int kColorGrayText = 17;
constexpr int kColorBtnText = 18;
constexpr int kColorInactiveCaptionText = 19;
constexpr int kColorBtnHighlight = 20;
constexpr int kColor3dDkShadow = 21;
constexpr int kColor3dLight = 22;
constexpr int kColorInfoText = 23;
constexpr int kColorInfoBk = 24;

constexpr std::uint32_t kDtTop = 0x00000000;
constexpr std::uint32_t kDtLeft = 0x00000000;
constexpr std::uint32_t kDtCenter = 0x00000001;
constexpr std::uint32_t kDtRight = 0x00000002;
constexpr std::uint32_t kDtVCenter = 0x00000004;
constexpr std::uint32_t kDtBottom = 0x00000008;
constexpr std::uint32_t kDtWordBreak = 0x00000010;
constexpr std::uint32_t kDtSingleLine = 0x00000020;
constexpr std::uint32_t kDtExpandTabs = 0x00000040;
constexpr std::uint32_t kDtTabStop = 0x00000080;
constexpr std::uint32_t kDtNoClip = 0x00000100;
constexpr std::uint32_t kDtExternalLeading = 0x00000200;
constexpr std::uint32_t kDtCalcRect = 0x00000400;
constexpr std::uint32_t kDtNoPrefix = 0x00000800;
constexpr std::uint32_t kDtInternal = 0x00001000;

inline void* const kDesktopHwndToken = reinterpret_cast<void*>(0x00010000ULL);
inline void* const kDefaultMonitorToken = reinterpret_cast<void*>(0x00010001ULL);

TL_MSABI void* tl_GetDesktopWindow() noexcept;
TL_MSABI void* tl_GetFocus() noexcept;
TL_MSABI void* tl_SetCapture(const void* window) noexcept;
TL_MSABI int tl_ReleaseCapture() noexcept;
TL_MSABI void* tl_GetCapture() noexcept;
TL_MSABI int tl_BringWindowToTop(const void* window) noexcept;
TL_MSABI void* tl_GetWindow(const void* window, std::uint32_t cmd) noexcept;
TL_MSABI int tl_GetClassNameA(const void* window, char* class_name, int max_count) noexcept;
TL_MSABI int tl_GetClassNameW(const void* window, std::uint16_t* class_name, int max_count) noexcept;
TL_MSABI std::uint32_t tl_GetWindowThreadProcessId(const void* window, std::uint32_t* process_id) noexcept;
TL_MSABI abi::Lresult tl_CallWindowProcA(std::uintptr_t prev_wnd_func, const void* window,
                                        std::uint32_t message, abi::Wparam wparam,
                                        abi::Lparam lparam) noexcept;
TL_MSABI abi::Lresult tl_CallWindowProcW(std::uintptr_t prev_wnd_func, const void* window,
                                        std::uint32_t message, abi::Wparam wparam,
                                        abi::Lparam lparam) noexcept;
TL_MSABI int tl_PeekMessageA(void* msg, const void* window, std::uint32_t filter_min,
                             std::uint32_t filter_max, std::uint32_t remove_msg) noexcept;
TL_MSABI int tl_PeekMessageW(void* msg, const void* window, std::uint32_t filter_min,
                             std::uint32_t filter_max, std::uint32_t remove_msg) noexcept;
TL_MSABI int tl_RedrawWindow(const void* window, const void* update_rect, const void* update_rgn,
                             std::uint32_t flags) noexcept;
TL_MSABI int tl_PtInRect(const void* rect, std::int32_t x, std::int32_t y) noexcept;
TL_MSABI int tl_CopyRect(void* dest_rect, const void* src_rect) noexcept;
TL_MSABI int tl_MapWindowPoints(const void* from_window, const void* to_window, void* points,
                                std::uint32_t count) noexcept;
TL_MSABI void* tl_MonitorFromWindow(const void* window, std::uint32_t flags) noexcept;
TL_MSABI std::uint32_t tl_GetSysColor(int index) noexcept;
TL_MSABI std::uint16_t* tl_CharUpperW(std::uint16_t* str) noexcept;
TL_MSABI std::uint16_t* tl_CharLowerW(std::uint16_t* str) noexcept;
TL_MSABI int tl_DrawTextA(const void* dc, const char* text, int count, void* rect,
                          std::uint32_t format) noexcept;
TL_MSABI int tl_DrawTextW(const void* dc, const std::uint16_t* text, int count, void* rect,
                          std::uint32_t format) noexcept;
TL_MSABI void tl_DeleteCriticalSection(void* critical_section) noexcept;
TL_MSABI void tl_EnterCriticalSection(void* critical_section) noexcept;
TL_MSABI int tl_GetConsoleMode(const void* handle, std::uint32_t* mode) noexcept;
TL_MSABI void tl_InitializeCriticalSection(void* critical_section) noexcept;
TL_MSABI int tl_InitializeCriticalSectionAndSpinCount(void* critical_section,
                                                       std::uint32_t spin_count) noexcept;
TL_MSABI int tl_InitializeCriticalSectionEx(void* critical_section, std::uint32_t spin_count,
                                             std::uint32_t flags) noexcept;
TL_MSABI int tl_IsDBCSLeadByteEx(std::uint32_t code_page, std::uint8_t test_char) noexcept;
TL_MSABI void tl_LeaveCriticalSection(void* critical_section) noexcept;
TL_MSABI int tl_MultiByteToWideChar(std::uint32_t code_page, std::uint32_t flags, const char* mb_str,
                                   int mb_count, std::uint16_t* wide_str, int wide_count) noexcept;
TL_MSABI int tl_SetConsoleMode(const void* handle, std::uint32_t mode) noexcept;
TL_MSABI std::uintptr_t tl_SetUnhandledExceptionFilter(std::uintptr_t handler) noexcept;
TL_MSABI void tl_Sleep(std::uint32_t milliseconds) noexcept;
TL_MSABI void* tl_TlsGetValue(std::uint32_t tls_index) noexcept;
TL_MSABI int tl_VirtualProtect(void* address, std::uintptr_t size, std::uint32_t new_protection,
                               std::uint32_t* old_protection) noexcept;
TL_MSABI std::uintptr_t tl_VirtualQuery(const void* address, void* memory_information,
                                        std::uintptr_t length) noexcept;
TL_MSABI int tl_WideCharToMultiByte(std::uint32_t code_page, std::uint32_t flags,
                                     const std::uint16_t* wide_str, int wide_count, char* mb_str,
                                     int mb_count, const char* default_char,
                                     int* used_default_char) noexcept;
TL_MSABI void* tl_GetModuleHandleA(const char* module_name) noexcept;
TL_MSABI void* tl_GetModuleHandleW(const std::uint16_t* module_name) noexcept;
TL_MSABI int tl_GetModuleHandleExA(std::uint32_t flags, const char* module_name, void** module) noexcept;
TL_MSABI int tl_GetModuleHandleExW(std::uint32_t flags, const std::uint16_t* module_name, void** module) noexcept;
TL_MSABI void* tl_LoadLibraryA(const char* file_name) noexcept;
TL_MSABI void* tl_LoadLibraryW(const std::uint16_t* file_name) noexcept;
TL_MSABI void* tl_LoadLibraryExA(const char* file_name, void* file, std::uint32_t flags) noexcept;
TL_MSABI void* tl_LoadLibraryExW(const std::uint16_t* file_name, void* file, std::uint32_t flags) noexcept;
TL_MSABI int tl_FreeLibrary(void* module) noexcept;
TL_MSABI void* tl_GetProcAddress(void* module, const char* name) noexcept;
TL_MSABI int tl_GetVersionExA(void* version_information) noexcept;
TL_MSABI int tl_GetVersionExW(void* version_information) noexcept;
TL_MSABI int tl_VerifyVersionInfoW(void* version_information, std::uint32_t type_mask,
                                   std::uint64_t condition_mask) noexcept;
TL_MSABI std::uint64_t tl_VerSetConditionMask(std::uint64_t condition_mask, std::uint32_t type_mask,
                                              std::uint8_t condition) noexcept;
TL_MSABI int tl_GetUserDefaultLocaleName(std::uint16_t* locale_name, int locale_name_length) noexcept;
TL_MSABI std::uint32_t tl_LocaleNameToLCID(const std::uint16_t* name, std::uint32_t flags) noexcept;
TL_MSABI int tl_WaitOnAddress(void* address, void* compare_address, std::size_t address_size,
                              std::uint32_t milliseconds) noexcept;
TL_MSABI void tl_WakeByAddressSingle(void* address) noexcept;
TL_MSABI void tl_WakeByAddressAll(void* address) noexcept;
TL_MSABI const char* tl_GetCommandLineA() noexcept;
TL_MSABI const std::uint16_t* tl_GetCommandLineW() noexcept;
TL_MSABI std::uint32_t tl_GetEnvironmentVariableA(const char* name, char* buffer,
                                                   std::uint32_t size) noexcept;
TL_MSABI std::uint32_t tl_GetEnvironmentVariableW(const std::uint16_t* name, std::uint16_t* buffer,
                                                   std::uint32_t size) noexcept;
TL_MSABI int tl_SetEnvironmentVariableW(const std::uint16_t* name,
                                        const std::uint16_t* value) noexcept;
TL_MSABI std::uint16_t* tl_GetEnvironmentStringsW() noexcept;
TL_MSABI int tl_FreeEnvironmentStringsW(std::uint16_t* block) noexcept;
TL_MSABI std::uint32_t tl_ExpandEnvironmentStringsW(const std::uint16_t* source,
                                                     std::uint16_t* destination,
                                                     std::uint32_t size) noexcept;
TL_MSABI std::uint32_t tl_GetACP() noexcept;
TL_MSABI std::uint32_t tl_GetOEMCP() noexcept;
TL_MSABI int tl_GetCPInfo(std::uint32_t code_page, abi::GuestCpInfo* info) noexcept;
TL_MSABI int tl_GetLocaleInfoW(std::uint32_t locale, std::uint32_t locale_type,
                               std::uint16_t* data, int data_count) noexcept;
TL_MSABI int tl_GetLocaleInfoEx(const std::uint16_t* locale_name, std::uint32_t locale_type,
                                std::uint16_t* data, int data_count) noexcept;
TL_MSABI int tl_IsValidLocale(std::uint32_t locale, std::uint32_t flags) noexcept;
TL_MSABI int tl_IsValidCodePage(std::uint32_t code_page) noexcept;
TL_MSABI int tl_EnumSystemLocalesW(std::uintptr_t callback, std::uint32_t flags) noexcept;
TL_MSABI int tl_GetStringTypeW(std::uint32_t info_type, const std::uint16_t* source,
                               int source_count, std::uint16_t* char_type) noexcept;
TL_MSABI int tl_GetDateFormatW(std::uint32_t locale, std::uint32_t flags,
                               const abi::GuestSystemTime* date, const std::uint16_t* format,
                               std::uint16_t* data, int data_count) noexcept;
TL_MSABI int tl_GetTimeFormatW(std::uint32_t locale, std::uint32_t flags,
                               const abi::GuestSystemTime* time, const std::uint16_t* format,
                               std::uint16_t* data, int data_count) noexcept;
TL_MSABI int tl_LCMapStringW(std::uint32_t locale, std::uint32_t flags,
                             const std::uint16_t* source, int source_count,
                             std::uint16_t* destination, int destination_count) noexcept;
TL_MSABI int tl_LCMapStringEx(const std::uint16_t* locale_name, std::uint32_t flags,
                              const std::uint16_t* source, int source_count,
                              std::uint16_t* destination, int destination_count,
                              const void* version_information, void* reserved,
                              std::uintptr_t sort_handle) noexcept;
TL_MSABI void* tl_GetProcessHeap() noexcept;
TL_MSABI void* tl_HeapAlloc(void* heap, std::uint32_t flags, std::uintptr_t size) noexcept;
TL_MSABI int tl_HeapFree(void* heap, std::uint32_t flags, void* memory) noexcept;
TL_MSABI void* tl_HeapReAlloc(void* heap, std::uint32_t flags, void* memory,
                               std::uintptr_t new_size) noexcept;
TL_MSABI std::uint64_t tl_GetTickCount64() noexcept;
TL_MSABI void tl_GetSystemTimeAsFileTime(void* file_time) noexcept;

// Fase 10: Sistema de arquivos e utilitários.
TL_MSABI std::uint32_t tl_GetFileSize(const void* handle, std::uint32_t* high_size) noexcept;
TL_MSABI int tl_GetFileSizeEx(const void* handle, std::int64_t* size) noexcept;
TL_MSABI std::int32_t tl_SetFilePointer(const void* handle, std::int32_t distance,
                                         std::int32_t* high_distance,
                                         std::uint32_t move_method) noexcept;
TL_MSABI int tl_SetFilePointerEx(const void* handle, std::int64_t distance,
                                 std::int64_t* new_position, std::uint32_t move_method) noexcept;
TL_MSABI int tl_SetEndOfFile(const void* handle) noexcept;
TL_MSABI int tl_FlushFileBuffers(const void* handle) noexcept;
TL_MSABI std::uint32_t tl_GetFileAttributesA(const char* path) noexcept;
TL_MSABI std::uint32_t tl_GetFileAttributesW(const std::uint16_t* path) noexcept;
TL_MSABI int tl_SetFileAttributesW(const std::uint16_t* path,
                                   std::uint32_t attributes) noexcept;
TL_MSABI int tl_GetFileAttributesExW(const std::uint16_t* path, int info_level,
                                     void* data) noexcept;
TL_MSABI int tl_DeleteFileA(const char* path) noexcept;
TL_MSABI int tl_DeleteFileW(const std::uint16_t* path) noexcept;
TL_MSABI int tl_MoveFileA(const char* from, const char* to) noexcept;
TL_MSABI int tl_MoveFileW(const std::uint16_t* from, const std::uint16_t* to) noexcept;
TL_MSABI int tl_MoveFileExW(const std::uint16_t* from, const std::uint16_t* to,
                            std::uint32_t flags) noexcept;
TL_MSABI int tl_CopyFileW(const std::uint16_t* from, const std::uint16_t* to,
                          int fail_if_exists) noexcept;
TL_MSABI int tl_CreateDirectoryA(const char* path, const void* security_attributes) noexcept;
TL_MSABI int tl_CreateDirectoryW(const std::uint16_t* path,
                                 const void* security_attributes) noexcept;
TL_MSABI int tl_RemoveDirectoryW(const std::uint16_t* path) noexcept;
TL_MSABI void* tl_FindFirstFileA(const char* path, void* find_data) noexcept;
TL_MSABI void* tl_FindFirstFileW(const std::uint16_t* path, void* find_data) noexcept;
TL_MSABI void* tl_FindFirstFileExW(const std::uint16_t* path, int info_level,
                                   void* find_data, int search_operation,
                                   const void* search_filter,
                                   std::uint32_t additional_flags) noexcept;
TL_MSABI int tl_FindNextFileA(const void* handle, void* find_data) noexcept;
TL_MSABI int tl_FindNextFileW(const void* handle, void* find_data) noexcept;
TL_MSABI int tl_FindClose(const void* handle) noexcept;
TL_MSABI int tl_AreFileApisANSI() noexcept;
TL_MSABI std::uint32_t tl_FormatMessageW(std::uint32_t flags, const void* source,
                                         std::uint32_t message_id, std::uint32_t language_id,
                                         std::uint16_t* buffer, std::uint32_t size,
                                         const void* arguments) noexcept;
TL_MSABI std::uint32_t tl_FormatMessageA(std::uint32_t flags, const void* source,
                                         std::uint32_t message_id, std::uint32_t language_id,
                                         char* buffer, std::uint32_t size,
                                         const void* arguments) noexcept;
TL_MSABI std::uint32_t tl_GetConsoleOutputCP() noexcept;
TL_MSABI int tl_SetConsoleOutputCP(std::uint32_t code_page) noexcept;
TL_MSABI std::uint32_t tl_GetTempFileNameW(const std::uint16_t* path_name,
                                           const std::uint16_t* prefix_string,
                                           std::uint32_t unique, std::uint16_t* temp_file_name) noexcept;
TL_MSABI std::uint32_t tl_GetTempPathW(std::uint32_t buffer_length,
                                       std::uint16_t* buffer) noexcept;
TL_MSABI std::uint32_t tl_GetFullPathNameW(const std::uint16_t* path, std::uint32_t buffer_length,
                                           std::uint16_t* buffer,
                                           std::uint16_t** file_part) noexcept;
TL_MSABI std::uint32_t tl_GetFullPathNameA(const char* path, std::uint32_t buffer_length, char* buffer,
                                          char** file_part) noexcept;
TL_MSABI int tl_GetFileTime(const void* handle, void* creation_time, void* access_time,
                            void* write_time) noexcept;
TL_MSABI int tl_SetFileTime(const void* handle, const void* creation_time,
                            const void* access_time, const void* write_time) noexcept;
TL_MSABI int tl_GetFileInformationByHandle(const void* handle, void* information) noexcept;
TL_MSABI int tl_GetFileInformationByHandleEx(const void* handle, int info_class,
                                             void* buffer, std::uint32_t size) noexcept;
TL_MSABI int tl_SetFileInformationByHandle(const void* handle, int info_class,
                                           const void* buffer, std::uint32_t size) noexcept;
TL_MSABI std::uint32_t tl_GetFinalPathNameByHandleW(const void* handle, std::uint16_t* buffer,
                                                    std::uint32_t buffer_length,
                                                    std::uint32_t flags) noexcept;
TL_MSABI void* tl_FindResourceW(const void* module, const std::uint16_t* name,
                                const std::uint16_t* type) noexcept;
TL_MSABI void* tl_LoadResource(const void* module, const void* resource) noexcept;
TL_MSABI void* tl_LockResource(const void* resource) noexcept;
TL_MSABI std::uint32_t tl_SizeofResource(const void* module, const void* resource) noexcept;
TL_MSABI void* tl_GlobalAlloc(std::uint32_t flags, std::size_t bytes) noexcept;
TL_MSABI void* tl_GlobalLock(void* memory) noexcept;
TL_MSABI int tl_GlobalUnlock(void* memory) noexcept;
TL_MSABI void* tl_GlobalFree(void* memory) noexcept;
TL_MSABI void* tl_LocalAlloc(std::uint32_t flags, std::size_t bytes) noexcept;
TL_MSABI void* tl_LocalFree(void* memory) noexcept;

// Diretório atual e módulo.
TL_MSABI std::uint32_t tl_GetCurrentDirectoryA(std::uint32_t buffer_length,
                                                char* buffer) noexcept;
TL_MSABI std::uint32_t tl_GetCurrentDirectoryW(std::uint32_t buffer_length,
                                                std::uint16_t* buffer) noexcept;
TL_MSABI std::uint32_t tl_GetModuleFileNameA(const void* module_handle, char* buffer,
                                              std::uint32_t size) noexcept;
TL_MSABI std::uint32_t tl_GetModuleFileNameW(const void* module_handle, std::uint16_t* buffer,
                                              std::uint32_t size) noexcept;

// Fase 11: Concorrência.

TL_MSABI void* tl_CreateThread(const void* security_attributes, std::uintptr_t stack_size,
                                std::uintptr_t start_address, void* parameter,
                                std::uint32_t creation_flags, std::uint32_t* thread_id) noexcept;
TL_MSABI void tl_ExitThread(std::uint32_t exit_code) noexcept;
TL_MSABI std::uint32_t tl_WaitForSingleObject(const void* handle,
                                               std::uint32_t milliseconds) noexcept;
TL_MSABI std::uint32_t tl_WaitForMultipleObjects(std::uint32_t count, const void* const* handles,
                                                 int wait_all, std::uint32_t milliseconds) noexcept;
TL_MSABI std::uint32_t tl_MsgWaitForMultipleObjects(std::uint32_t count, const void* const* handles,
                                                    int wait_all, std::uint32_t milliseconds,
                                                    std::uint32_t wake_mask) noexcept;
TL_MSABI std::uint32_t tl_MsgWaitForMultipleObjectsEx(std::uint32_t count, const void* const* handles,
                                                      std::uint32_t milliseconds, std::uint32_t wake_mask,
                                                      std::uint32_t flags) noexcept;
TL_MSABI std::uint32_t tl_GetCurrentThreadId() noexcept;
TL_MSABI std::uint32_t tl_GetCurrentProcessId() noexcept;
TL_MSABI void* tl_GetCurrentProcess() noexcept;
TL_MSABI std::uint32_t tl_TlsAlloc() noexcept;
TL_MSABI int tl_TlsSetValue(std::uint32_t tls_index, void* tls_value) noexcept;
TL_MSABI int tl_TlsFree(std::uint32_t tls_index) noexcept;
TL_MSABI std::uint32_t tl_FlsAlloc(std::uintptr_t callback) noexcept;
TL_MSABI int tl_FlsFree(std::uint32_t fls_index) noexcept;
TL_MSABI void* tl_FlsGetValue(std::uint32_t fls_index) noexcept;
TL_MSABI int tl_FlsSetValue(std::uint32_t fls_index, void* value) noexcept;

// Temporização de alta resolução e hardware info (KERNEL32)
TL_MSABI int tl_QueryPerformanceCounter(std::int64_t* performance_count) noexcept;
TL_MSABI int tl_QueryPerformanceFrequency(std::int64_t* frequency) noexcept;
TL_MSABI void tl_GetSystemInfo(void* system_info) noexcept;
TL_MSABI void tl_GetNativeSystemInfo(void* system_info) noexcept;
TL_MSABI int tl_GlobalMemoryStatusEx(void* buffer) noexcept;

// File Mapping (KERNEL32)
TL_MSABI void* tl_CreateFileMappingA(const void* file, const void* file_mapping_attributes,
                                     std::uint32_t protect, std::uint32_t maximum_size_high,
                                     std::uint32_t maximum_size_low, const char* name) noexcept;
TL_MSABI void* tl_CreateFileMappingW(const void* file, const void* file_mapping_attributes,
                                     std::uint32_t protect, std::uint32_t maximum_size_high,
                                     std::uint32_t maximum_size_low, const std::uint16_t* name) noexcept;
TL_MSABI void* tl_MapViewOfFile(const void* file_mapping_object, std::uint32_t desired_access,
                                std::uint32_t file_offset_high, std::uint32_t file_offset_low,
                                std::size_t number_of_bytes_to_map) noexcept;
TL_MSABI int tl_UnmapViewOfFile(const void* base_address) noexcept;
TL_MSABI int tl_FlushViewOfFile(const void* base_address, std::size_t number_of_bytes_to_flush) noexcept;

// Volumes e Utilitários de Disco (KERNEL32)
TL_MSABI int tl_GetDiskFreeSpaceExA(const char* directory_name, std::uint64_t* free_bytes_available_to_caller,
                                    std::uint64_t* total_number_of_bytes, std::uint64_t* total_number_of_free_bytes) noexcept;
TL_MSABI int tl_GetDiskFreeSpaceExW(const std::uint16_t* directory_name, std::uint64_t* free_bytes_available_to_caller,
                                    std::uint64_t* total_number_of_bytes, std::uint64_t* total_number_of_free_bytes) noexcept;
TL_MSABI std::uint32_t tl_GetDriveTypeA(const char* root_path_name) noexcept;
TL_MSABI std::uint32_t tl_GetDriveTypeW(const std::uint16_t* root_path_name) noexcept;
TL_MSABI int tl_GetVolumeInformationA(const char* root_path_name, char* volume_name_buffer,
                                      std::uint32_t volume_name_size, std::uint32_t* volume_serial_number,
                                      std::uint32_t* maximum_component_length, std::uint32_t* file_system_flags,
                                      char* file_system_name_buffer, std::uint32_t file_system_name_size) noexcept;
TL_MSABI int tl_GetVolumeInformationW(const std::uint16_t* root_path_name, std::uint16_t* volume_name_buffer,
                                      std::uint32_t volume_name_size, std::uint32_t* volume_serial_number,
                                      std::uint32_t* maximum_component_length, std::uint32_t* file_system_flags,
                                      std::uint16_t* file_system_name_buffer, std::uint32_t file_system_name_size) noexcept;

// Data, Hora e Strings (KERNEL32)
TL_MSABI void tl_GetSystemTime(void* system_time) noexcept;
TL_MSABI void tl_GetLocalTime(void* system_time) noexcept;
TL_MSABI int tl_FileTimeToSystemTime(const void* file_time, void* system_time) noexcept;
TL_MSABI int tl_SystemTimeToFileTime(const void* system_time, void* file_time) noexcept;
TL_MSABI int tl_FlushFileBuffers(const void* handle) noexcept;
TL_MSABI int tl_SetFilePointerEx(const void* handle, std::int64_t distance_to_move,
                                 std::int64_t* new_file_pointer, std::uint32_t move_method) noexcept;
TL_MSABI int tl_GetFileSizeEx(const void* handle, std::int64_t* file_size) noexcept;
TL_MSABI int tl_CompareStringA(std::uint32_t locale, std::uint32_t flags,
                               const char* string1, int count1, const char* string2, int count2) noexcept;
TL_MSABI int tl_CompareStringW(std::uint32_t locale, std::uint32_t flags,
                               const std::uint16_t* string1, int count1, const std::uint16_t* string2, int count2) noexcept;
TL_MSABI std::uint32_t tl_GetUserDefaultLCID() noexcept;
TL_MSABI std::uint32_t tl_GetSystemDefaultLCID() noexcept;
TL_MSABI int tl_GetComputerNameA(char* buffer, std::uint32_t* size) noexcept;
TL_MSABI int tl_GetComputerNameW(std::uint16_t* buffer, std::uint32_t* size) noexcept;

// USER32: Métricas, Hierarquia, Mensagens e Input
TL_MSABI int tl_GetSystemMetrics(int index) noexcept;
TL_MSABI std::intptr_t tl_GetWindowLongPtrA(const void* window, int index) noexcept;
TL_MSABI std::intptr_t tl_GetWindowLongPtrW(const void* window, int index) noexcept;
TL_MSABI std::intptr_t tl_SetWindowLongPtrA(const void* window, int index, std::intptr_t new_long) noexcept;
TL_MSABI std::intptr_t tl_SetWindowLongPtrW(const void* window, int index, std::intptr_t new_long) noexcept;
TL_MSABI void* tl_GetParent(const void* window) noexcept;
TL_MSABI void* tl_SetParent(const void* child_window, const void* new_parent_window) noexcept;
TL_MSABI int tl_IsWindow(const void* window) noexcept;
TL_MSABI int tl_MessageBoxW(const void* window, const std::uint16_t* text,
                            const std::uint16_t* caption, std::uint32_t type) noexcept;
TL_MSABI void* tl_GetDC(const void* window) noexcept;
TL_MSABI int tl_ReleaseDC(const void* window, const void* dc) noexcept;
TL_MSABI void* tl_GetWindowDC(const void* window) noexcept;
TL_MSABI void* tl_SetCursor(const void* cursor) noexcept;
TL_MSABI int tl_ShowCursor(int show) noexcept;
TL_MSABI int tl_SetCursorPos(int x, int y) noexcept;
TL_MSABI std::int16_t tl_GetKeyState(int virt_key) noexcept;
TL_MSABI std::int16_t tl_GetAsyncKeyState(int virt_key) noexcept;

// GDI32: Bitmaps, DCs e Fontes
TL_MSABI int tl_GetDeviceCaps(const void* dc, int index) noexcept;
TL_MSABI void* tl_CreateCompatibleDC(const void* dc) noexcept;
TL_MSABI int tl_DeleteDC(const void* dc) noexcept;
TL_MSABI void* tl_CreateCompatibleBitmap(const void* dc, int width, int height) noexcept;
TL_MSABI int tl_BitBlt(const void* dest_dc, int x, int y, int width, int height,
                       const void* src_dc, int src_x, int src_y, std::uint32_t rop) noexcept;
TL_MSABI void* tl_SelectObject(const void* dc, const void* object) noexcept;
TL_MSABI int tl_SetBkMode(const void* dc, int mode) noexcept;
TL_MSABI void* tl_CreateFontIndirectA(const void* log_font) noexcept;
TL_MSABI void* tl_CreateFontIndirectW(const void* log_font) noexcept;
TL_MSABI void* tl_CreateFontW(int height, int width, int escapement, int orientation, int weight,
                              std::uint32_t italic, std::uint32_t underline, std::uint32_t strikeout,
                              std::uint32_t charset, std::uint32_t output_precision,
                              std::uint32_t clip_precision, std::uint32_t quality,
                              std::uint32_t pitch_and_family, const std::uint16_t* face_name) noexcept;
TL_MSABI std::uint32_t tl_SetDCBrushColor(const void* dc, std::uint32_t color) noexcept;
TL_MSABI std::uint32_t tl_SetDCPenColor(const void* dc, std::uint32_t color) noexcept;

// gdiplus.dll
TL_MSABI int tl_GdiplusStartup(void* token, const void* input, void* output) noexcept;
TL_MSABI void tl_GdiplusShutdown(void* token) noexcept;
TL_MSABI void* tl_GdipAlloc(std::size_t size) noexcept;
TL_MSABI void tl_GdipFree(void* ptr) noexcept;
TL_MSABI int tl_GdipCreateBitmapFromStream(void* stream, void** bitmap) noexcept;
TL_MSABI int tl_GdipCloneImage(void* image, void** clone) noexcept;
TL_MSABI int tl_GdipDisposeImage(void* image) noexcept;
TL_MSABI int tl_GdipCreateHBITMAPFromBitmap(void* bitmap, void** hbm, std::uint32_t background) noexcept;

// UxTheme.dll
TL_MSABI int tl_SetWindowTheme(void* hwnd, const std::uint16_t* subAppName, const std::uint16_t* subIdList) noexcept;

// WINMM.dll
TL_MSABI std::uint32_t tl_timeSetEvent(std::uint32_t delay, std::uint32_t resolution, void* callback,
                                       std::uintptr_t user, std::uint32_t event) noexcept;

// dbghelp.dll
TL_MSABI int tl_SymFromAddr(void* process, std::uint64_t address, std::uint64_t* displacement,
                            void* symbol) noexcept;

// POWRPROF.dll
TL_MSABI std::uint32_t tl_PowerGetActiveScheme(void* UserRootPowerKey, void** ActivePolicyGuid) noexcept;
TL_MSABI std::uint32_t tl_PowerSetActiveScheme(void* UserRootPowerKey, const void* SchemeGuid) noexcept;
TL_MSABI std::uint32_t tl_CallNtPowerInformation(int InformationLevel, void* InputBuffer,
                                                 std::uint32_t InputBufferLength, void* OutputBuffer,
                                                 std::uint32_t OutputBufferLength) noexcept;

// IPHLPAPI.DLL
TL_MSABI std::uint32_t tl_GetAdaptersInfo(void* AdapterInfo, std::uint32_t* OutBufLen) noexcept;
TL_MSABI std::uint32_t tl_GetAdaptersAddresses(std::uint32_t Family, std::uint32_t Flags, void* Reserved,
                                               void* AdapterAddresses, std::uint32_t* SizePointer) noexcept;
TL_MSABI std::uint32_t tl_if_nametoindex(const char* ifname) noexcept;

// ADVAPI32: Criptografia / Random e Registry Wide
TL_MSABI int tl_CryptAcquireContextA(void** prov_handle, const char* container,
                                     const char* provider, std::uint32_t prov_type, std::uint32_t flags) noexcept;
TL_MSABI int tl_CryptAcquireContextW(void** prov_handle, const std::uint16_t* container,
                                     const std::uint16_t* provider, std::uint32_t prov_type, std::uint32_t flags) noexcept;
TL_MSABI int tl_CryptGenRandom(void* prov_handle, std::uint32_t length, std::uint8_t* buffer) noexcept;
TL_MSABI int tl_CryptReleaseContext(void* prov_handle, std::uint32_t flags) noexcept;
// SHELL32.dll (Fase 10+): linha de comando no formato wide.
TL_MSABI std::uint16_t** tl_CommandLineToArgvW(const std::uint16_t* command_line,
                                               int* argument_count) noexcept;
TL_MSABI int tl_ShellNotifyIconA(std::uint32_t message, void* data) noexcept;
TL_MSABI int tl_SHGetKnownFolderPath(const void* rfid, std::uint32_t flags, void* token,
                                     std::uint16_t** path) noexcept;
TL_MSABI int tl_SHGetFolderPathW(void* hwnd, int csidl, void* token, std::uint32_t flags,
                                 std::uint16_t* path) noexcept;
TL_MSABI int tl_SHGetFolderPathAndSubDirW(void* hwnd, int csidl, void* token, std::uint32_t flags,
                                          const std::uint16_t* sub_dir, std::uint16_t* path) noexcept;
TL_MSABI void* tl_ShellExecuteW(void* hwnd, const std::uint16_t* operation,
                                const std::uint16_t* file, const std::uint16_t* parameters,
                                const std::uint16_t* directory, int show) noexcept;
TL_MSABI int tl_ShellExecuteExW(void* exec_info) noexcept;

// KERNEL32: Slim Reader/Writer (SRW) Locks & Condition Variables
TL_MSABI void tl_InitializeSRWLock(void* srw_lock) noexcept;
TL_MSABI void tl_AcquireSRWLockExclusive(void* srw_lock) noexcept;
TL_MSABI void tl_ReleaseSRWLockExclusive(void* srw_lock) noexcept;
TL_MSABI void tl_AcquireSRWLockShared(void* srw_lock) noexcept;
TL_MSABI void tl_ReleaseSRWLockShared(void* srw_lock) noexcept;
TL_MSABI int tl_SleepConditionVariableSRW(void* cond, void* srw_lock,
                                          std::uint32_t milliseconds, std::uint32_t flags) noexcept;
TL_MSABI void tl_WakeConditionVariable(void* cond) noexcept;
TL_MSABI void tl_WakeAllConditionVariable(void* cond) noexcept;

// KERNEL32: Vectored Exception Handling & RaiseException
TL_MSABI void* tl_AddVectoredExceptionHandler(std::uint32_t first, void* handler) noexcept;
TL_MSABI std::uint32_t tl_RemoveVectoredExceptionHandler(void* handle) noexcept;
TL_MSABI void tl_RaiseException(std::uint32_t exception_code, std::uint32_t exception_flags,
                                std::uint32_t number_of_arguments, const std::uint64_t* arguments) noexcept;

// KERNEL32: Arquivos INI (PrivateProfile)
TL_MSABI std::uint32_t tl_GetPrivateProfileStringA(const char* app_name, const char* key_name,
                                                   const char* default_val, char* returned_string,
                                                   std::uint32_t size, const char* file_name) noexcept;
TL_MSABI std::uint32_t tl_GetPrivateProfileStringW(const std::uint16_t* app_name, const std::uint16_t* key_name,
                                                   const std::uint16_t* default_val, std::uint16_t* returned_string,
                                                   std::uint32_t size, const std::uint16_t* file_name) noexcept;
TL_MSABI std::uint32_t tl_GetPrivateProfileIntA(const char* app_name, const char* key_name,
                                                int default_val, const char* file_name) noexcept;
TL_MSABI std::uint32_t tl_GetPrivateProfileIntW(const std::uint16_t* app_name, const std::uint16_t* key_name,
                                                int default_val, const std::uint16_t* file_name) noexcept;
TL_MSABI int tl_WritePrivateProfileStringA(const char* app_name, const char* key_name,
                                           const char* string_val, const char* file_name) noexcept;
TL_MSABI int tl_WritePrivateProfileStringW(const std::uint16_t* app_name, const std::uint16_t* key_name,
                                           const std::uint16_t* string_val, const std::uint16_t* file_name) noexcept;
TL_MSABI std::uint32_t tl_GetPrivateProfileSectionA(const char* app_name, char* returned_string,
                                                    std::uint32_t size, const char* file_name) noexcept;
TL_MSABI std::uint32_t tl_GetPrivateProfileSectionW(const std::uint16_t* app_name, std::uint16_t* returned_string,
                                                    std::uint32_t size, const std::uint16_t* file_name) noexcept;

// KERNEL32: Console Color and Screen Buffer
TL_MSABI int tl_GetConsoleScreenBufferInfo(const void* console_handle, void* buffer_info) noexcept;
TL_MSABI int tl_SetConsoleTextAttribute(const void* console_handle, std::uint16_t attributes) noexcept;

// USER32: Recursos de Strings
TL_MSABI int tl_LoadStringA(void* instance, std::uint32_t id, char* buffer, int buffer_max) noexcept;
TL_MSABI int tl_LoadStringW(void* instance, std::uint32_t id, std::uint16_t* buffer, int buffer_max) noexcept;

// KERNEL32: Thread Pool
TL_MSABI void* tl_CreateThreadpoolWork(void* callback, void* context, void* environment) noexcept;
TL_MSABI void tl_SubmitThreadpoolWork(void* work) noexcept;
TL_MSABI void tl_WaitForThreadpoolWorkCallbacks(void* work, int cancel_pending) noexcept;
TL_MSABI void tl_CloseThreadpoolWork(void* work) noexcept;
TL_MSABI void* tl_CreateThreadpoolTimer(void* callback, void* context, void* environment) noexcept;
TL_MSABI void tl_SetThreadpoolTimer(void* timer, const void* due_time, std::uint32_t period, std::uint32_t window_length) noexcept;
TL_MSABI void tl_WaitForThreadpoolTimerCallbacks(void* timer, int cancel_pending) noexcept;
TL_MSABI void tl_CloseThreadpoolTimer(void* timer) noexcept;

// KERNEL32: Fibras e Corrotinas
TL_MSABI void* tl_ConvertThreadToFiber(void* parameter) noexcept;
TL_MSABI void* tl_ConvertThreadToFiberEx(void* parameter, std::uint32_t flags) noexcept;
TL_MSABI int tl_ConvertFiberToThread() noexcept;
TL_MSABI void* tl_CreateFiber(std::size_t stack_size, void* start_address, void* parameter) noexcept;
TL_MSABI void* tl_CreateFiberEx(std::size_t stack_commit, std::size_t stack_reserve, std::uint32_t flags,
                                void* start_address, void* parameter) noexcept;
TL_MSABI void tl_SwitchToFiber(void* fiber) noexcept;
TL_MSABI void tl_DeleteFiber(void* fiber) noexcept;
TL_MSABI void* tl_GetFiberData() noexcept;

// KERNEL32: Gestão Avançada de Múltiplos Heaps
TL_MSABI void* tl_HeapCreate(std::uint32_t options, std::size_t initial_size, std::size_t maximum_size) noexcept;
TL_MSABI int tl_HeapDestroy(void* heap) noexcept;
TL_MSABI int tl_HeapValidate(void* heap, std::uint32_t flags, const void* memory) noexcept;
TL_MSABI std::size_t tl_HeapSize(void* heap, std::uint32_t flags, const void* memory) noexcept;
TL_MSABI std::size_t tl_HeapCompact(void* heap, std::uint32_t flags) noexcept;

// KERNEL32: Toolhelp
TL_MSABI void* tl_CreateToolhelp32Snapshot(std::uint32_t flags, std::uint32_t process_id) noexcept;
TL_MSABI int tl_Process32FirstW(void* snapshot, void* entry) noexcept;
TL_MSABI int tl_Process32NextW(void* snapshot, void* entry) noexcept;
TL_MSABI void* tl_OpenProcess(std::uint32_t desired_access, int inherit_handle, std::uint32_t process_id) noexcept;

// KERNEL32: Sistema, Processos e Tempo
TL_MSABI void tl_OutputDebugStringA(const char* output_string) noexcept;
TL_MSABI void tl_OutputDebugStringW(const std::uint16_t* output_string) noexcept;
TL_MSABI int tl_SetDllDirectoryW(const std::uint16_t* path_name) noexcept;
TL_MSABI std::size_t tl_VirtualQueryEx(const void* process_handle, const void* address,
                                       void* buffer, std::size_t length) noexcept;
TL_MSABI std::uint32_t tl_GetTimeZoneInformation(void* tz_info) noexcept;
TL_MSABI std::uint32_t tl_GetProcessId(const void* process) noexcept;
TL_MSABI int tl_QueryFullProcessImageNameW(const void* process, std::uint32_t flags,
                                           std::uint16_t* exe_name, std::uint32_t* size) noexcept;
TL_MSABI int tl_FileTimeToLocalFileTime(const void* file_time, void* local_file_time) noexcept;
TL_MSABI std::uint32_t tl_GetLongPathNameW(const std::uint16_t* short_path,
                                           std::uint16_t* long_path, std::uint32_t buffer_length) noexcept;
TL_MSABI std::uint32_t tl_GetShortPathNameW(const std::uint16_t* long_path,
                                            std::uint16_t* short_path, std::uint32_t buffer_length) noexcept;
TL_MSABI int tl_SetThreadPriority(const void* thread_handle, int priority) noexcept;
TL_MSABI int tl_GetProcessAffinityMask(const void* process_handle, std::uintptr_t* process_affinity_mask,
                                       std::uintptr_t* system_affinity_mask) noexcept;

// SHELL32 / SHLWAPI: Utilitários de Caminhos e Shell
TL_MSABI int tl_SHAutoComplete(const void* hwnd_edit, std::uint32_t flags) noexcept;
TL_MSABI int tl_PathRemoveFileSpecW(std::uint16_t* path) noexcept;
TL_MSABI std::uint16_t* tl_PathCombineW(std::uint16_t* dest, const std::uint16_t* dir,
                                        const std::uint16_t* file) noexcept;
TL_MSABI int tl_PathIsRelativeW(const std::uint16_t* path) noexcept;
TL_MSABI int tl_SHFileOperationW(void* file_op) noexcept;

}  // extern "C"

// Define o caminho do módulo convidado antes da execução.
void set_guest_module_path(const char* path) noexcept;
// Define o prefixo ativo da execução atual. O valor é herdado pelos processos
// convidados criados via fork e nunca é obtido implicitamente do diretório do
// launcher.
void set_guest_prefix_path(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path guest_prefix_root();
void set_guest_image_view(const void* image_base, std::size_t image_size,
                          std::uint32_t resource_rva, std::uint32_t resource_size) noexcept;

// Executa um entry point Microsoft x64 e captura ExitProcess sem encerrar o
// processo hospedeiro. O ponteiro deve apontar para código já mapeado como
// executável e com imports resolvidos.
[[nodiscard]] GuestExecutionResult execute_guest_entry(std::uintptr_t entry_point,
                                                       std::uintptr_t stack_top) noexcept;

}  // namespace tradutorlinux
