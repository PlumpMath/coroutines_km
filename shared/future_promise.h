#pragma once

#include <std_emu.h>

#include <experimental/coro.h>

namespace coro
{

namespace detail
{

template <typename PlatformEventType = std_emu::DefaultPlatformEvent>
struct shared_state_base
{
public:

    shared_state_base() = default;

    shared_state_base(const shared_state_base& ) = delete;
    shared_state_base& operator=(const shared_state_base& ) = delete;

    shared_state_base(shared_state_base&&) = default;
    shared_state_base& operator=(shared_state_base&& other) = default;

    ~shared_state_base()
    {
        DestroyCoroutine();
    }

    std_emu::error_code Initialize()
    {
        return m_event.Initialize();
    }

    std_emu::error_code Wait()
    {
        if (!IsValid())
        {
            return std_emu::errc::coro_invalid_shared_state;
        }

        return m_event.Wait();
    }

    bool IsValid() const
    {
        return m_event.IsValid();
    }

    void SetError(const std_emu::error_code& ec)
    {
        m_error = ec;
    }

    std_emu::error_code GetError() const
    {
        return m_error;
    }

    std_emu::error_code Notify(std::experimental::coroutine_handle<> coroutine)
    {
        if (!IsValid())
        {
            return std_emu::errc::coro_invalid_shared_state;
        }

        m_coroutine = coroutine;

        return m_event.Notify();
    }

    std_emu::error_code Notify()
    {
        if (!IsValid())
        {
            return std_emu::errc::coro_invalid_shared_state;
        }

        return m_event.Notify();
    }

    void DestroyCoroutine()
    {
        if (m_coroutine)
        {
            m_coroutine.destroy();
            m_coroutine = nullptr;
        }
    }

private:
    PlatformEventType m_event;
    std_emu::error_code m_error;

    std::experimental::coroutine_handle<> m_coroutine;
};



template <typename Type>
struct shared_state : private shared_state_base<>
{
public:

    using shared_state_base<>::Initialize;
    using shared_state_base<>::IsValid;
    using shared_state_base<>::SetError;
    using shared_state_base<>::Wait;
    using shared_state_base<>::Notify;
    using shared_state_base<>::DestroyCoroutine;

    void SetValue(const Type& value)
    {
        m_value = value;
    }

    std_emu::error_code GetValue(Type& value)
    {
        auto err = GetError();
        if (err)
        {
            return err;
        }

        value = std_emu::move(m_value);
        return std_emu::error_code();
    }

private:
    Type m_value;
};

template <>
struct shared_state<void> : private shared_state_base<>
{
public:

    using shared_state_base<>::Initialize;
    using shared_state_base<>::IsValid;
    using shared_state_base<>::SetError;
    using shared_state_base<>::Wait;
    using shared_state_base<>::Notify;
    using shared_state_base<>::DestroyCoroutine;

    void SetValue()
    {
    }

    std_emu::error_code GetValue()
    {
        return GetError();
    }
};


template <typename Type>
using shared_state_ptr = std_emu::shared_ptr<shared_state<Type>>;

template <typename Type>
bool IsValid(const shared_state_ptr<Type>& state)
{
    if (!state)
    {
        return false;
    }

    return state->IsValid();
}

} // namespace detail


template <typename Type>
struct future
{
    future()
        : m_state()
    {
    }

    ~future()
    {
        if (m_state)
        {
            m_state->DestroyCoroutine();
        }
    }

    explicit future(const detail::shared_state_ptr<Type>& state)
        : m_state(state)
    {
    }

    future(const future&) = delete;
    future& operator=(const future& ) = delete;

    future(future&& ) = default;
    future& operator=(future&& ) = default;

    // obtain result

    template <typename ...Value>
    std_emu::error_code get_value(Value&&... val)
    {
        if (!detail::IsValid(m_state))
        {
            return std_emu::errc::coro_invalid_shared_state;
        }

        auto err = m_state->Wait();
        if (err)
        {
            return err;
        }

        return m_state->GetValue(std::forward<Value>(val)...);
    }

private:
    detail::shared_state_ptr<Type> m_state;
};



template <typename Type>
struct promise
{
    promise()
        : m_state()
    {
        init();
    }

