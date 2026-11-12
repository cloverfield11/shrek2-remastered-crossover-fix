#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

#define GAMEINPUT_E_READING_NOT_FOUND ((HRESULT)0x838A0005L)
#define GameInputKindGamepad 0x00040000u
#define GameInputDeviceConnected 0x00000001u

#ifndef XINPUT_GAMEPAD_DPAD_UP
#define XINPUT_GAMEPAD_DPAD_UP        0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN      0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT      0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT     0x0008
#define XINPUT_GAMEPAD_START          0x0010
#define XINPUT_GAMEPAD_BACK           0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB     0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB    0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER  0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200
#define XINPUT_GAMEPAD_A              0x1000
#define XINPUT_GAMEPAD_B              0x2000
#define XINPUT_GAMEPAD_X              0x4000
#define XINPUT_GAMEPAD_Y              0x8000
#endif

static void my_memset(void *dst, int val, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)val;
}

static void my_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

static int my_memeq(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return 0;
    }
    return 1;
}

typedef struct _MY_XINPUT_GAMEPAD {
    WORD  wButtons;
    BYTE  bLeftTrigger;
    BYTE  bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} MY_XINPUT_GAMEPAD;

typedef struct _MY_XINPUT_STATE {
    DWORD             dwPacketNumber;
    MY_XINPUT_GAMEPAD Gamepad;
} MY_XINPUT_STATE;

typedef struct _MY_XINPUT_VIBRATION {
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
} MY_XINPUT_VIBRATION;

typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD dwUserIndex, MY_XINPUT_STATE *pState);
typedef DWORD (WINAPI *PFN_XInputSetState)(DWORD dwUserIndex, MY_XINPUT_VIBRATION *pVibration);

static PFN_XInputGetState g_pfnXInputGetState = NULL;
static PFN_XInputSetState g_pfnXInputSetState = NULL;
static HMODULE g_hXInput = NULL;

static void InitXInput(void) {
    if (g_pfnXInputGetState) return;
    const char *dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    for (int i = 0; i < 3; i++) {
        HMODULE h = LoadLibraryA(dlls[i]);
        if (h) {
            PFN_XInputGetState getFn = (PFN_XInputGetState)(void *)GetProcAddress(h, "XInputGetState");
            PFN_XInputSetState setFn = (PFN_XInputSetState)(void *)GetProcAddress(h, "XInputSetState");
            if (getFn) {
                g_hXInput = h;
                g_pfnXInputGetState = getFn;
                g_pfnXInputSetState = setFn;
                return;
            }
            FreeLibrary(h);
        }
    }
}

typedef struct GameInputGamepadState {
    uint32_t buttons;
    float leftTrigger;
    float rightTrigger;
    float leftThumbstickX;
    float leftThumbstickY;
    float rightThumbstickX;
    float rightThumbstickY;
} GameInputGamepadState;

typedef struct GameInputRumbleParams {
    float lowFrequency;
    float highFrequency;
    float leftTrigger;
    float rightTrigger;
} GameInputRumbleParams;

typedef struct GameInputDeviceInfo {
    uint16_t vendorId;              /* 0x00 */
    uint16_t productId;             /* 0x02 */
    uint16_t revisionNumber;        /* 0x04 */
    uint16_t usagePage;             /* 0x06 */
    uint8_t  deviceId[32];          /* 0x08 */
    uint8_t  deviceRootId[32];      /* 0x28 */
    uint32_t deviceFamily;          /* 0x48 */
    uint32_t supportedInput;        /* 0x4c */
    uint32_t supportedRumbleMotors; /* 0x50 */
    uint8_t  reserved[428];         /* pad to 512 bytes */
} GameInputDeviceInfo;

typedef void (CALLBACK *GameInputDeviceCallback)(
    uint64_t callbackToken,
    void *context,
    void *device,
    uint64_t timestamp,
    uint32_t currentStatus,
    uint32_t previousStatus);

typedef struct MyReading {
    void **lpVtbl;
    volatile LONG refCount;
    GameInputGamepadState state;
} MyReading;

typedef struct MyDevice {
    void **lpVtbl;
    volatile LONG refCount;
    DWORD slot;
    GameInputDeviceInfo info;
} MyDevice;

typedef struct MyGameInput {
    void **lpVtbl;
    volatile LONG refCount;
} MyGameInput;

static HRESULT STDMETHODCALLTYPE Dummy_Stub(void *This) {
    (void)This;
    return E_NOTIMPL;
}

