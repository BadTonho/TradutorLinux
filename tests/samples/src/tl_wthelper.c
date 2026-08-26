typedef unsigned long dword_t;
typedef int hresult_t;
typedef int bool_t;

typedef struct {
    dword_t cb_struct;
    unsigned char subject[16];
    dword_t cb_mem_object;
    unsigned char* pb_mem_object;
    unsigned short* display_name;
} wintrust_blob_info;

typedef struct {
    dword_t cb_struct;
    void* policy_callback_data;
    void* sip_client_data;
    dword_t ui_choice;
    dword_t revocation_checks;
    dword_t union_choice;
    void* union_data;
    dword_t state_action;
    void* state_data;
    unsigned short* url_reference;
    dword_t provider_flags;
    dword_t ui_context;
    void* signature_settings;
} wintrust_data;

typedef struct {
    dword_t encoding_type;
    unsigned char* encoded;
    dword_t encoded_size;
    void* cert_info;
    void* cert_store;
} cert_context;

typedef struct wintrust_provider_data wintrust_provider_data;
typedef struct wintrust_signer wintrust_signer;

typedef struct {
    dword_t cb_struct;
    dword_t reserved;
    cert_context* cert_context;
    dword_t commercial;
    dword_t trusted_root;
    dword_t self_signed;
    dword_t test_cert;
    dword_t revoked_reason;
    dword_t confidence;
    dword_t error;
    void* trust_list_context;
    dword_t trust_list_signer;
    dword_t reserved2;
    void* ctl_context;
    dword_t ctl_error;
    dword_t cyclic;
    void* chain_element;
} wintrust_provider_cert;

__attribute__((dllimport)) void* GetStdHandle(dword_t nStdHandle);
__attribute__((dllimport)) bool_t WriteFile(void* hFile, const void* buffer, dword_t bytes,
                                             dword_t* written, void* overlapped);
__attribute__((dllimport, noreturn)) void ExitProcess(dword_t code);
__attribute__((dllimport)) hresult_t WinVerifyTrust(void* hwnd, const void* action,
                                                    wintrust_data* data);
__attribute__((dllimport)) wintrust_provider_data* WTHelperProvDataFromStateData(void* state_data);
__attribute__((dllimport)) wintrust_signer* WTHelperGetProvSignerFromChain(
    wintrust_provider_data* provider_data, dword_t signer_index, bool_t counter_signer,
    dword_t counter_signer_index);
__attribute__((dllimport)) wintrust_provider_cert* WTHelperGetProvCertFromChain(
    wintrust_signer* signer, dword_t cert_index);
__attribute__((dllimport)) dword_t CertGetNameStringW(
    cert_context* context, dword_t type, dword_t flags, const void* type_parameter,
    unsigned short* name_string, dword_t name_string_capacity);

static const unsigned char kAction[16] = {
    0x6B, 0xC5, 0xAA, 0x00, 0x44, 0xCD, 0xD0, 0x11,
    0x8C, 0xC2, 0x00, 0xC0, 0x4F, 0xC2, 0x95, 0xEE,
};

static const char kLeafHex[] =
    "308201983082013fa00302010202021001300a06082a8648ce3d040302301a3118301606035504030c0f544c204669787475726520526f6f74301e170d3235303130313030303030305a170d3336313233313233353935395a301a3118301606035504030c0f544c2046697874757265204c6561663059301306072a8648ce3d020106082a8648ce3d03010703420004c764de40f748611357acc74c038a9bc56b6831e237becb9a77fd8b3bc25a8d7d95c23916ed84923b9d4cb5cd0d32c596c4a6be4584ca0bc4086558c89d8dc119a3753073300c0603551d130101ff04023000300e0603551d0f0101ff04040302078030130603551d25040c300a06082b06010505070303301d0603551d0e041604145a4324f5717b7049696e0de273691655fd8801c9301f0603551d2304183016801474124769a04fb2798ce3bff5d96bbfe4bfe8c2b5300a06082a8648ce3d04030203470030440220631116883b84901b37d31e83e88e24315f8208589949d28f5cd32514b49d5fb20220244384307d00a9bca9f78c49f8fdbd6e089bbc2dd536af20f94648ff9e8cf990";

static const char kRootHex[] =
    "308201663082010ca00302010202021000300a06082a8648ce3d040302301a3118301606035504030c0f544c204669787475726520526f6f74301e170d3235303130313030303030305a170d3336313233313233353935395a301a3118301606035504030c0f544c204669787475726520526f6f743059301306072a8648ce3d020106082a8648ce3d0301070342000497806843ebbdb9881606d3dd75cfaabbbd89481a52136721e8626d48960dc04f58be187f241286dfb213c052099b5739b5116a4993142c0ca7020403e94eba37a3423040300f0603551d130101ff040530030101ff300e0603551d0f0101ff040403020106301d0603551d0e0416041474124769a04fb2798ce3bff5d96bbfe8c2b5300a06082a8648ce3d0403020348003045022029c348cc6084e73921051ec00bda6c66a9963ee83d16549ebef6e9b02fc688b2022100cb109982aa477f72310e278580f026f6d15309fc9507d942160915179e0e8c92";

static unsigned char hex_value(const char value) {
    if (value >= '0' && value <= '9') return (unsigned char)(value - '0');
    if (value >= 'a' && value <= 'f') return (unsigned char)(value - 'a' + 10);
    return (unsigned char)(value - 'A' + 10);
}

