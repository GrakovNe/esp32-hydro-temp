/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "fresh.h"

#include "tinytest.h"

int main(void)
{
    CHECK(fresh_max_age_ms(10000) == 50000);
    CHECK(fresh_max_age_ms(300000) == 630000);
    CHECK(fresh_max_age_ms(3600000) == 7230000);

    sensors_reading_t r = { .max_age_ms = fresh_max_age_ms(300000) };
    r.slot[0].valid = true;
    r.slot[0].age_ms = 630000;
    CHECK(slot_fresh(&r, 0));
    r.slot[0].age_ms = 630001;
    CHECK(!slot_fresh(&r, 0));

    r.slot[1].valid = false;
    r.slot[1].age_ms = 0;
    CHECK(!slot_fresh(&r, 1));

    r.slot[1].valid = true;
    r.slot[1].age_ms = UINT32_MAX;
    CHECK(!slot_fresh(&r, 1));
    REPORT();
}
