// 32-bit guest handles: kind in the low 2 bits, a 6-bit reuse serial above that, and the table
// index in the top 24 bits; 0 is null.
#include <cstdio>

#include "check.h"
#include "zb/jni_handles.h"

int main() {
    using zb::HandleKind;
    using Ref = std::optional<std::uint64_t>;

    CHECK(!zb::handle_kind(0));
    CHECK(zb::handle_kind(zb::make_handle(HandleKind::Global, 7)) == HandleKind::Global);
    CHECK(zb::handle_index(zb::make_handle(HandleKind::WeakGlobal, 7)) == 7);

    // make_handle / handle_serial round trip, including the mod-64 wrap.
    CHECK(zb::handle_serial(zb::make_handle(HandleKind::Local, 5, 0)) == 0);
    CHECK(zb::handle_serial(zb::make_handle(HandleKind::Local, 5, 63)) == 63);
    CHECK(zb::handle_serial(zb::make_handle(HandleKind::Local, 5, 64)) == 0);   // wraps mod 64
    CHECK(zb::handle_serial(zb::make_handle(HandleKind::Local, 5, 65)) == 1);
    CHECK(zb::handle_index(zb::make_handle(HandleKind::Local, 5, 65)) == 5);

    zb::LocalHandles locals;
    CHECK(locals.add(0) == 0);
    CHECK(locals.get(0) == Ref(0));
    const std::uint32_t a = locals.add(0x7F00000000A0ull);  // base frame created on demand
    CHECK(locals.frame_count() == 1);
    CHECK(a != 0 && zb::handle_kind(a) == HandleKind::Local);
    CHECK(locals.get(a) == Ref(0x7F00000000A0ull));

    locals.push_frame();
    const std::uint32_t b = locals.add(0x7F00000000B0ull);
    const std::uint32_t c = locals.add(0x7F00000000C0ull);
    CHECK(locals.remove(b) == Ref(0x7F00000000B0ull));
    CHECK(!locals.get(b));     // deleted
    CHECK(!locals.remove(b));  // double delete is invalid
    const std::vector<std::uint64_t> released = locals.pop_frame();
    CHECK(released.size() == 1 && released[0] == 0x7F00000000C0ull);
    CHECK(!locals.get(c));                           // gone with its frame
    CHECK(locals.get(a) == Ref(0x7F00000000A0ull));  // outer frame intact
    CHECK(!locals.get(zb::make_handle(HandleKind::Global, 0)));  // wrong kind
    CHECK(!locals.get(zb::make_handle(HandleKind::Local, 99)));  // out of range
    CHECK(locals.remove(0) == Ref(0));                           // DeleteLocalRef(NULL) is allowed
    CHECK(locals.pop_frame().size() == 1);
    CHECK(locals.pop_frame().empty() && locals.frame_count() == 0);

    // Stale local after pop: a slot reused by a new frame must not answer to the old handle.
    {
        zb::LocalHandles stale;
        stale.push_frame();
        const std::uint32_t h1 = stale.add(0x1111);
        CHECK(stale.pop_frame().size() == 1);  // releases the frame, bumps the slot's serial
        const std::uint32_t h2 = stale.add(0x2222);  // lands on the same slot index
        CHECK(zb::handle_index(h1) == zb::handle_index(h2));
        CHECK(h1 != h2);
        CHECK(!stale.get(h1));               // stale handle, wrong serial
        CHECK(stale.get(h2) == Ref(0x2222));  // new handle is valid
    }

    // LIFO reclaim: repeatedly adding and immediately removing the top-of-frame slot must not
    // grow the underlying storage without bound.
    {
        zb::LocalHandles lifo;
        lifo.push_frame();
        for (int i = 0; i < 1000000; ++i) {
            const std::uint32_t h = lifo.add(0x3000 + static_cast<std::uint64_t>(i));
            CHECK(lifo.remove(h) == Ref(0x3000 + static_cast<std::uint64_t>(i)));
        }
        const std::uint32_t y = lifo.add(0x4000);
        CHECK(zb::handle_index(y) < 2);
    }

    // Remove from an outer frame: releasing a's slot must not disturb b in the inner frame, and
    // popping the inner frame must release only b.
    {
        zb::LocalHandles outer;
        outer.push_frame();
        const std::uint32_t oa = outer.add(0x5A);
        outer.push_frame();
        const std::uint32_t ob = outer.add(0x5B);
        CHECK(outer.remove(oa) == Ref(0x5A));
        CHECK(outer.get(ob) == Ref(0x5B));  // still valid, unaffected by the outer removal
        const std::vector<std::uint64_t> rel = outer.pop_frame();
        CHECK(rel.size() == 1 && rel[0] == 0x5B);
    }

    zb::GlobalHandles globals(HandleKind::Global);
    const std::uint32_t g1 = globals.add(0x10);
    const std::uint32_t g2 = globals.add(0x20);
    CHECK(zb::handle_kind(g1) == HandleKind::Global && g1 != g2);
    CHECK(globals.get(g2) == Ref(0x20));
    CHECK(globals.remove(g1) == Ref(0x10));
    CHECK(!globals.get(g1) && !globals.remove(g1));
    const std::uint32_t g3 = globals.add(0x30);
    CHECK(zb::handle_index(g3) == zb::handle_index(g1) && g3 != g1);  // freed slot reused, new serial
    CHECK(!globals.get(zb::make_handle(HandleKind::WeakGlobal, zb::handle_index(g2))));  // kind mismatch

    // Global use-after-delete: a stale handle to a reused slot must be rejected by both get and
    // remove.
    {
        zb::GlobalHandles gg(HandleKind::Global);
        const std::uint32_t g = gg.add(0x60);
        CHECK(gg.remove(g) == Ref(0x60));
        const std::uint32_t reused = gg.add(0x70);  // reuses g's slot
        CHECK(zb::handle_index(reused) == zb::handle_index(g));
        CHECK(!gg.get(g));
        CHECK(!gg.remove(g));
        CHECK(gg.get(reused) == Ref(0x70));
    }

    zb::IdTable ids;
    CHECK(ids.intern(0) == 0);
    const std::uint32_t m1 = ids.intern(0x7F0000001000ull);
    const std::uint32_t m2 = ids.intern(0x7F0000002000ull);
    CHECK(m1 == 1 && m2 == 2 && ids.intern(0x7F0000001000ull) == m1);
    CHECK(ids.get(m2) == Ref(0x7F0000002000ull));
    CHECK(!ids.get(3));

    std::printf("jni_handles_test ok\n");
    return 0;
}