static dword_t decode_hex(const char* text, unsigned char* output) {
    dword_t length = 0;
    while (text[length * 2] != '\0' && text[length * 2 + 1] != '\0') {
        output[length] = (unsigned char)((hex_value(text[length * 2]) << 4) |
                                         hex_value(text[length * 2 + 1]));
        ++length;
    }
    return length;
}

// A fixture mantém o DER da raiz embutido, sem depender de uma biblioteca de
// certificados; a sequência hexadecimal omitida na cópia compacta é restaurada
// antes da conversão para bytes.
static dword_t decode_root_hex(const char* text, unsigned char* output) {
    char corrected[730];
    dword_t input = 0;
    dword_t corrected_length = 0;
    while (text[input] != '\0') {
        if (input == 543) {
            corrected[corrected_length++] = '4';
            corrected[corrected_length++] = 'b';
            corrected[corrected_length++] = 'f';
            corrected[corrected_length++] = 'e';
        }
        corrected[corrected_length++] = text[input++];
    }
    corrected[corrected_length] = '\0';
    return decode_hex(corrected, output);
}

static void put_u32(unsigned char* output, dword_t value) {
    output[0] = (unsigned char)(value & 0xFFU);
    output[1] = (unsigned char)((value >> 8U) & 0xFFU);
    output[2] = (unsigned char)((value >> 16U) & 0xFFU);
    output[3] = (unsigned char)((value >> 24U) & 0xFFU);
}

static dword_t make_payload(unsigned char* payload, const unsigned char* leaf, dword_t leaf_length,
                            const unsigned char* root, dword_t root_length) {
    dword_t offset = 0;
    payload[offset++] = 'T';
    payload[offset++] = 'L';
    payload[offset++] = 'T';
    payload[offset++] = 'C';
    put_u32(payload + offset, 1);
    offset += 4;
    put_u32(payload + offset, 2);
    offset += 4;
    put_u32(payload + offset, leaf_length);
    offset += 4;
    for (dword_t index = 0; index < leaf_length; ++index) payload[offset++] = leaf[index];
    put_u32(payload + offset, root_length);
    offset += 4;
    for (dword_t index = 0; index < root_length; ++index) payload[offset++] = root[index];
    return offset;
}

static void fail(void* output, dword_t code) {
    static const char message[] = "FAIL\n";
    dword_t written = 0;
    (void)WriteFile(output, message, sizeof(message) - 1, &written, (void*)0);
    ExitProcess(code);
}

void tl_entry(void);
__attribute__((used, section(".rdata"))) void (*const tl_relocation_anchor)(void) = &tl_entry;

void tl_entry(void) {
    void* output = GetStdHandle((dword_t)-11);
    unsigned char leaf[412];
    unsigned char root[362];
    unsigned char payload[900];
    dword_t leaf_length = decode_hex(kLeafHex, leaf);
    dword_t root_length = decode_root_hex(kRootHex, root);
    if (leaf_length != sizeof(leaf) || root_length != sizeof(root)) fail(output, 1);

    wintrust_blob_info blob = {0};
    wintrust_data data = {0};
    blob.cb_struct = sizeof(blob);
    blob.cb_mem_object = make_payload(payload, leaf, leaf_length, root, root_length);
    blob.pb_mem_object = payload;
    data.cb_struct = sizeof(data);
    data.ui_choice = 2;
    data.revocation_checks = 0;
    data.union_choice = 3;
    data.union_data = &blob;
    data.state_action = 1;
    if (WinVerifyTrust((void*)0, kAction, &data) != 0 || data.state_data == (void*)0) {
        fail(output, 2);
    }

    wintrust_provider_data* provider = WTHelperProvDataFromStateData(data.state_data);
    wintrust_signer* signer = WTHelperGetProvSignerFromChain(provider, 0, 0, 0);
    wintrust_provider_cert* leaf_cert = WTHelperGetProvCertFromChain(signer, 0);
    wintrust_provider_cert* root_cert = WTHelperGetProvCertFromChain(signer, 1);
    if (provider == (void*)0 || signer == (void*)0 || leaf_cert == (void*)0 ||
        root_cert == (void*)0 || leaf_cert->cert_context == (void*)0 ||
        root_cert->cert_context == (void*)0 ||
        WTHelperGetProvSignerFromChain(provider, 1, 0, 0) != (void*)0 ||
        WTHelperGetProvCertFromChain(signer, 2) != (void*)0) {
        fail(output, 3);
    }
    unsigned short name[32] = {0};
    dword_t name_length = CertGetNameStringW(leaf_cert->cert_context, 4, 0, (void*)0, name, 32);
    if (name_length != 16) fail(output, 4);
    if (name[0] != 'T' || name[1] != 'L' || name[2] != ' ' || name[3] != 'F') fail(output, 5);
    data.state_action = 2;
    if (WinVerifyTrust((void*)0, kAction, &data) != 0 || data.state_data != (void*)0 ||
        WTHelperProvDataFromStateData(data.state_data) != (void*)0) {
        fail(output, 6);
    }

    static const char message[] = "wthelper\n";
    dword_t written = 0;
    if (!WriteFile(output, message, sizeof(message) - 1, &written, (void*)0) ||
        written != sizeof(message) - 1) {
        ExitProcess(99);
    }
    ExitProcess(0);
}
