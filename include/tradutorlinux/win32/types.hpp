#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

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
constexpr Dword kErrorClassDoesNotExist = 141;
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
constexpr Dword kFileAttributeNotContentIndexed = 0x00002000U;
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
constexpr Dword kMemFree = 0x10000U;
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
constexpr Dword kCp1250 = 1250;      // Central European
constexpr Dword kCp1251 = 1251;      // Cyrillic
constexpr Dword kCp1252 = 1252;
constexpr Dword kCp28591 = 28591;    // ISO 8859-1 Latin 1
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
constexpr Dword kMbUseGlyphChars = 0x04U;
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
constexpr Uint kWmRButtonDown = 0x0204;
constexpr Uint kWmRButtonUp = 0x0205;
constexpr Wparam kMkLButton = 0x0001;  // MK_LBUTTON
constexpr Wparam kMkRButton = 0x0002;  // MK_RBUTTON
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
constexpr std::uint32_t kTbAddButtons = 0x0414;
constexpr std::uint32_t kTbAddButtonsW = 0x0444;
constexpr std::uint32_t kTbDeleteButton = 0x0416;
constexpr std::uint32_t kTbButtonCount = 0x0418;
constexpr std::uint32_t kTbSetBitmapSize = 0x041D;
constexpr std::uint32_t kTbButtonStructSize = 0x041E;
constexpr std::uint32_t kTbSetButtonSize = 0x041F;
constexpr std::uint32_t kTbAutoSize = 0x0421;
constexpr std::uint32_t kTbSetImageList = 0x0430;
constexpr std::uint32_t kTbEnableButton = 0x0401;
constexpr std::uint32_t kSbSetTextA = 0x0401;
constexpr std::uint32_t kSbSetParts = 0x0404;
constexpr std::uint32_t kSbSetMinHeight = 0x0408;
constexpr std::uint32_t kSbSimple = 0x0409;
constexpr std::uint32_t kSbSetTextW = 0x040B;
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

}  // namespace tradutorlinux
