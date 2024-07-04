/**
 * MIT-License
 * Copyright (c) 2018 by nolm <nolan@nolm.name>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Modified version.
 */

#include "scard.h"
#include <windows.h>
#include <winscard.h>
// #include <tchar.h>
// #include <thread>

extern char module[];

#define MAX_APDU_SIZE 255
extern int readCooldown;
// set to detect all cards, reduce polling rate to 500ms.
// based off acr122u reader, see page 26 in api document.
// https://www.acs.com.hk/en/download-manual/419/API-ACR122U-2.04.pdf

#define PICC_OPERATING_PARAMS 0xDFu
BYTE PICC_OPERATING_PARAM_CMD[5] = {0xFFu, 0x00u, 0x51u, PICC_OPERATING_PARAMS, 0x00u};

// return bytes from device
#define PICC_SUCCESS 0x90u
#define PICC_ERROR 0x63u

static const BYTE UID_CMD[5] = {0xFFu, 0xCAu, 0x00u, 0x00u, 0x00u};

enum scard_atr_protocol
{
    SCARD_ATR_PROTOCOL_ISO14443_PART3 = 0x03,
    SCARD_ATR_PROTOCOL_ISO15693_PART3 = 0x0B,
    SCARD_ATR_PROTOCOL_FELICA_212K = 0x11,
    SCARD_ATR_PROTOCOL_FELICA_424K = 0x12,
};

// winscard_config_t WINSCARD_CONFIG;
SCARDCONTEXT hContext = 0;
SCARD_READERSTATE reader_states[2];
LPTSTR reader_name_slots[2] = {NULL, NULL};
int reader_count = 0;
LONG lRet = 0;

static const BYTE COMMAND_GET_UID[5] = {0xFFu, 0xCAu, 0x00u, 0x00u, 0x00u};
static const BYTE PARAM_LOAD_KEY[11] = {0xFFu, 0x82u, 0x00u, 0x00u, 0x06u, 0x57u, 0x43u, 0x43u, 0x46u, 0x76u, 0x32u};
static const BYTE COMMAND_AUTH_BLOCK2[10] = {0xFFu, 0x86u, 0x00u, 0x00u, 0x05u, 0x01u, 0x00u, 0x02u, 0x61u, 0x00u};
static const BYTE COMMAND_READ_BLOCK2[5] = {0xFFu, 0xB0u, 0x00u, 0x02u, 0x10u};

