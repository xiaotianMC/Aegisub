// Copyright (c) 2026, Aegisub contributors
// SPDX-License-Identifier: ISC
#pragma once
#include <libaegisub/signal.h>
#include <libaegisub/subtitle_rebind.h>
#include <functional>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <type_traits>

// Project-local registration, not a replacement for the generic Signal contract.
// Ordinary delivery retains reverse registration order and exception propagation.
// Installation delivery delegates each subscriber to the existing C0 protocol.
template<class... Args> class SubtitleModelObservers {
public:
    using Ticket = agi::SubtitleRebindBatch::Ticket;
private:
    struct Subscriber {
        std::function<void(Args..., Ticket)> callback;
        std::function<void()> prepare;
    };
    struct Slot {
        std::string name;
        agi::signal::Signal<Args..., Ticket> signal;
        std::weak_ptr<Subscriber> subscriber;
    };
    struct State {
        std::vector<std::shared_ptr<Slot>> slots;
        size_t frozen = 0;
        uint64_t sequence = 0;
        bool alive = true;
    };
    std::shared_ptr<State> state = std::make_shared<State>();
    std::string prefix;
    template<class F> static void Invoke(F& f, Ticket ticket, Args... args) {
        if constexpr (std::is_invocable_v<F&, Args..., Ticket>) f(args..., ticket);
        else if constexpr (sizeof...(Args) > 0) {
            auto values = std::forward_as_tuple(args...);
            using First = decltype(std::get<0>(values));
            if constexpr (std::is_invocable_v<F&, First, Ticket>) f(std::get<0>(values), ticket);
            else if constexpr (std::is_invocable_v<F&, Ticket>) f(ticket);
            else if constexpr (std::is_invocable_v<F&, Args...>) f(args...);
            else if constexpr (std::is_invocable_v<F&, First>) f(std::get<0>(values));
            else f();
        }
        else if constexpr (std::is_invocable_v<F&, Ticket>) f(ticket);
        else f();
    }
public:
    explicit SubtitleModelObservers(std::string prefix) : prefix(std::move(prefix)) {}
    SubtitleModelObservers(SubtitleModelObservers const&) = delete;
    ~SubtitleModelObservers() { state->alive = false; }

    class Prepared {
        std::shared_ptr<State> state;
        std::vector<std::shared_ptr<Slot>> slots;
        friend class SubtitleModelObservers;
        explicit Prepared(std::shared_ptr<State> const& value) : state(value) {
            if (state->frozen == std::numeric_limits<size_t>::max()) throw std::overflow_error("Observer preparation count exhausted");
            for (auto it=value->slots.rbegin(); it!=value->slots.rend(); ++it)
                if (!(*it)->subscriber.expired()) slots.push_back(*it);
            ++state->frozen;
        }
    public:
        Prepared(Prepared const&) = delete;
        Prepared& operator=(Prepared const&) = delete;
        Prepared(Prepared&& other) noexcept : state(std::move(other.state)), slots(std::move(other.slots)) {}
        ~Prepared() { if (state) --state->frozen; }
        std::vector<std::string> Names() const {
            std::vector<std::string> names;
            for (auto const& slot : slots) names.push_back(slot->name);
            return names;
        }
        void Prepare() const {
            if (!state->alive) throw std::logic_error("Installation observer registry closed");
            for (auto const& slot : slots)
                if (auto subscriber=slot->subscriber.lock()) subscriber->prepare();
        }
        void Dispatch(agi::SubtitleRebindBatch& batch, size_t offset, Args... args) const {
            for (size_t i=0; i<slots.size(); ++i) {
                auto slot=slots[i];
                batch.Dispatch(offset+i, [&](Ticket ticket) {
                    if (!state->alive) throw std::logic_error("Installation observer registry closed");
                    slot->signal(args..., ticket);
                    // Only completed unsubscription withdraws the observer. A live
                    // subscriber's successful return NEVER acknowledges its ticket.
                    if (slot->subscriber.expired()) ticket.Acknowledge(ticket.Revision());
                });
            }
        }
    };
    Prepared PrepareObservers() const { return Prepared(state); }

    template<class F> agi::signal::UnscopedConnection Observe(std::string name, std::function<void()> prepare, F callback) const {
        if (state->frozen) throw std::logic_error("Cannot subscribe during model installation");
        if (state->sequence == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Observer identity exhausted");
        auto slot=std::make_shared<Slot>();
        slot->name=prefix+":"+std::to_string(++state->sequence)+":"+name;
        auto subscriber=std::make_shared<Subscriber>();
        subscriber->prepare=std::move(prepare);
        subscriber->callback=[callback=std::move(callback)](Args... args, Ticket ticket) mutable { Invoke(callback,ticket,args...); };
        slot->subscriber=subscriber;
        auto connection=slot->signal.Connect([subscriber](Args... args, Ticket ticket) {
            auto hold=subscriber; // Owner may unsubscribe/destroy itself in its callback.
            hold->callback(args...,ticket);
        });
        state->slots.push_back(slot);
        return connection;
    }
    template<class F> agi::signal::UnscopedConnection Add(F callback) const {
        return Observe("subscriber", []{}, std::move(callback));
    }
    template<class Method, class Object> agi::signal::UnscopedConnection Add(Method method, Object* object) const {
        return Add([method,object](auto&&... args) -> decltype(std::invoke(method,object,std::forward<decltype(args)>(args)...)) {
            return std::invoke(method,object,std::forward<decltype(args)>(args)...);
        });
    }
    void operator()(Args... args) const {
        auto slots=state->slots;
        for (auto it=slots.rbegin(); it!=slots.rend(); ++it) (*it)->signal(args...,Ticket{});
        std::erase_if(state->slots, [](auto const& slot){return slot->subscriber.expired();});
    }
};
