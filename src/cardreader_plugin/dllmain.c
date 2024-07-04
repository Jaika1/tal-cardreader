#include "CardIO/cardio.h"
#include "SmartCard/scard.h"
char module[] = "CardReader";

// Reader Thread
static bool READER_RUNNER_INITIALIZED = false;
static HANDLE READER_POLL_THREAD;
static bool READER_POLL_STOP_FLAG;

// SmartCard Specific

// Game specific
static bool waitingForTouch = false;
static bool HasCard = false;

// Misc
bool usingSmartCard = false;
int readCooldown = 200;

typedef void (*callbackTouch)(i32, i32, u8[168], u64);
callbackTouch touchCallback;
u64 touchData;
char AccessID[21] = "00000000000000000001";
static u8 cardData[168] = {0x01, 0x01, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x92, 0x2E, 0x58, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00,
                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x5C, 0x97, 0x44, 0xF0, 0x88, 0x04, 0x00, 0x43, 0x26, 0x2C, 0x33, 0x00, 0x04,
                           0x06, 0x10, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
                           0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x30, 0x30,
                           0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00,
                           0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4E, 0x42, 0x47, 0x49, 0x43, 0x36,
                           0x00, 0x00, 0xFA, 0xE9, 0x69, 0x00, 0xF6, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static card_info_t card_info;

static unsigned int __stdcall reader_poll_thread_proc(void *ctx)
{
    while (!READER_POLL_STOP_FLAG)
    {
        if (HasCard && !usingSmartCard) // A Cardio scan is already in progress, SmartCard doesn't need any extra cooldown.
        {
            Sleep(500);
        }
        else
        {

            //uint8_t UID[8] = {0};

            // update devices
            if (!usingSmartCard) // CardIO
            {
                // I don't have anything to test this with nor enough know-how on what a 'CardIO' actually is :) - Jaika

                // EnterCriticalSection(&CARDIO_HID_CRIT_SECTION);
                // for (size_t device_no = 0; device_no < CARDIO_HID_CONTEXTS_LENGTH; device_no++)
                // {
                //     struct cardio_hid_device *device = &CARDIO_HID_CONTEXTS[device_no];

                //     // get status
                //     cardio_hid_poll_value_t status = cardio_hid_device_poll(device);
                //     if (status == HID_POLL_CARD_READY)
                //     {

                //         // read card
                //         if (cardio_hid_device_read(device) == HID_CARD_NONE)
                //             continue;

                //         // if card not empty
                //         if (*((uint64_t *)&device->u.usage_value[0]) > 0)
                //             for (int i = 0; i < 8; ++i)
                //                 UID[i] = device->u.usage_value[i];
                //     }
                // }
                // LeaveCriticalSection(&CARDIO_HID_CRIT_SECTION);
                // Sleep(readCooldown);
            }
            else // SmartCard
            {
                scard_update(&card_info, &waitingForTouch, &HasCard);
            }

            // if (UID[0] > 0) // If a card was read, format it properly and set HasCard to true so the game can insert it on next frame.
            // {
            //     printWarning("%s (%s): Read card %02X%02X%02X%02X%02X%02X%02X%02X\n", __func__, module, UID[0], UID[1], UID[2], UID[3], UID[4], UID[5], UID[6], UID[7]);

            //     if (waitingForTouch) // Check if game is waiting for a card.
            //     {
            //         // Properly format the AccessID
            //         u64 ReversedAccessID;
            //         for (int i = 0; i < 8; i++)
            //             ReversedAccessID = (ReversedAccessID << 8) | UID[i];
            //         sprintf(AccessID, "%020llu", ReversedAccessID);

            //         waitingForTouch = false; // We don't want to read more cards unless the game asks us to, this is a failsafe to avoid weird behaviour.
            //         HasCard = true;
            //     }
            //     else // Game wasn't checking for cards yet.
            //         printError("%s (%s): Card %02X%02X%02X%02X%02X%02X%02X%02X was rejected, still waiting for WaitTouch()\n", __func__, module, UID[0], UID[1], UID[2], UID[3], UID[4], UID[5], UID[6], UID[7]);
            // }
        }
    }

    return 0;
}

bool reader_runner_start()
{
    // initialize
    if (!READER_RUNNER_INITIALIZED)
    {
        READER_RUNNER_INITIALIZED = true;

        if (!usingSmartCard)
        { // CardIO INIT
            printWarning("%s (%s): Initializing CardIO\n", __func__, module);

            // Initialize
            if (!cardio_hid_init())
            {
                printError("%s (%s): Couldn't init CardIO\n", __func__, module);
                return FALSE;
            }

            // Scan HID devices
            if (!cardio_hid_scan())
            {
                printError("%s (%s): Couldn't scan for CardIO devices\n", __func__, module);
                return FALSE;
            }
        }
        else
        { // SmartCard INIT
            printWarning("%s (%s): Initializing SmartCard\n", __func__, module);

            if (!scard_init())
            {
                //  Initialize
                printError("%s (%s): Couldn't init SmartCard\n", __func__, module);
                return FALSE;
            }
        }

        return TRUE; // Init success
    }

    return TRUE;
}

void reader_runner_stop()
{
    // shutdown HID
    if (!usingSmartCard)
    {
        cardio_hid_close();
    }

    // set initialized to false
    READER_RUNNER_INITIALIZED = false;
}

void Init()
{
    printWarning("%s (%s): Starting HID Service...\n", __func__, module);

    // Read config
    toml_table_t *config = openConfig(configPath("plugins/cardreader.toml"));
    if (config)
    {
        usingSmartCard = readConfigBool(config, "using_smartcard", false);
        readCooldown = readConfigInt(config, "read_cooldown", usingSmartCard ? 500 : 50);
        toml_free(config);
    }
    else
    {
        printError("%s (%s): Config file not found\n", __func__, module);
        return;
    }

    printInfo("%s (%s): Using %s as reader type\n", __func__, module, usingSmartCard ? "Smart Card" : "cardIO");
    printInfo("%s (%s): Card read cooldown set to %d ms\n", __func__, module, readCooldown);

    printInfo("%s (%s): Init card info structure\n", __func__, module);
    
    memset(&card_info, 0, sizeof(card_info)); // We init the card_data structure

    //  Find and initialize reader(s)
    if (!reader_runner_start())
        return;

    printWarning("%s (%s): Starting reader thread.\n", __func__, module);

    // Start reader thread
    READER_POLL_STOP_FLAG = false;
    READER_POLL_THREAD = (HANDLE)_beginthreadex(
        NULL,
        0,
        reader_poll_thread_proc,
        NULL,
        0,
        NULL);
}

void Update()
{
#pragma region Legacy
    //     // Generating a ChipID Derived from the AccessID
    //     char ChipID[33] = "000000000000";
    //     strcat(ChipID, AccessID);

    //     // Insert card in game
    //     printInfo("%s (%s): Inserting Card %s, with ChipID %s\n", __func__, module, AccessID, ChipID);

    //     memcpy(cardData + 0x2C, ChipID, 33);
    //     memcpy(cardData + 0x50, AccessID, 21);
    //     touchCallback(0, 0, cardData, touchData);
#pragma endregion

    if (HasCard)
    {   
        printWarning("%s (%s): HasCard True!\n", __func__, module);
        printWarning("%s (%s): Card scan detected! Type: %02X\n", __func__, module, card_info.card_type);

        if (card_info.card_type == Mifare)
        {
            // u64 numAccessID;
            // for (int i = 0; i < 10; i++)
            //     numAccessID = (numAccessID >> 8) | ((uint64_t)card_info.card_id[i] << 56);
            sprintf(AccessID, "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
            card_info.card_id[0],
            card_info.card_id[1],
            card_info.card_id[2],
            card_info.card_id[3],
            card_info.card_id[4],
            card_info.card_id[5],
            card_info.card_id[6],
            card_info.card_id[7],
            card_info.card_id[8],
            card_info.card_id[9]);
        }
        else
        {
            u64 ReversedAccessID;
            for (int i = 0; i < 8; i++)
                ReversedAccessID = (ReversedAccessID << 8) | card_info.card_id[i];
            sprintf(AccessID, "%020llu", ReversedAccessID);
        }

        char ChipID[33] = "000000000000";
        strcat(ChipID, AccessID);

        // Insert card in game
        printInfo("%s (%s): Inserting Card %s, with ChipID %s\n", __func__, module, AccessID, ChipID);

        memcpy(cardData + 0x2C, ChipID, 33);
        memcpy(cardData + 0x50, AccessID, 21);
        touchCallback(0, 0, cardData, touchData);

        memset(&card_info, 0, sizeof(card_info)); // Reset card_data structure
        HasCard = false;
    }
}

void WaitTouch(i32 (*callback)(i32, i32, u8[168], u64), u64 data) // This is called everytime the game expects a card to be read
{
    printInfo("%s (%s): Waiting for touch\n", __func__, module);
    waitingForTouch = true;
    touchCallback = callback;
    touchData = data;
}

void Exit()
{
    reader_runner_stop();
    READER_POLL_STOP_FLAG = true;
    WaitForSingleObject(READER_POLL_THREAD, INFINITE);
    CloseHandle(READER_POLL_THREAD);
    READER_POLL_THREAD = NULL;
    READER_POLL_STOP_FLAG = false;
}