void scard_poll(struct card_info *card_info, SCARDCONTEXT _hContext, LPCTSTR _readerName, uint8_t unit_no, bool *waitForTouch, bool *hasCard)
{
    printInfo("%s (%s): Update on reader : %s\n", __func__, module, reader_states[unit_no].szReader);
    
    if (*waitForTouch == false){
        printError("%s (%s): Card was rejected, still waiting for WaitTouch()\n", __func__, module);
        return;
    }
    
    // Connect to the smart card.
    LONG lRet = 0;
    SCARDHANDLE hCard;
    DWORD dwActiveProtocol;
    for (int retry = 0; retry < 100; retry++) // retry times has to be increased since poll rate is set to 500ms
    {
        if ((lRet = SCardConnect(_hContext, _readerName, SCARD_SHARE_EXCLUSIVE, SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &hCard, &dwActiveProtocol)) == SCARD_S_SUCCESS)
            break;

        Sleep(20);
    }

    if (lRet != SCARD_S_SUCCESS)
    {
        printError("%s (%s): Error connecting to the card: 0x%08X\n", __func__, module, lRet);
        return;
    }

    // set the reader params
    lRet = 0;
    LPCSCARD_IO_REQUEST pci = dwActiveProtocol == SCARD_PROTOCOL_T1 ? SCARD_PCI_T1 : SCARD_PCI_T0;
    DWORD cbRecv = MAX_APDU_SIZE;
    BYTE pbRecv[MAX_APDU_SIZE];

    // Read ATR to determine card type.
    TCHAR szReader[200];
    DWORD cchReader = 200;
    BYTE atr[32];
    DWORD cByteAtr = 32;
    lRet = SCardStatus(hCard, szReader, &cchReader, NULL, NULL, atr, &cByteAtr);
    if (lRet != SCARD_S_SUCCESS)
    {
        printError("%s (%s): Error getting card status: 0x%08X\n", __func__, module, lRet);
        return;
    }

    // Only care about 20-byte ATRs returned by arcade-type smart cards
    if (cByteAtr != 20)
    {
        printError("%s (%s): Ignoring card with len(%d) = %02X (%08X)\n", __func__, module, cByteAtr, atr, cByteAtr);
        return;
    }

    printInfo("%s (%s): atr Return: len(%d) = %02X (%08X)\n", __func__, module, cByteAtr, atr, cByteAtr);

    // Figure out if we should reverse the UID returned by the card based on the ATR protocol
    BYTE cardProtocol = atr[12];

    if (cardProtocol == SCARD_ATR_PROTOCOL_ISO14443_PART3) // Mifare
    {
        printWarning("%s (%s): Card protocol: ISO14443_PART3\n", __func__, module);

        printWarning("%s (%s): Loading key for block auth onto reader...\n", __func__, module);
        cbRecv = MAX_APDU_SIZE;
        if ((lRet = SCardTransmit(hCard, pci, PARAM_LOAD_KEY, sizeof(PARAM_LOAD_KEY), NULL, pbRecv, &cbRecv)) != SCARD_S_SUCCESS)
        {
            printError("%s (%s): Error loading key to reader : 0x%08X\n", __func__, module, lRet);
            return;
        }

        if (cbRecv > 1 && pbRecv[0] == PICC_ERROR)
        {
            printError("%s (%s): loading key failed\n", __func__, module);
            return;
        }

        printWarning("%s (%s): key has been loaded, authenticating block 2...\n", __func__, module);

        cbRecv = MAX_APDU_SIZE;
        if ((lRet = SCardTransmit(hCard, pci, COMMAND_AUTH_BLOCK2, sizeof(COMMAND_AUTH_BLOCK2), NULL, pbRecv, &cbRecv)) != SCARD_S_SUCCESS)
        {
            printError("%s (%s): Couldn't authenticate for block 2 : 0x%08X\n", __func__, module, lRet);
            return;
        }

        printWarning("%s (%s): authentication successful, reading block 2...\n", __func__, module);

        cbRecv = MAX_APDU_SIZE;
        if ((lRet = SCardTransmit(hCard, pci, COMMAND_READ_BLOCK2, sizeof(COMMAND_READ_BLOCK2), NULL, pbRecv, &cbRecv)) != SCARD_S_SUCCESS)
        {
            printError("%s (%s): Couldn't read block 2 : 0x%08X\n", __func__, module, lRet);
            return;
        }

        printWarning("%s (%s): Block 2 read successfully!\n", __func__, module);
        memcpy(card_info->card_id, pbRecv + 6, 10);
        card_info->card_type = Mifare;
        *waitForTouch = false;
        *hasCard = true;
        return;
    }
    else if (cardProtocol == SCARD_ATR_PROTOCOL_FELICA_212K) // Felica
    {
        printWarning("%s (%s): Card protocol: FELICA_212K\n", __func__, module);

        // Read mID
        cbRecv = MAX_APDU_SIZE;
        if ((lRet = SCardTransmit(hCard, pci, COMMAND_GET_UID, sizeof(COMMAND_GET_UID), NULL, pbRecv, &cbRecv)) != SCARD_S_SUCCESS)
        {
            printError("%s (%s): Error querying card UID: 0x%08X\n", __func__, module, lRet);
            return;
        }

        if (cbRecv > 1 && pbRecv[0] == PICC_ERROR)
        {
            printf("scard_update: UID query failed\n");
            printError("%s (%s): UID query failed\n", __func__, module);
            return;
        }

        if ((lRet = SCardDisconnect(hCard, SCARD_LEAVE_CARD)) != SCARD_S_SUCCESS)
            printError("%s (%s): Failed SCardDisconnect: 0x%08X\n", __func__, module, lRet);

        if (cbRecv < 8)
        {
            printWarning("%s (%s): Padding card uid to 8 bytes\n", __func__, module);
            memset(&pbRecv[cbRecv], 0, 8 - cbRecv);
        }
        else if (cbRecv > 8)
            printWarning("%s (%s): taking first 8 bytes of %d received\n", __func__, module, cbRecv);

        memcpy(card_info->card_id, pbRecv, 8);
        card_info->card_type = FeliCa;
        *waitForTouch = false;
        *hasCard = true;
        return;
    }
    else
    {
        printError("%s (%s): Unknown NFC Protocol: 0x%02X\n", __func__, module, cardProtocol);
        return;
    }

#pragma region LEGACY
    // Read UID
    // cbRecv = MAX_APDU_SIZE;
    // if ((lRet = SCardTransmit(hCard, pci, UID_CMD, sizeof(UID_CMD), NULL, pbRecv, &cbRecv)) != SCARD_S_SUCCESS)
    // {
    //     printError("%s (%s): Error querying card UID: 0x%08X\n", __func__, module, lRet);
    //     return;
    // }

    // if (cbRecv > 1 && pbRecv[0] == PICC_ERROR)
    // {
    //     printError("%s (%s): UID query failed\n", __func__, module);
    //     return;
    // }

    // if ((lRet = SCardDisconnect(hCard, SCARD_LEAVE_CARD)) != SCARD_S_SUCCESS)
    //     printError("%s (%s): Failed SCardDisconnect: 0x%08X\n", __func__, module, lRet);

    // if (cbRecv < 8)
    // {
    //     printWarning("%s (%s): Padding card uid to 8 bytes\n", __func__, module);
    //     memset(&pbRecv[cbRecv], 0, 8 - cbRecv);
    // }
    // else if (cbRecv > 8)
    //     printWarning("%s (%s): taking first 8 bytes of len(uid) = %02X\n", __func__, module, cbRecv);

    // // Copy UID to struct, reversing if necessary
    // if (shouldReverseUid)
    //     for (DWORD i = 0; i < 8; i++)
    //         card_info->uid[i] = pbRecv[7 - i];
    // else
    //     memcpy(card_info->uid, pbRecv, 8);

    //for (int i = 0; i < 8; ++i)
    //    buf[i] = card_info.uid[i];
#pragma endregion
}