/* ---- IGameInputReading ---- */
static HRESULT STDMETHODCALLTYPE Reading_QueryInterface(void *This, REFIID riid, void **ppv) {
    (void)riid;
    if (!ppv) return E_POINTER;
    *ppv = This;
    InterlockedIncrement(&((MyReading *)This)->refCount);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE Reading_AddRef(void *This) {
    return (ULONG)InterlockedIncrement(&((MyReading *)This)->refCount);
}

static ULONG STDMETHODCALLTYPE Reading_Release(void *This) {
    MyReading *r = (MyReading *)This;
    LONG c = InterlockedDecrement(&r->refCount);
    if (c == 0) {
        HeapFree(GetProcessHeap(), 0, r);
    }
    return (ULONG)c;
}

static uint8_t STDMETHODCALLTYPE Reading_GetGamepadState(void *This, GameInputGamepadState *state) {
    if (!state) return 0;
    my_memcpy(state, &((MyReading *)This)->state, sizeof(GameInputGamepadState));
    return 1;
}

static void *g_readingVtbl[24] = {
    (void *)Reading_QueryInterface,  /* 0: 0x00 */
    (void *)Reading_AddRef,          /* 1: 0x08 */
    (void *)Reading_Release,         /* 2: 0x10 */
    (void *)Dummy_Stub,              /* 3: 0x18 */
    (void *)Dummy_Stub,              /* 4: 0x20 */
    (void *)Dummy_Stub,              /* 5: 0x28 */
    (void *)Dummy_Stub,              /* 6: 0x30 */
    (void *)Dummy_Stub,              /* 7: 0x38 */
    (void *)Dummy_Stub,              /* 8: 0x40 */
    (void *)Dummy_Stub,              /* 9: 0x48 */
    (void *)Dummy_Stub,              /* 10: 0x50 */
    (void *)Dummy_Stub,              /* 11: 0x58 */
    (void *)Dummy_Stub,              /* 12: 0x60 */
    (void *)Dummy_Stub,              /* 13: 0x68 */
    (void *)Dummy_Stub,              /* 14: 0x70 */
    (void *)Dummy_Stub,              /* 15: 0x78 */
    (void *)Dummy_Stub,              /* 16: 0x80 */
    (void *)Reading_GetGamepadState, /* 17: 0x88 */
    (void *)Dummy_Stub,
    (void *)Dummy_Stub,
    (void *)Dummy_Stub,
    (void *)Dummy_Stub,
    (void *)Dummy_Stub,
    (void *)Dummy_Stub
};

/* ---- IGameInputDevice ---- */
static HRESULT STDMETHODCALLTYPE Device_QueryInterface(void *This, REFIID riid, void **ppv) {
    (void)riid;
    if (!ppv) return E_POINTER;
    *ppv = This;
    InterlockedIncrement(&((MyDevice *)This)->refCount);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE Device_AddRef(void *This) {
    return (ULONG)InterlockedIncrement(&((MyDevice *)This)->refCount);
}

static ULONG STDMETHODCALLTYPE Device_Release(void *This) {
    return (ULONG)InterlockedDecrement(&((MyDevice *)This)->refCount);
}

static HRESULT STDMETHODCALLTYPE Device_GetDeviceInfo(void *This, const GameInputDeviceInfo **info) {
    if (!info) return E_POINTER;
    *info = &((MyDevice *)This)->info;
    return S_OK;
}

static uint32_t STDMETHODCALLTYPE Device_GetDeviceStatus(void *This) {
    MyDevice *dev = (MyDevice *)This;
    if (!g_pfnXInputGetState) return 0;
    MY_XINPUT_STATE xs;
    if (g_pfnXInputGetState(dev->slot, &xs) == ERROR_SUCCESS) {
        return GameInputDeviceConnected;
    }
    return 0;
}

static void STDMETHODCALLTYPE Device_SetRumbleState(void *This, const GameInputRumbleParams *params) {
    MyDevice *dev = (MyDevice *)This;
    if (!g_pfnXInputSetState) return;
    MY_XINPUT_VIBRATION vib;
    if (!params) {
        vib.wLeftMotorSpeed = 0;
        vib.wRightMotorSpeed = 0;
    } else {
        float l = params->lowFrequency;
        float h = params->highFrequency;
        if (l < 0.0f) l = 0.0f;
        if (l > 1.0f) l = 1.0f;
        if (h < 0.0f) h = 0.0f;
        if (h > 1.0f) h = 1.0f;
        vib.wLeftMotorSpeed = (WORD)(l * 65535.0f);
        vib.wRightMotorSpeed = (WORD)(h * 65535.0f);
    }
    g_pfnXInputSetState(dev->slot, &vib);
}

static void *g_deviceVtbl[16] = {
    (void *)Device_QueryInterface,  /* 0: 0x00 */
    (void *)Device_AddRef,          /* 1: 0x08 */
    (void *)Device_Release,         /* 2: 0x10 */
    (void *)Device_GetDeviceInfo,   /* 3: 0x18 */
    (void *)Device_GetDeviceStatus, /* 4: 0x20 */
    (void *)Dummy_Stub,             /* 5: 0x28 */
    (void *)Dummy_Stub,             /* 6: 0x30 */
    (void *)Dummy_Stub,             /* 7: 0x38 */
    (void *)Device_SetRumbleState,  /* 8: 0x40 */
    (void *)Dummy_Stub,
    (void *)Dummy_Stub,
    (void *)Dummy_Stub,
    (void *)Dummy_Stub,
    (void *)Dummy_Stub,
    (void *)Dummy_Stub,
    (void *)Dummy_Stub
};

static MyDevice g_devices[4];

static void InitDevices(void) {
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;
    for (DWORD i = 0; i < 4; i++) {
        my_memset(&g_devices[i], 0, sizeof(MyDevice));
        g_devices[i].lpVtbl = g_deviceVtbl;
        g_devices[i].refCount = 1;
        g_devices[i].slot = i;
        g_devices[i].info.vendorId = 0x045E;   /* Microsoft Xbox controller */
        g_devices[i].info.productId = 0x02EA;
        g_devices[i].info.revisionNumber = 1;
        g_devices[i].info.deviceId[0] = (uint8_t)(0xA0 + i);
        g_devices[i].info.deviceId[1] = 0x58;
        g_devices[i].info.deviceId[2] = 0x49;
        g_devices[i].info.deviceId[3] = (uint8_t)('0' + i);
        my_memcpy(g_devices[i].info.deviceRootId, g_devices[i].info.deviceId, 32);
        g_devices[i].info.deviceFamily = 3;
        g_devices[i].info.supportedInput = GameInputKindGamepad;
        g_devices[i].info.supportedRumbleMotors = 3;
    }
}

static float NormStick(SHORT val) {
    if (val < 0) return (float)val / 32768.0f;
    return (float)val / 32767.0f;
}

static uint32_t TranslateButtons(WORD w) {
    uint32_t b = 0;
    if (w & XINPUT_GAMEPAD_START)          b |= 0x0001u;
    if (w & XINPUT_GAMEPAD_BACK)           b |= 0x0002u;
    if (w & XINPUT_GAMEPAD_A)              b |= 0x0004u;
    if (w & XINPUT_GAMEPAD_B)              b |= 0x0008u;
    if (w & XINPUT_GAMEPAD_X)              b |= 0x0010u;
    if (w & XINPUT_GAMEPAD_Y)              b |= 0x0020u;
    if (w & XINPUT_GAMEPAD_DPAD_UP)        b |= 0x0040u;
    if (w & XINPUT_GAMEPAD_DPAD_DOWN)      b |= 0x0080u;
    if (w & XINPUT_GAMEPAD_DPAD_LEFT)      b |= 0x0100u;
    if (w & XINPUT_GAMEPAD_DPAD_RIGHT)     b |= 0x0200u;
    if (w & XINPUT_GAMEPAD_LEFT_SHOULDER)  b |= 0x0400u;
    if (w & XINPUT_GAMEPAD_RIGHT_SHOULDER) b |= 0x0800u;
    if (w & XINPUT_GAMEPAD_LEFT_THUMB)     b |= 0x1000u;
    if (w & XINPUT_GAMEPAD_RIGHT_THUMB)    b |= 0x2000u;
    return b;
}

/* ---- IGameInput ---- */
/* {40ffb7e4-6150-407a-b439-132badc08d2d} */
static const GUID MY_IID_IGameInput = {
    0x40ffb7e4, 0x6150, 0x407a,
    { 0xb4, 0x39, 0x13, 0x2b, 0xad, 0xc0, 0x8d, 0x2d }
};
static const GUID MY_IID_IUnknown = {
    0x00000000, 0x0000, 0x0000,
    { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }
};

static HRESULT STDMETHODCALLTYPE GI_QueryInterface(void *This, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    if (riid && (my_memeq(riid, &MY_IID_IGameInput, sizeof(GUID)) ||
                 my_memeq(riid, &MY_IID_IUnknown, sizeof(GUID)))) {
        *ppv = This;
        InterlockedIncrement(&((MyGameInput *)This)->refCount);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE GI_AddRef(void *This) {
    return (ULONG)InterlockedIncrement(&((MyGameInput *)This)->refCount);
}

static ULONG STDMETHODCALLTYPE GI_Release(void *This) {
    return (ULONG)InterlockedDecrement(&((MyGameInput *)This)->refCount);
}

static HRESULT STDMETHODCALLTYPE GI_GetCurrentReading(
    void *This, uint32_t inputKind, void *device, void **reading)
{
    (void)This;
    (void)inputKind;
    if (!reading) return E_POINTER;
    *reading = NULL;
    if (!g_pfnXInputGetState) return GAMEINPUT_E_READING_NOT_FOUND;

    DWORD slot = 0xFFFFFFFFu;
    if (device) {
        for (DWORD i = 0; i < 4; i++) {
            if (device == &g_devices[i]) {
                slot = i;
                break;
            }
        }
    } else {
        for (DWORD i = 0; i < 4; i++) {
            MY_XINPUT_STATE tmp;
            if (g_pfnXInputGetState(i, &tmp) == ERROR_SUCCESS) {
                slot = i;
                break;
            }
        }
    }
    if (slot >= 4) return GAMEINPUT_E_READING_NOT_FOUND;

    MY_XINPUT_STATE xs;
    if (g_pfnXInputGetState(slot, &xs) != ERROR_SUCCESS) {
        return GAMEINPUT_E_READING_NOT_FOUND;
    }

    MyReading *r = (MyReading *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MyReading));
    if (!r) return E_OUTOFMEMORY;
    r->lpVtbl = g_readingVtbl;
    r->refCount = 1;
    r->state.buttons = TranslateButtons(xs.Gamepad.wButtons);
    r->state.leftTrigger = (float)xs.Gamepad.bLeftTrigger / 255.0f;
    r->state.rightTrigger = (float)xs.Gamepad.bRightTrigger / 255.0f;
    r->state.leftThumbstickX = NormStick(xs.Gamepad.sThumbLX);
    r->state.leftThumbstickY = NormStick(xs.Gamepad.sThumbLY);
    r->state.rightThumbstickX = NormStick(xs.Gamepad.sThumbRX);
    r->state.rightThumbstickY = NormStick(xs.Gamepad.sThumbRY);
    *reading = r;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE GI_GetNextReading(
    void *This, void *referenceReading, uint32_t inputKind, void *device, void **reading)
{
    (void)This;
    (void)referenceReading;
    (void)inputKind;
    (void)device;
    if (reading) *reading = NULL;
    return GAMEINPUT_E_READING_NOT_FOUND;
}

static HRESULT STDMETHODCALLTYPE GI_RegisterDeviceCallback(
    void *This,
    void *device,
    uint32_t inputKind,
    uint32_t statusFilter,
    uint32_t enumerationKind,
    void *context,
    GameInputDeviceCallback callbackFunc,
    uint64_t *callbackToken)
{
    (void)This;
    (void)device;
    (void)inputKind;
    (void)statusFilter;
    (void)enumerationKind;
    uint64_t token = 1;
    if (callbackToken) *callbackToken = token;
    if (callbackFunc) {
        for (DWORD i = 0; i < 4; i++) {
            callbackFunc(token, context, &g_devices[i], 0, GameInputDeviceConnected, 0);
        }
    }
    return S_OK;
}

static void STDMETHODCALLTYPE GI_UnregisterCallback(void *This, uint64_t callbackToken) {
    (void)This;
    (void)callbackToken;
}

static void STDMETHODCALLTYPE GI_SetFocusPolicy(void *This, uint32_t policy) {
    (void)This;
    (void)policy;
}

static void *g_gameInputVtbl[20] = {
    (void *)GI_QueryInterface,         /* 0: 0x00 */
    (void *)GI_AddRef,                 /* 1: 0x08 */
    (void *)GI_Release,                /* 2: 0x10 */
    (void *)Dummy_Stub,                /* 3: 0x18 */
    (void *)GI_GetCurrentReading,      /* 4: 0x20 */
    (void *)GI_GetNextReading,         /* 5: 0x28 */
    (void *)Dummy_Stub,                /* 6: 0x30 */
    (void *)Dummy_Stub,                /* 7: 0x38 */
    (void *)GI_RegisterDeviceCallback, /* 8: 0x40 */
    (void *)Dummy_Stub,                /* 9: 0x48 */
    (void *)Dummy_Stub,                /* 10: 0x50 */
    (void *)Dummy_Stub,                /* 11: 0x58 */
    (void *)GI_UnregisterCallback,     /* 12: 0x60 */
    (void *)Dummy_Stub,                /* 13: 0x68 */
    (void *)Dummy_Stub,                /* 14: 0x70 */
    (void *)Dummy_Stub,                /* 15: 0x78 */
    (void *)GI_SetFocusPolicy,         /* 16: 0x80 */
    (void *)Dummy_Stub,
    (void *)Dummy_Stub,
    (void *)Dummy_Stub
};

static MyGameInput g_gameInput = { g_gameInputVtbl, 1 };

HRESULT WINAPI GameInputCreate(void **gameInput) {
    if (!gameInput) return E_POINTER;
    InitXInput();
    InitDevices();
    InterlockedIncrement(&g_gameInput.refCount);
    *gameInput = &g_gameInput;
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)lpvReserved;
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}
