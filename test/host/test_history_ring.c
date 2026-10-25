/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include "history_ring.h"

#include "tinytest.h"

static hist_rec_t mk(uint32_t t, int16_t a, int16_t b)
{
    hist_rec_t r = { .t = t, .temp_c100 = { a, b } };
    return r;
}

static void test_init_empty(void)
{
    hist_store_t s;
    hist_store_init(&s);
    CHECK(s.magic == HIST_MAGIC);
    CHECK(s.version == HIST_VERSION);
    CHECK(s.count == 0);

    hist_rec_t out[HIST_RING_CAP];
    CHECK(hist_store_sorted(&s, out) == 0);
}

static void test_append_read(void)
{
    hist_store_t s;
    hist_store_init(&s);
    hist_store_append(&s, mk(100, 2391, 2200));
    hist_store_append(&s, mk(200, HIST_NO_TEMP, HIST_NO_TEMP));
    hist_store_append(&s, mk(300, -50, HIST_NO_TEMP));

    hist_rec_t out[HIST_RING_CAP];
    CHECK(hist_store_sorted(&s, out) == 3);
    CHECK(out[0].t == 100 && out[0].temp_c100[0] == 2391 &&
          out[0].temp_c100[1] == 2200);
    CHECK(out[1].t == 200 && out[1].temp_c100[0] == HIST_NO_TEMP &&
          out[1].temp_c100[1] == HIST_NO_TEMP);
    CHECK(out[2].t == 300 && out[2].temp_c100[0] == -50 &&
          out[2].temp_c100[1] == HIST_NO_TEMP);
}

static void test_replace_same_window(void)
{
    hist_store_t s;
    hist_store_init(&s);
    hist_store_append(&s, mk(100, 2391, 0));
    hist_store_append(&s, mk(200, 2392, 0));

    hist_store_append(&s, mk(200, 2395, 1));

    CHECK(s.count == 2);
    hist_rec_t out[HIST_RING_CAP];
    CHECK(hist_store_sorted(&s, out) == 2);
    CHECK(out[1].t == 200 && out[1].temp_c100[0] == 2395 &&
          out[1].temp_c100[1] == 1);

    hist_store_append(&s, mk(150, 2390, 0));
    CHECK(s.count == 3);
    CHECK(hist_store_sorted(&s, out) == 3);
    CHECK(out[1].t == 150);
}

static void test_wrap(void)
{
    hist_store_t s;
    hist_store_init(&s);

    for (uint32_t i = 0; i < HIST_RING_CAP + 5; i++) {
        hist_store_append(&s, mk(1000 + i * 10, (int16_t)(2000 + i), 2000));
    }
    CHECK(s.count == HIST_RING_CAP);

    hist_rec_t out[HIST_RING_CAP];
    CHECK(hist_store_sorted(&s, out) == HIST_RING_CAP);
    CHECK(out[0].t == 1000 + 5 * 10);
    CHECK(out[HIST_RING_CAP - 1].t ==
          1000 + (HIST_RING_CAP + 4) * 10);
    for (uint16_t i = 1; i < HIST_RING_CAP; i++) {
        CHECK(out[i].t == out[i - 1].t + 10);
    }
}

static void test_backwards_clock(void)
{
    hist_store_t s;
    hist_store_init(&s);
    hist_store_append(&s, mk(300, 2390, 0));
    hist_store_append(&s, mk(200, 2391, 0));
    hist_store_append(&s, mk(400, 2392, 0));

    hist_rec_t out[HIST_RING_CAP];
    CHECK(hist_store_sorted(&s, out) == 3);
    CHECK(out[0].t == 200 && out[1].t == 300 && out[2].t == 400);
}

static void test_empty_slot_skipped(void)
{
    hist_store_t s;
    hist_store_init(&s);

    s.rec[0] = mk(100, 2390, 0);
    s.rec[2] = mk(300, 2392, 0);
    s.rec[3] = mk(400, 2393, 0);
    s.next = 4;
    s.count = 4;

    hist_rec_t out[HIST_RING_CAP];
    CHECK(hist_store_sorted(&s, out) == 3);
    CHECK(out[0].t == 100 && out[1].t == 300 && out[2].t == 400);
}

int main(void)
{
    test_init_empty();
    test_append_read();
    test_replace_same_window();
    test_wrap();
    test_backwards_clock();
    test_empty_slot_skipped();
    REPORT();
}