void scard_clear(uint8_t unitNo)
{
    card_info_t empty_cardinfo;
}

void scard_update(struct card_info *card_info, bool *waitForTouch, bool *hasCard)
{
    if (reader_count < 1)
    {
        return;
    }

    lRet = SCardGetStatusChange(hContext, readCooldown, reader_states, reader_count);
    if (lRet == SCARD_E_TIMEOUT)
    {
        return;
    }
    else if (lRet != SCARD_S_SUCCESS)
    {
        printError("%s (%s): Failed SCardGetStatusChange: 0x%08X\n", __func__, module, lRet);
        return;
    }

    for (uint8_t unit_no = 0; unit_no < reader_count; unit_no++)
    {
        if (!(reader_states[unit_no].dwEventState & SCARD_STATE_CHANGED))
            continue;

        DWORD newState = reader_states[unit_no].dwEventState ^ SCARD_STATE_CHANGED;
        bool wasCardPresent = (reader_states[unit_no].dwCurrentState & SCARD_STATE_PRESENT) > 0;
        if (newState & SCARD_STATE_UNAVAILABLE)
        {
            printWarning("%s (%s): New card state: unavailable\n", __func__, module);
            Sleep(readCooldown);
        }
        else if (newState & SCARD_STATE_EMPTY)
        {
            printWarning("%s (%s): New card state: empty\n", __func__, module);
            //scard_clear(unit_no);
        }
        else if (newState & SCARD_STATE_PRESENT && !wasCardPresent)
        {
            printInfo("%s (%s): New card state: present\n", __func__, module);
            scard_poll(card_info, hContext, reader_states[unit_no].szReader, unit_no, waitForTouch, hasCard);
        }

        reader_states[unit_no].dwCurrentState = reader_states[unit_no].dwEventState;
    }

    return;
}