    promise(const promise&) = delete;
    promise& operator=(const promise& ) = delete;

    promise(promise&& ) = default;
    promise& operator=(promise&& ) = default;

    bool valid() const
    {
        return detail::IsValid(m_state);
    }

    // communication with future

    future<Type> get_future()
    {
        return future<Type>{m_state};
    }


    template <typename ...Args>
    void set_value(Args&&... args)
    {
        assert(detail::IsValid(m_state));

        m_state->SetValue(std::forward<Args>(args)...);
    }

    void set_error(const std_emu::error_code& ec)
    {
        assert(detail::IsValid(m_state));

        m_state->SetError(ec);
    }

    void notify()
    {
        assert(detail::IsValid(m_state));

        m_state->Notify();
    }

    void notify(std::experimental::coroutine_handle<promise> coroutine)
    {
        assert(detail::IsValid(m_state));

        m_state->Notify(coroutine);
    }

    // promise_type implementation

    /// @note This type is used for early error detection
    /// If we fail to create shared state for future/promise
    /// we stop further processing as soon as possible
	struct destroy_coro_if
	{
        explicit destroy_coro_if(bool shouldDestroy)
            : m_shouldDestroy(shouldDestroy)
        {
        }

		bool await_ready() noexcept
		{
			return !m_shouldDestroy;
		}

		void await_suspend(std::experimental::coroutine_handle<promise> coroutine) noexcept
		{
            assert(m_shouldDestroy);

            coroutine.destroy();
		}

		void await_resume() noexcept
		{
            assert(!m_shouldDestroy);
		}

    private:
        bool m_shouldDestroy;
	};

    destroy_coro_if initial_suspend()
    {
        return destroy_coro_if{!detail::IsValid(m_state)};
    }

    future<Type> get_return_object()
    {
        return get_future();
    }

#if defined(CORO_NO_EXCEPTIONS)

    static auto get_return_object_on_allocation_failure()
    {
        return coro::future<Type>{};
    }

#endif /* CORO_NO_EXCEPTIONS */

    /// This type is used for clients notification and destruction coroutine in async thread
	struct notify_and_destroy_coroutine_in_async_context
	{
        notify_and_destroy_coroutine_in_async_context(detail::shared_state_ptr<Type> state)
            : m_state(state)
        {
        }

		bool await_ready() noexcept
		{
            return true;
		}

		void await_suspend(std::experimental::coroutine_handle<> /* coroutine */) noexcept
		{
            assert(false && "should not be called");
		}

		void await_resume() noexcept
		{
            m_state->Notify();
		}

        detail::shared_state_ptr<Type> m_state;
	};

    /// Notify future and destroy coroutine in caller context
	struct notify_and_destroy_coroutine_in_caller_context
	{
        notify_and_destroy_coroutine_in_caller_context()
        {
        }

		bool await_ready() noexcept
		{
			return false;
		}

		void await_suspend(std::experimental::coroutine_handle<promise> coroutine) noexcept
		{
            coroutine.promise().notify(coroutine);
		}

		void await_resume() noexcept
		{
            assert(false && "should not be called");
		}
	};

#if 0
    notify_and_destroy_coroutine_in_async_context final_suspend()
    {
        return notify_and_destroy_coroutine_in_async_context{m_state};
    }
#endif //0

    notify_and_destroy_coroutine_in_caller_context final_suspend()
    {
        return {};
    }

private:
    detail::shared_state_ptr<Type> m_state;

    std_emu::error_code init()
    {
        detail::shared_state_ptr<Type> tmp;

        auto err = std_emu::make_shared(tmp);
        if (err)
        {
            return err;
        }

        err = tmp->Initialize();
        if (err)
        {
            return err;
        }
        
        m_state = std_emu::move(tmp);

        return std_emu::error_code();
    }
};

} // namespace coro


namespace std
{
namespace experimental
{

template <typename Type, typename... Args>
struct coroutine_traits<coro::future<Type>, Args...>
{
    using promise_type = coro::promise<Type>;
};


} // namespace experimental
} // namespace std
