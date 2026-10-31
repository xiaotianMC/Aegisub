// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#include <main.h>
#include <libaegisub/subtitle_rebind.h>
#include <libaegisub/signal.h>

TEST(subtitle_rebind, normal_notifications_preserve_exception_behavior) {
    agi::signal::Signal<> signal;
    auto connection = signal.Connect([]{throw std::runtime_error("observer");});
    EXPECT_THROW(signal(),std::runtime_error);
}
TEST(subtitle_rebind, explicit_acknowledgements_keep_old_nodes_alive_and_writes_blocked) {
    agi::SubtitleEditGate gate;
    auto old = std::make_shared<int>(42); std::weak_ptr<int> lifetime = old;
    auto session = gate.TryBeginComparison(); ASSERT_TRUE(session);
    auto revision = session->AdvanceRevision();
    agi::SubtitleRebindBatch batch(std::move(*session),old,{"grid","editor"}); old.reset();
    batch.Dispatch(0,[](auto ticket){ticket.Acknowledge(ticket.Revision());});
    batch.Dispatch(1,[](auto){}); // Returning normally does not prove rebinding.
    EXPECT_FALSE(batch.Complete()); EXPECT_FALSE(lifetime.expired()); EXPECT_FALSE(gate.TryBeginWrite());
    EXPECT_EQ((std::vector<std::string>{"editor"}),batch.Pending());
    auto ticket=batch.Observer(1); ticket.Acknowledge(revision);
    EXPECT_TRUE(batch.Complete()); EXPECT_TRUE(lifetime.expired()); EXPECT_FALSE(ticket.Current());
    EXPECT_TRUE(gate.TryBeginWrite());
}
TEST(subtitle_rebind, exceptions_are_isolated_and_require_successful_retry) {
    agi::SubtitleEditGate gate; auto session=gate.TryBeginComparison(); ASSERT_TRUE(session);
    agi::SubtitleRebindBatch batch(std::move(*session),std::make_shared<int>(7),{"bad","good"});
    bool second=false;
    EXPECT_NO_THROW(batch.Dispatch(0,[](auto ticket){ticket.Acknowledge(ticket.Revision()); throw std::runtime_error("failed after ack");}));
    batch.Dispatch(1,[&](auto ticket){second=true;ticket.Acknowledge(ticket.Revision());});
    EXPECT_TRUE(second); EXPECT_TRUE(batch.Failure(0)); EXPECT_FALSE(batch.Complete());
    EXPECT_TRUE(gate.NeedsRebinding()); EXPECT_FALSE(gate.TryBeginWrite());
    batch.Dispatch(0,[](auto ticket){ticket.Acknowledge(ticket.Revision());});
    EXPECT_FALSE(batch.Failure(0)); EXPECT_TRUE(batch.Complete()); EXPECT_TRUE(gate.TryBeginWrite());
}
TEST(subtitle_rebind, abandoning_failed_batch_does_not_release_nodes_or_unlock) {
    agi::SubtitleEditGate gate; auto retired=std::make_shared<int>(8); std::weak_ptr<int> lifetime=retired;
    {
        auto session=gate.TryBeginComparison(); ASSERT_TRUE(session);
        agi::SubtitleRebindBatch batch(std::move(*session),retired,{"not-migrated"}); retired.reset();
    }
    EXPECT_FALSE(lifetime.expired()); EXPECT_TRUE(gate.NeedsRebinding()); EXPECT_FALSE(gate.TryBeginWrite());
    EXPECT_FALSE(gate.TryBeginComparison()); gate.Close(); EXPECT_TRUE(lifetime.expired());
}
TEST(subtitle_rebind, stale_observers_and_wrong_revisions_cannot_acknowledge) {
    agi::SubtitleEditGate gate; auto before=gate.Capture();
    auto session=gate.TryBeginComparison(); ASSERT_TRUE(session); auto target=session->AdvanceRevision();
    EXPECT_FALSE(before.Current());
    agi::SubtitleRebindBatch batch(std::move(*session),std::make_shared<int>(1),{"view"});
    auto ticket=batch.Observer(0);
    EXPECT_THROW(ticket.Acknowledge(before.Revision()),std::logic_error);
    gate.Close(); EXPECT_FALSE(ticket.Current());
    EXPECT_THROW(ticket.Revision(),std::logic_error);
    EXPECT_THROW(ticket.Acknowledge(target),std::logic_error);
}
TEST(subtitle_rebind, completion_cannot_free_nodes_inside_a_running_callback) {
    agi::SubtitleEditGate gate; auto session=gate.TryBeginComparison(); ASSERT_TRUE(session);
    auto retired=std::make_shared<int>(9); std::weak_ptr<int> lifetime=retired;
    agi::SubtitleRebindBatch batch(std::move(*session),retired,{"view"});retired.reset();
    batch.Dispatch(0,[&](auto ticket){
        ticket.Acknowledge(ticket.Revision()); EXPECT_FALSE(batch.Complete()); EXPECT_FALSE(lifetime.expired());
        EXPECT_THROW(batch.Dispatch(0,[](auto){}),std::logic_error);
    });
    EXPECT_TRUE(batch.Complete()); EXPECT_TRUE(lifetime.expired());
}
TEST(subtitle_rebind, document_close_during_callback_invalidates_ticket_but_pins_nodes) {
    agi::SubtitleEditGate gate; auto session=gate.TryBeginComparison();
    auto retired=std::make_shared<int>(9); std::weak_ptr<int> lifetime=retired;
    {
        agi::SubtitleRebindBatch batch(std::move(*session),retired,{"view"}); retired.reset();
        batch.Dispatch(0,[&](auto ticket){
            gate.Close(); EXPECT_FALSE(ticket.Current()); EXPECT_FALSE(lifetime.expired());
        });
        EXPECT_THROW(batch.Complete(),std::logic_error);
    }
    EXPECT_TRUE(lifetime.expired());
}