bool scard_init()
{
    if ((lRet = SCardEstablishContext(SCARD_SCOPE_USER, NULL, NULL, &hContext)) != SCARD_S_SUCCESS)
    {
        //  log_warning("scard", "failed to establish SCard context: {}", bin2hex(&lRet, sizeof(LONG)));
        return lRet;
    }

    LPCTSTR reader = NULL;

    int readerNameLen = 0;

    // get list of readers
    LPTSTR reader_list = NULL;
    auto pcchReaders = SCARD_AUTOALLOCATE;
    lRet = SCardListReaders(hContext, NULL, (LPTSTR)&reader_list, &pcchReaders);

    int slot0_idx = -1;
    int slot1_idx = -1;
    int readerCount = 0;
    switch (lRet)
    {
    case SCARD_E_NO_READERS_AVAILABLE:
        printError("%s (%s): No readers available\n", __func__, module);
        return FALSE;

    case SCARD_S_SUCCESS:

        // So WinAPI has this terrible "multi-string" concept wherein you have a list
        // of null-terminated strings, terminated by a double-null.
        for (reader = reader_list; *reader; reader = reader + lstrlen(reader) + 1)
        {
            printInfo("%s (%s): Found reader: %s\n", __func__, module, reader);
            readerCount++;

            // Connect to reader and send PICC operating params command
            LONG lRet = 0;
            SCARDHANDLE hCard;
            DWORD dwActiveProtocol;
            lRet = SCardConnect(hContext, reader, SCARD_SHARE_DIRECT, 0, &hCard, &dwActiveProtocol);
            if (lRet != SCARD_S_SUCCESS)
            {
                printError("%s (%s): Error connecting to the reader: 0x%08X\n", __func__, module, lRet);
                continue;
            }
            printInfo("%s (%s): Connected to reader: %s, sending PICC operating params command\n", __func__, module, reader);

            // set the reader params
            lRet = 0;
            DWORD cbRecv = MAX_APDU_SIZE;
            BYTE pbRecv[MAX_APDU_SIZE];
            lRet = SCardControl(hCard, SCARD_CTL_CODE(3500), PICC_OPERATING_PARAM_CMD, sizeof(PICC_OPERATING_PARAM_CMD), pbRecv, cbRecv, &cbRecv);
            Sleep(100);
            if (lRet != SCARD_S_SUCCESS)
            {
                printError("%s (%s): Error setting PICC params: 0x%08X\n", __func__, module, lRet);
                return FALSE;
            }

            if (cbRecv > 2 && pbRecv[0] != PICC_SUCCESS && pbRecv[1] != PICC_OPERATING_PARAMS)
            {
                printError("%s (%s): PICC params not valid 0x%02X != 0x%02X\n", __func__, module, pbRecv[1], PICC_OPERATING_PARAMS);
                return FALSE;
            }

            // Disconnect from reader
            if ((lRet = SCardDisconnect(hCard, SCARD_LEAVE_CARD)) != SCARD_S_SUCCESS)
            {
                printError("%s (%s): Failed SCardDisconnect: 0x%08X\n", __func__, module, lRet);
            }
            else
            {
                printInfo("%s (%s): Disconnected from reader: %s, this is expected behavior\n", __func__, module, reader);
            }

            break;
        }

        // If we have at least two readers, assign readers to slots as necessary.
        if (readerCount >= 2)
        {
            if (slot1_idx != 0)
                slot0_idx = 0;
            if (slot0_idx != 1)
                slot1_idx = 1;
        }

        // if the reader count is 1 and no reader was set, set first reader
        if (readerCount == 1 && slot0_idx < 0 && slot1_idx < 0)
            slot0_idx = 0;

        // If we somehow only found slot 1, promote slot 1 to slot 0.
        if (slot0_idx < 0 && slot1_idx >= 0)
        {
            slot0_idx = slot1_idx;
            slot1_idx = -1;
        }

        // Extract the relevant names from the multi-string.
        int i;
        for (i = 0, reader = reader_list; *reader; reader = reader + lstrlen(reader) + 1, i++)
        {
            if (slot0_idx == i)
            {
                readerNameLen = lstrlen(reader);
                reader_name_slots[0] = (LPTSTR)HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS, sizeof(TCHAR) * (readerNameLen + 1));
                memcpy(reader_name_slots[0], &reader[0], (size_t)(readerNameLen + 1));
            }
            if (slot1_idx == i)
            {
                readerNameLen = lstrlen(reader);
                reader_name_slots[1] = (LPTSTR)HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS, sizeof(TCHAR) * (readerNameLen + 1));
                memcpy(reader_name_slots[1], &reader[0], (size_t)(readerNameLen + 1));
            }
            break;
        }

        if (reader_name_slots[0])
            printInfo("%s (%s): Using reader slot 0: %s\n", __func__, module, reader_name_slots[0]);

        if (reader_name_slots[1])
            printInfo("%s (%s): Using reader slot 1: %s\n", __func__, module, reader_name_slots[1]);

        reader_count = reader_name_slots[1] ? 2 : 1;

        memset(&reader_states[0], 0, sizeof(SCARD_READERSTATE));
        reader_states[0].szReader = reader_name_slots[0];

        memset(&reader_states[1], 0, sizeof(SCARD_READERSTATE));
        reader_states[1].szReader = reader_name_slots[1];
        return TRUE;

    default:
        printWarning("%s (%s): Failed SCardListReaders: 0x%08X\n", __func__, module, lRet);
        return FALSE;
    }

    if (reader_list)
    {
        SCardFreeMemory(hContext, reader_list);
    }
}