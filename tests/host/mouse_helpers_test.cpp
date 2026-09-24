#include "arch/mouse_helpers.hpp"

#include <assert.h>
#include <stdint.h>

using namespace linux95::mouse;

static void test_bad_sync_is_ignored()
{
    PacketDecoder decoder;
    MouseEvent event{};

    assert(!decoder.feed(0x00, event));

    // A later valid first byte must start a fresh packet.
    // Sync + Y sign.
    assert(!decoder.feed(0x28, event));
    assert(!decoder.feed(0x05, event));
    assert(decoder.feed(0xFD, event));

    assert(event.dx == 5);
    assert(event.dy == -3);
    assert(!event.left);
    assert(!event.right);
    assert(!event.middle);
}

static void test_left_button()
{
    PacketDecoder decoder;
    MouseEvent event{};

    assert(!decoder.feed(0x09, event));
    assert(!decoder.feed(0x00, event));
    assert(decoder.feed(0x00, event));

    assert(event.dx == 0);
    assert(event.dy == 0);
    assert(event.left);
    assert(!event.right);
    assert(!event.middle);
}

static void test_all_buttons()
{
    PacketDecoder decoder;
    MouseEvent event{};

    // Bit 3 sync + left/right/middle.
    assert(!decoder.feed(0x0F, event));
    assert(!decoder.feed(0x00, event));
    assert(decoder.feed(0x00, event));

    assert(event.left);
    assert(event.right);
    assert(event.middle);
}

static void test_incomplete_packet_emits_nothing()
{
    PacketDecoder decoder;
    MouseEvent event{};

    assert(!decoder.feed(0x08, event));
    assert(!decoder.feed(0x01, event));

    decoder.reset();

    assert(!decoder.feed(0x08, event));
    assert(!decoder.feed(0x02, event));
    assert(decoder.feed(0x03, event));

    assert(event.dx == 2);
    assert(event.dy == 3);
}

static void test_negative_x_sign_extension()
{
    PacketDecoder decoder;
    MouseEvent event{};

    // Sync + X sign.
    assert(!decoder.feed(0x18, event));
    assert(!decoder.feed(0xFB, event));
    assert(decoder.feed(0x00, event));

    assert(event.dx == -5);
    assert(event.dy == 0);
}

static void test_overflow_packet_is_consumed_and_discarded()
{
    PacketDecoder decoder;
    MouseEvent event{};

    // Sync + X overflow.
    assert(!decoder.feed(0x48, event));
    assert(!decoder.feed(0x7F, event));

    // Third byte consumes the packet but emits no event.
    assert(!decoder.feed(0x00, event));

    // Decoder must recover for the next valid packet.
    assert(!decoder.feed(0x08, event));
    assert(!decoder.feed(0x04, event));
    assert(decoder.feed(0x02, event));

    assert(event.dx == 4);
    assert(event.dy == 2);
}


static void test_axis_sign_comes_from_status_byte()
{
    PacketDecoder decoder;
    MouseEvent event{};

    // No X/Y sign flags in byte 1.
    // Raw X 0xFD must therefore remain +253.
    assert(!decoder.feed(0x08, event));
    assert(!decoder.feed(0xFD, event));
    assert(decoder.feed(0x00, event));

    assert(event.dx == 253);
    assert(event.dy == 0);
}

int main()
{
    test_bad_sync_is_ignored();
    test_left_button();
    test_all_buttons();
    test_incomplete_packet_emits_nothing();
    test_negative_x_sign_extension();
    test_overflow_packet_is_consumed_and_discarded();
    test_axis_sign_comes_from_status_byte();

    return 0;
}
