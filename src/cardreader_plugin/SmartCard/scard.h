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

#pragma once

// #include <cstdint>
#include "../../helpers.h"

// Card types
enum AIME_CARDTYPE
{
    Mifare = 0b001,
    FeliCa = 0b010,
    CardIO = 0b100,
};

// cardinfo_t is a description of a card that was presented to a reader
typedef struct card_info
{
    int card_type;
    //uint8_t uid[8];
    uint8_t card_id[32];
} card_info_t;

void scard_update(struct card_info *card_info, bool *waitForTouch, bool *hasCard, bool *blockBadFelica);

void scard_poll(struct card_info *card_info, SCARDCONTEXT _hContext, LPCTSTR _readerName, uint8_t unit_no, bool *waitForTouch, bool *hasCard, bool *blockBadFelica);

void scard_clear(uint8_t unitNo);

bool scard_init();